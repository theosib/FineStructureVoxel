#include "finevox/core/io_manager.hpp"
#include "finevox/core/tick_journal.hpp"
#include "finevox/core/event_queue.hpp"  // ScheduledTick
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/serialization.hpp"
#include <algorithm>

namespace finevox {

IOManager::IOManager(const std::filesystem::path& worldPath)
    : worldPath_(worldPath) {
    std::filesystem::create_directories(worldPath_);
}

std::unique_ptr<IOManager> IOManager::forWorld(const std::string& worldName,
                                                const std::string& dimension) {
    auto regionPath = ResourceLocator::instance().regionPath(worldName, dimension);
    if (regionPath.empty()) {
        return nullptr;
    }
    return std::make_unique<IOManager>(regionPath);
}

IOManager::~IOManager() {
    stop();
}

void IOManager::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    loadQueue_.resetShutdown();
    saveQueue_.resetShutdown();

    loadThread_ = std::thread(&IOManager::loadThreadFunc, this);
    saveThread_ = std::thread(&IOManager::saveThreadFunc, this);
}

void IOManager::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }

    // Wake up queues and any flush() waiters
    loadQueue_.shutdown();
    saveQueue_.shutdown();
    flushCv_.notify_all();

    // Wait for threads to finish
    if (loadThread_.joinable()) {
        loadThread_.join();
    }
    if (saveThread_.joinable()) {
        saveThread_.join();
    }
}

void IOManager::requestLoad(ColumnPos pos, LoadCallback callback) {
    loadQueue_.push(LoadRequest{pos, std::move(callback)});
}

void IOManager::queueSave(ColumnPos pos, const ChunkColumn& column) {
    queueSave(pos, column, nullptr);
}

void IOManager::queueSave(ColumnPos pos, const ChunkColumn& column, SaveCallback callback) {
    // Serialize on the calling thread to avoid holding locks during serialization
    auto serialized = ColumnSerializer::toCBOR(column, pos.x, pos.z);

    {
        std::lock_guard lock(flushMutex_);
        ++unfinishedSaves_;
    }
    if (!saveQueue_.push(SaveRequest{pos, std::move(serialized), {}, std::move(callback), false})) {
        std::lock_guard lock(flushMutex_);
        --unfinishedSaves_;
        flushCv_.notify_all();
    }
}

void IOManager::queueSave(ColumnPos pos, const ChunkColumn& column,
                           std::vector<ScheduledTick> ticks, SaveCallback callback) {
    auto serialized = ColumnSerializer::toCBOR(column, pos.x, pos.z);

    {
        std::lock_guard lock(flushMutex_);
        ++unfinishedSaves_;
    }
    if (!saveQueue_.push(SaveRequest{pos, std::move(serialized), std::move(ticks), std::move(callback), false})) {
        std::lock_guard lock(flushMutex_);
        --unfinishedSaves_;
        flushCv_.notify_all();
    }
}

void IOManager::queueTickSave(ColumnPos pos, std::vector<ScheduledTick> ticks) {
    {
        std::lock_guard lock(flushMutex_);
        ++unfinishedSaves_;
    }
    if (!saveQueue_.push(SaveRequest{pos, {}, std::move(ticks), nullptr, true})) {
        std::lock_guard lock(flushMutex_);
        --unfinishedSaves_;
        flushCv_.notify_all();
    }
}

void IOManager::setTickLoadCallback(TickLoadCallback callback) {
    tickLoadCallback_ = std::move(callback);
}

void IOManager::flush() {
    // Wait until both saveQueue_ is empty AND any in-flight disk write/callback has completed
    std::unique_lock lock(flushMutex_);
    flushCv_.wait(lock, [this] {
        return (unfinishedSaves_ == 0 &&
                saveQueue_.empty() &&
                inFlightSaves_.load(std::memory_order_acquire) == 0) ||
               !running_.load(std::memory_order_acquire);
    });
}

bool IOManager::hasPendingLoads() const {
    return !loadQueue_.empty();
}

bool IOManager::hasPendingSaves() const {
    return !saveQueue_.empty() || inFlightSaves_.load(std::memory_order_acquire) > 0;
}

size_t IOManager::pendingLoadCount() const {
    return loadQueue_.size();
}

size_t IOManager::pendingSaveCount() const {
    return saveQueue_.size() + inFlightSaves_.load(std::memory_order_acquire);
}

size_t IOManager::regionFileCount() const {
    std::lock_guard lock(regionMutex_);
    return regionFiles_.size();
}

