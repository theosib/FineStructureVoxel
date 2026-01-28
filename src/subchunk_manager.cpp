#include "finevox/subchunk_manager.hpp"
#include "finevox/io_manager.hpp"

namespace finevox {

SubChunkManager::SubChunkManager(size_t cacheCapacity)
    : unloadCache_(cacheCapacity)
    , lastPeriodicSave_(std::chrono::steady_clock::now()) {}

SubChunkManager::~SubChunkManager() = default;

ManagedColumn* SubChunkManager::get(ColumnPos pos) {
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    // 1. Check if currently being saved - can't retrieve during save
    if (currentlySaving_.contains(key)) {
        return nullptr;
    }

    // 2. Check active columns (most common case)
    if (auto it = active_.find(key); it != active_.end()) {
        it->second->touch();
        return it->second.get();
    }

    // 3. Check unload cache
    auto cached = unloadCache_.remove(key);
    if (cached) {
        // Move back to active
        auto& col = *cached;
        col->state = ColumnState::Active;
        col->touch();
        auto ptr = col.get();
        active_[key] = std::move(*cached);
        return ptr;
    }

    // Not in memory
    return nullptr;
}

void SubChunkManager::add(std::unique_ptr<ChunkColumn> column) {
    ColumnPos pos = column->position();
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    auto managed = std::make_unique<ManagedColumn>(std::move(column));
    managed->state = ColumnState::Active;
    active_[key] = std::move(managed);

    // Notify about newly available chunk
    if (chunkLoadCallback_) {
        chunkLoadCallback_(pos);
    }
}

void SubChunkManager::markDirty(ColumnPos pos) {
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    if (auto it = active_.find(key); it != active_.end()) {
        it->second->markDirty();
    }
}

void SubChunkManager::addRef(ColumnPos pos) {
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    if (auto it = active_.find(key); it != active_.end()) {
        ++it->second->refCount;
        it->second->touch();
    }
}

void SubChunkManager::release(ColumnPos pos) {
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    auto it = active_.find(key);
    if (it == active_.end()) {
        return;
    }

    --it->second->refCount;
    if (it->second->refCount > 0) {
        return;
    }

    // Refs dropped to zero - transition based on dirty state
    if (it->second->dirty) {
        transitionToSaveQueue(key);
    } else {
        transitionToUnloadCache(key);
    }
}

bool SubChunkManager::isSaving(ColumnPos pos) const {
    std::shared_lock lock(mutex_);
    return currentlySaving_.contains(pos.pack());
}

std::vector<ColumnPos> SubChunkManager::getSaveQueue() {
    std::unique_lock lock(mutex_);

    std::vector<ColumnPos> result;

    while (true) {
        auto key = saveQueue_.pop();
        if (!key) break;

        // Find the column in active
        auto it = active_.find(*key);
        if (it == active_.end()) {
            continue;  // Column was removed while in queue
        }

        // Mark as currently saving
        it->second->state = ColumnState::Saving;
        currentlySaving_.insert(*key);
        result.push_back(ColumnPos::unpack(*key));
    }

    return result;
}

void SubChunkManager::onSaveComplete(ColumnPos pos) {
    uint64_t key = pos.pack();

    std::unique_lock lock(mutex_);

    currentlySaving_.erase(key);

    auto it = active_.find(key);
    if (it == active_.end()) {
        return;
    }

    it->second->markClean();

    // If still no refs, move to unload cache
    if (it->second->refCount == 0) {
        transitionToUnloadCache(key);
    } else {
        it->second->state = ColumnState::Active;
    }
}

void SubChunkManager::tick() {
    auto now = std::chrono::steady_clock::now();

    std::unique_lock lock(mutex_);

    // Process periodic saves
    if (now - lastPeriodicSave_ >= periodicSaveInterval_) {
        lastPeriodicSave_ = now;

        // Queue dirty active columns for save
        for (auto& [key, col] : active_) {
            if (col->dirty && col->state == ColumnState::Active) {
                saveQueue_.push(key);
                col->state = ColumnState::SaveQueued;
            }
        }
    }
}

std::vector<ColumnPos> SubChunkManager::getAllDirty() {
    std::shared_lock lock(mutex_);

    std::vector<ColumnPos> result;
    for (const auto& [key, col] : active_) {
        if (col->dirty) {
            result.push_back(ColumnPos::unpack(key));
        }
    }
    return result;
}

void SubChunkManager::setPeriodicSaveInterval(std::chrono::seconds interval) {
    std::unique_lock lock(mutex_);
    periodicSaveInterval_ = interval;
}

void SubChunkManager::setCacheCapacity(size_t capacity) {
    std::unique_lock lock(mutex_);

    auto evicted = unloadCache_.setCapacity(capacity);

    if (evictionCallback_) {
        for (auto& [key, col] : evicted) {
            evictionCallback_(std::move(col->column));
        }
    }
}

size_t SubChunkManager::activeCount() const {
    std::shared_lock lock(mutex_);
    return active_.size();
}

size_t SubChunkManager::saveQueueSize() const {
    std::shared_lock lock(mutex_);
    return saveQueue_.size();
}

size_t SubChunkManager::cacheSize() const {
    std::shared_lock lock(mutex_);
    return unloadCache_.size();
}

void SubChunkManager::setEvictionCallback(EvictionCallback callback) {
    std::unique_lock lock(mutex_);
    evictionCallback_ = std::move(callback);
}

void SubChunkManager::setChunkLoadCallback(ChunkLoadCallback callback) {
    std::unique_lock lock(mutex_);
    chunkLoadCallback_ = std::move(callback);
}

void SubChunkManager::setActivityTimeout(int64_t timeoutMs) {
    std::unique_lock lock(mutex_);
    activityTimeoutMs_ = timeoutMs;
}

void SubChunkManager::setCanUnloadCallback(CanUnloadCallback callback) {
    std::unique_lock lock(mutex_);
    canUnloadCallback_ = std::move(callback);
}

void SubChunkManager::transitionToSaveQueue(uint64_t key) {
    // Assumes lock is held
    auto it = active_.find(key);
    if (it == active_.end()) return;

    it->second->state = ColumnState::SaveQueued;
    saveQueue_.push(key);
}

void SubChunkManager::transitionToUnloadCache(uint64_t key) {
    // Assumes lock is held
    auto it = active_.find(key);
    if (it == active_.end()) return;

    ColumnPos pos = ColumnPos::unpack(key);

    // Check if column has recent activity (cross-chunk update protection)
    if (it->second->column && !it->second->column->activityExpired(activityTimeoutMs_)) {
        // Activity timer not expired - keep in active state
        return;
    }

    // Check if external callback allows unloading (force loader check)
    if (canUnloadCallback_ && !canUnloadCallback_(pos)) {
        // Force loader or other reason prevents unloading - keep in active state
        return;
    }

    auto col = std::move(it->second);
    active_.erase(it);

    col->state = ColumnState::UnloadQueued;

    auto evicted = unloadCache_.put(key, std::move(col));
    if (evicted && evictionCallback_) {
        evictionCallback_(std::move(evicted->second->column));
    }
}

void SubChunkManager::transitionToActive(uint64_t key, std::unique_ptr<ManagedColumn> column) {
    // Assumes lock is held
    column->state = ColumnState::Active;
    active_[key] = std::move(column);
}

// ============================================================================
// IOManager integration
// ============================================================================

void SubChunkManager::bindIOManager(IOManager* io) {
    std::unique_lock lock(mutex_);
    ioManager_ = io;
}

void SubChunkManager::unbindIOManager() {
    std::unique_lock lock(mutex_);
    ioManager_ = nullptr;
}

bool SubChunkManager::requestLoad(ColumnPos pos, LoadCallback callback) {
    std::unique_lock lock(mutex_);

    // Can't load if currently saving this column
    if (currentlySaving_.contains(pos.pack())) {
        return false;
    }

    // Can't load without bound IOManager
    if (!ioManager_) {
        return false;
    }

    // Request load from IOManager
    ioManager_->requestLoad(pos, [this, callback](ColumnPos loadedPos, std::unique_ptr<ChunkColumn> col) {
        // Handle the loaded column
        if (col) {
            // Add to manager if not already present
            std::unique_lock lock(mutex_);
            uint64_t key = loadedPos.pack();

            // Check if column appeared while we were loading
            if (!active_.contains(key) && !unloadCache_.contains(key)) {
                auto managed = std::make_unique<ManagedColumn>(std::move(col));
                managed->state = ColumnState::Active;
                active_[key] = std::move(managed);

                // Notify about newly available chunk (under lock - keep callback fast!)
                if (chunkLoadCallback_) {
                    chunkLoadCallback_(loadedPos);
                }

                // Column was added - callback gets nullptr since we took ownership
                // Caller should use get() to access the managed column
                if (callback) {
                    lock.unlock();
                    callback(loadedPos, nullptr);
                }
                return;
            }
        }

        // Call user callback
        if (callback) {
            callback(loadedPos, std::move(col));
        }
    });

    return true;
}

void SubChunkManager::processSaveQueue() {
    IOManager* io = nullptr;
    std::vector<std::pair<ColumnPos, ChunkColumn*>> toSave;

    {
        std::unique_lock lock(mutex_);

        if (!ioManager_) {
            return;
        }
        io = ioManager_;

        // Process save queue
        while (true) {
            auto key = saveQueue_.pop();
            if (!key) break;

            auto it = active_.find(*key);
            if (it == active_.end()) {
                continue;  // Column was removed while in queue
            }

            // Mark as currently saving
            it->second->state = ColumnState::Saving;
            currentlySaving_.insert(*key);
            toSave.emplace_back(ColumnPos::unpack(*key), it->second->column.get());
        }
    }

    // Queue saves outside the lock
    for (auto& [pos, col] : toSave) {
        io->queueSave(pos, *col, [this](ColumnPos savedPos, bool success) {
            if (success) {
                onSaveComplete(savedPos);
            } else {
                // Save failed - remove from saving set but keep dirty
                std::unique_lock lock(mutex_);
                uint64_t key = savedPos.pack();
                currentlySaving_.erase(key);

                auto it = active_.find(key);
                if (it != active_.end()) {
                    it->second->state = ColumnState::Active;
                    // Re-queue for retry on next tick
                    saveQueue_.push(key);
                    it->second->state = ColumnState::SaveQueued;
                }
            }
        });
    }
}

}  // namespace finevox
