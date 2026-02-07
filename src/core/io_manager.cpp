#include "finevox/core/io_manager.hpp"
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

    loadThread_ = std::thread(&IOManager::loadThreadFunc, this);
    saveThread_ = std::thread(&IOManager::saveThreadFunc, this);
}

void IOManager::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }

    // Wake up threads
    {
        std::lock_guard lock(loadMutex_);
        loadCond_.notify_all();
    }
    {
        std::lock_guard lock(saveMutex_);
        saveCond_.notify_all();
    }

    // Wait for threads to finish
    if (loadThread_.joinable()) {
        loadThread_.join();
    }
    if (saveThread_.joinable()) {
        saveThread_.join();
    }
}

void IOManager::requestLoad(ColumnPos pos, LoadCallback callback) {
    std::lock_guard lock(loadMutex_);
    loadQueue_.push_back({pos, std::move(callback)});
    loadCond_.notify_one();
}

void IOManager::queueSave(ColumnPos pos, const ChunkColumn& column) {
    queueSave(pos, column, nullptr);
}

void IOManager::queueSave(ColumnPos pos, const ChunkColumn& column, SaveCallback callback) {
    // Serialize on the calling thread to avoid holding locks during serialization
    auto serialized = ColumnSerializer::toCBOR(column, pos.x, pos.z);

    std::lock_guard lock(saveMutex_);
    saveQueue_.push_back({pos, std::move(serialized), std::move(callback)});
    saveCond_.notify_one();
}

void IOManager::flush() {
    // Wait for save queue to empty
    std::unique_lock lock(saveMutex_);
    while (!saveQueue_.empty() && running_) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        lock.lock();
    }
}

bool IOManager::hasPendingLoads() const {
    std::lock_guard lock(loadMutex_);
    return !loadQueue_.empty();
}

bool IOManager::hasPendingSaves() const {
    std::lock_guard lock(saveMutex_);
    return !saveQueue_.empty();
}

size_t IOManager::pendingLoadCount() const {
    std::lock_guard lock(loadMutex_);
    return loadQueue_.size();
}

size_t IOManager::pendingSaveCount() const {
    std::lock_guard lock(saveMutex_);
    return saveQueue_.size();
}

size_t IOManager::regionFileCount() const {
    std::lock_guard lock(regionMutex_);
    return regionFiles_.size();
}

void IOManager::setMaxOpenRegions(size_t count) {
    std::lock_guard lock(regionMutex_);
    maxOpenRegions_ = count;
    while (regionFiles_.size() > maxOpenRegions_) {
        evictOldestRegion();
    }
}

// ============================================================================
// Thread functions
// ============================================================================

void IOManager::loadThreadFunc() {
    while (running_) {
        LoadRequest request;

        // Get next request
        {
            std::unique_lock lock(loadMutex_);
            loadCond_.wait(lock, [this] {
                return !loadQueue_.empty() || !running_;
            });

            if (!running_ && loadQueue_.empty()) {
                break;
            }

            if (loadQueue_.empty()) {
                continue;
            }

            request = std::move(loadQueue_.front());
            loadQueue_.erase(loadQueue_.begin());
        }

        // Perform load (outside lock)
        std::unique_ptr<ChunkColumn> column;

        RegionPos regionPos = RegionPos::fromColumn(request.pos);
        RegionFile* region = getOrOpenRegion(regionPos);

        if (region) {
            column = region->loadColumn(request.pos);
        }

        // Invoke callback
        if (request.callback) {
            request.callback(request.pos, std::move(column));
        }
    }
}

void IOManager::saveThreadFunc() {
    while (running_) {
        SaveRequest request;

        // Get next request
        {
            std::unique_lock lock(saveMutex_);
            saveCond_.wait(lock, [this] {
                return !saveQueue_.empty() || !running_;
            });

            if (!running_ && saveQueue_.empty()) {
                break;
            }

            if (saveQueue_.empty()) {
                continue;
            }

            request = std::move(saveQueue_.front());
            saveQueue_.erase(saveQueue_.begin());
        }

        // Perform save (outside lock)
        bool success = false;

        RegionPos regionPos = RegionPos::fromColumn(request.pos);
        RegionFile* region = getOrOpenRegion(regionPos);

        if (region) {
            success = region->saveColumnRaw(request.pos, request.serializedData);
        }

        // Invoke callback
        if (request.callback) {
            request.callback(request.pos, success);
        }
    }
}

// ============================================================================
// Region file management
// ============================================================================

RegionFile* IOManager::getOrOpenRegion(RegionPos pos) {
    uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(pos.rx)) << 32) |
                   static_cast<uint64_t>(static_cast<uint32_t>(pos.rz));

    std::lock_guard lock(regionMutex_);

    auto it = regionFiles_.find(key);
    if (it != regionFiles_.end()) {
        return it->second.get();
    }

    // Evict if at capacity
    while (regionFiles_.size() >= maxOpenRegions_) {
        evictOldestRegion();
    }

    // Open new region file
    auto region = std::make_unique<RegionFile>(worldPath_, pos);
    auto* ptr = region.get();
    regionFiles_[key] = std::move(region);

    return ptr;
}

void IOManager::evictOldestRegion() {
    // Simple eviction: just remove the first one
    // A real implementation would track access time
    if (!regionFiles_.empty()) {
        regionFiles_.erase(regionFiles_.begin());
    }
}

}  // namespace finevox