bool IOManager::isRegionOpen(RegionPos pos) const {
    uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(pos.rx)) << 32) |
                   static_cast<uint64_t>(static_cast<uint32_t>(pos.rz));
    std::lock_guard lock(regionMutex_);
    return regionFiles_.find(key) != regionFiles_.end();
}

void IOManager::setMaxOpenRegions(size_t count) {
    std::lock_guard lock(regionMutex_);
    maxOpenRegions_ = count;
    while (regionFiles_.size() > maxOpenRegions_ && !regionFiles_.empty()) {
        evictOldestRegion();
    }
}

// ============================================================================
// Thread functions
// ============================================================================

void IOManager::loadThreadFunc() {
    while (auto optRequest = loadQueue_.popWait()) {
        LoadRequest& request = *optRequest;

        // Perform load
        std::unique_ptr<ChunkColumn> column;

        RegionPos regionPos = RegionPos::fromColumn(request.pos);
        RegionFile* region = getOrOpenRegion(regionPos);

        if (region) {
            column = region->loadColumn(request.pos);
        }

        // Read tick journal and deliver to scheduler
        if (tickJournal_ && tickJournal_->hasJournal(request.pos)) {
            auto ticks = tickJournal_->readTicks(request.pos);
            tickJournal_->deleteJournal(request.pos);
            if (!ticks.empty() && tickLoadCallback_) {
                tickLoadCallback_(request.pos, std::move(ticks));
            }
        }

        // Invoke callback
        if (request.callback) {
            request.callback(request.pos, std::move(column));
        }
    }
}

void IOManager::saveThreadFunc() {
    while (auto optRequest = saveQueue_.popWait()) {
        SaveRequest& request = *optRequest;
        inFlightSaves_.fetch_add(1, std::memory_order_acq_rel);

        // Perform save
        bool success = false;

        if (request.ticksOnly) {
            // Tick-only save (eviction path): append ticks to journal
            if (tickJournal_ && !request.pendingTicks.empty()) {
                success = tickJournal_->appendTicks(request.pos, request.pendingTicks);
            } else {
                success = true;
            }
        } else {
            // Full column save: region write + tick journal
            RegionPos regionPos = RegionPos::fromColumn(request.pos);
            RegionFile* region = getOrOpenRegion(regionPos);

            if (region) {
                success = region->saveColumnRaw(request.pos, request.serializedData);
            }

            // Write tick journal (overwrite) alongside region data
            if (tickJournal_) {
                if (request.pendingTicks.empty()) {
                    tickJournal_->deleteJournal(request.pos);
                } else {
                    tickJournal_->writeTicks(request.pos, request.pendingTicks);
                }
            }
        }

        // Invoke callback
        if (request.callback) {
            request.callback(request.pos, success);
        }

        inFlightSaves_.fetch_sub(1, std::memory_order_acq_rel);
        {
            std::lock_guard lock(flushMutex_);
            if (unfinishedSaves_ > 0) {
                --unfinishedSaves_;
            }
        }
        flushCv_.notify_all();
    }
}

// ============================================================================
// Region file management (LRU cached)
// ============================================================================

RegionFile* IOManager::getOrOpenRegion(RegionPos pos) {
    uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(pos.rx)) << 32) |
                   static_cast<uint64_t>(static_cast<uint32_t>(pos.rz));

    std::lock_guard lock(regionMutex_);

    auto it = regionFiles_.find(key);
    if (it != regionFiles_.end()) {
        it->second.lastAccess = ++accessCounter_;
        return it->second.file.get();
    }

    // Evict least-recently-used region if at capacity
    while (regionFiles_.size() >= maxOpenRegions_ && !regionFiles_.empty()) {
        evictOldestRegion();
    }

    // Open new region file
    auto region = std::make_unique<RegionFile>(worldPath_, pos);
    auto* ptr = region.get();
    regionFiles_[key] = CachedRegion{std::move(region), ++accessCounter_};

    return ptr;
}

void IOManager::evictOldestRegion() {
    if (regionFiles_.empty()) {
        return;
    }

    auto oldestIt = regionFiles_.begin();
    for (auto it = std::next(regionFiles_.begin()); it != regionFiles_.end(); ++it) {
        if (it->second.lastAccess < oldestIt->second.lastAccess) {
            oldestIt = it;
        }
    }
    regionFiles_.erase(oldestIt);
}

}  // namespace finevox
