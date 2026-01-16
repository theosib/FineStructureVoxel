#include "finevox/subchunk_manager.hpp"

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
    uint64_t key = column->position().pack();

    std::unique_lock lock(mutex_);

    auto managed = std::make_unique<ManagedColumn>(std::move(column));
    managed->state = ColumnState::Active;
    active_[key] = std::move(managed);
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

}  // namespace finevox
