# 5. World Management

[Back to Index](INDEX.md) | [Previous: Core Data Structures](04-core-data-structures.md)

---

## 5.1 Column-Based Loading

**Key Design Decision:** While blocks are stored in 16x16x16 subchunks for efficient memory and rendering, **loading and unloading operates on full-height 16x16 columns**. This solves the problem where lower Y levels wouldn't load if the player was high enough (the number of subchunks to load varied with height in the old design).

```cpp
namespace finevox {

// A ChunkColumn contains all subchunks for a 16x16 XZ area
// Height range is configurable (e.g., -64 to 319 like modern Minecraft)
class ChunkColumn {
public:
    static constexpr int MIN_Y = -64;   // Configurable
    static constexpr int MAX_Y = 319;   // Configurable
    static constexpr int HEIGHT = MAX_Y - MIN_Y + 1;
    static constexpr int SUBCHUNK_COUNT = (HEIGHT + 15) / 16;

    explicit ChunkColumn(ColumnPos pos);

    ColumnPos pos() const { return pos_; }

    // Access subchunks by Y level
    Chunk* getSubchunk(int subchunkY);              // Returns nullptr if not generated
    Chunk& getOrCreateSubchunk(int subchunkY);      // Creates empty if needed

    // Block access (delegates to appropriate subchunk)
    Block getBlock(BlockPos pos);
    void setBlock(BlockPos pos, uint16_t typeId, uint8_t rotation = 0);

    // Column-level state
    enum class State { Unloaded, Loading, Loaded, Generated };
    State state() const { return state_; }

    // Serialization saves all subchunks together
    void serialize(std::ostream& out) const;
    static std::unique_ptr<ChunkColumn> deserialize(std::istream& in);

private:
    ColumnPos pos_;
    State state_ = State::Unloaded;
    std::array<std::unique_ptr<Chunk>, SUBCHUNK_COUNT> subchunks_;
};

// Column position (just X, Z - no Y component)
struct ColumnPos {
    int32_t x, z;

    uint64_t pack() const;
    static ColumnPos unpack(uint64_t packed);

    static ColumnPos fromBlockPos(BlockPos pos) {
        return {pos.x >> 4, pos.z >> 4};
    }

    bool operator==(const ColumnPos& other) const;
};

}  // namespace finevox
```

---

## 5.2 World Class

```cpp
namespace finevox {

class World {
public:
    World();
    ~World();

    // Column access (primary interface for loading/unloading)
    ChunkColumn* getColumn(ColumnPos pos);
    ChunkColumn* getOrLoadColumn(ColumnPos pos);

    // Subchunk access (for rendering, derived from columns)
    Chunk* getChunk(ChunkPos pos);
    Chunk* getChunkContaining(BlockPos pos);

    // Block access (convenience, wraps column/chunk access)
    Block getBlock(BlockPos pos);                     // Returns air if not loaded
    void setBlock(BlockPos pos, uint16_t typeId, uint8_t rotation = 0);
    void breakBlock(BlockPos pos);                    // Triggers events
    void placeBlock(BlockPos pos, uint16_t typeId, uint8_t rotation = 0);

    // Column lifecycle (load/unload in full columns)
    void loadColumnsAround(BlockPos center, int radiusInChunks);
    void unloadDistantColumns(BlockPos center, int keepRadiusInChunks);
    void tickWorld();  // Process pending updates

    // Entities
    void addEntity(std::shared_ptr<Entity> entity);
    void removeEntity(Entity* entity);
    const std::vector<std::shared_ptr<Entity>>& entities() const;

    // Queues for deferred updates (see Batch Operations)
    void queueBlockUpdate(BlockPos pos);
    void queueRepaint(BlockPos pos);
    void processUpdateQueue();
    void processRepaintQueue();

    // Raycasting
    struct RaycastResult {
        bool hit;
        BlockPos blockPos;
        Face hitFace;
        float distance;
    };
    RaycastResult raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance);

    // Content generator callback (set by game, called for new columns)
    using ColumnGenerator = std::function<void(ChunkColumn&)>;
    void setColumnGenerator(ColumnGenerator gen);

    // Persistence
    void setSaveDirectory(const std::filesystem::path& dir);
    void save();
    void load();

private:
    // Thread-safe column storage (shared_ptr for safe concurrent access)
    mutable std::shared_mutex columnMutex_;
    std::unordered_map<uint64_t, std::shared_ptr<ChunkColumn>> columns_;

    // Update queues (thread-safe)
    std::mutex updateMutex_;
    std::unordered_set<BlockPos> blockUpdateQueue_;
    std::unordered_set<BlockPos> repaintQueue_;

    // Entities (shared_ptr for safe references across threads)
    std::vector<std::shared_ptr<Entity>> entities_;

    // Generation callback (provided by game)
    ColumnGenerator columnGenerator_;
    std::filesystem::path saveDir_;

    // Worker threads
    std::unique_ptr<ColumnLoadThread> loadThread_;
    std::unique_ptr<ColumnSaveThread> saveThread_;
};

}  // namespace finevox
```

---

## 5.3 Column Loading Thread

```cpp
namespace finevox {

class ColumnLoadThread {
public:
    explicit ColumnLoadThread(World& world);
    ~ColumnLoadThread();

    void start();
    void stop();

    // Request column load (non-blocking)
    void requestLoad(ColumnPos pos);

    // Check for completed loads (thread-safe)
    std::vector<std::shared_ptr<ChunkColumn>> collectLoaded();

private:
    void workerLoop();

    World& world_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    // Request queue (priority queue for distance-based loading)
    std::mutex requestMutex_;
    std::priority_queue<ColumnLoadRequest> loadRequests_;

    // Result queue
    std::mutex resultMutex_;
    std::vector<std::shared_ptr<ChunkColumn>> loadedColumns_;
};

}  // namespace finevox
```

---

## 5.4 SubChunk Lifecycle and Caching

SubChunks go through multiple lifecycle stages. The key insight is that **saving doesn't mean unloading** - a saved subchunk can remain in memory for fast access.

### 5.4.1 Lifecycle Diagram

```
                         ┌─────────────────────────────────────────┐
                         │                                         │
                         ▼                                         │
┌─────────────┐    ┌─────────────┐    ┌─────────────┐             │
│   Active    │───►│   Active    │───►│   Active    │─────────────┘
│   (dirty)   │    │   (clean)   │    │   (dirty)   │
└──────┬──────┘    └──────┬──────┘    └─────────────┘
       │                  │
       │ refs drop        │ refs drop (clean - skip save queue)
       ▼                  │
┌─────────────┐           │
│ Save Queue  │◄──────────┼─── periodic save of active dirty chunks
│  (dirty)    │           │
└──────┬──────┘           │
       │                  │
       │ being saved      │
       ▼                  │
┌─────────────┐           │
│   Saving    │ ◄─────────┼─── MUST NOT load from disk during this!
│ (in-flight) │           │
└──────┬──────┘           │
       │                  │
       │ save complete    │
       ▼                  ▼
┌──────────────────────────┐
│      Unload Queue        │ ◄── clean subchunks wait here
│  (clean, retrievable)    │
└──────────┬───────────────┘
           │
           │ time expires OR memory pressure
           ▼
┌─────────────┐
│   Evicted   │  (disk only, must reload)
└─────────────┘
```

### 5.4.2 SubChunk State Machine

```cpp
namespace finevox {

class SubChunk : public std::enable_shared_from_this<SubChunk> {
public:
    enum class State {
        Loading,       // Being loaded from disk or generated
        Active,        // In use (may be clean or dirty)
        InSaveQueue,   // Queued for background save
        Saving,        // Currently being written to disk
        InUnloadQueue, // Saved, waiting for eviction
        Evicted        // Only on disk (not really a state we track - it's gone)
    };

    // State management
    State state() const { return state_; }

    // Dirty tracking (orthogonal to state - active chunks can be clean or dirty)
    bool isDirty() const { return dirty_; }
    void markDirty();   // Sets dirty, updates lastModified
    void markClean();   // Called after successful save

    // Timestamps for lifecycle management
    std::chrono::steady_clock::time_point lastModified() const { return lastModified_; }
    std::chrono::steady_clock::time_point lastAccessed() const { return lastAccessed_; }
    void touch() { lastAccessed_ = std::chrono::steady_clock::now(); }

private:
    State state_ = State::Loading;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point lastModified_;
    std::chrono::steady_clock::time_point lastAccessed_;
};

}  // namespace finevox
```

### 5.4.3 SubChunk Manager

The SubChunkManager coordinates all the queues and tracks subchunks across their lifecycle:

```cpp
namespace finevox {

class SubChunkManager {
public:
    explicit SubChunkManager(World& world);

    // Retrieve a subchunk - checks active, save queue, unload queue
    // Returns nullptr only if truly not in memory
    std::shared_ptr<SubChunk> get(ChunkPos pos);

    // Called when a subchunk loses all external references
    void onRefsDropped(std::shared_ptr<SubChunk> subchunk);

    // Periodic maintenance (call from game loop)
    void tick();

    // Configuration
    void setPeriodicSaveInterval(std::chrono::seconds interval);
    void setUnloadQueueTimeout(std::chrono::seconds timeout);
    void setMaxMemoryUsage(size_t bytes);

private:
    World& world_;

    // Active subchunks (have external references)
    std::unordered_map<uint64_t, std::weak_ptr<SubChunk>> active_;

    // Save queue - dirty subchunks waiting to be saved
    // Ordered by lastModified (oldest first for fairness)
    std::deque<std::shared_ptr<SubChunk>> saveQueue_;

    // Currently being saved - CRITICAL: don't load from disk while here!
    std::unordered_set<uint64_t> currentlySaving_;

    // Unload queue - clean subchunks that can be retrieved or evicted
    // LRU: front = most recent, back = oldest
    std::list<std::shared_ptr<SubChunk>> unloadQueue_;
    std::unordered_map<uint64_t, std::list<std::shared_ptr<SubChunk>>::iterator> unloadIndex_;

    // Periodic save tracking for active dirty chunks
    std::chrono::steady_clock::time_point lastPeriodicSave_;
    std::chrono::seconds periodicSaveInterval_{60};  // Save active dirty chunks every 60s

    // Eviction parameters
    std::chrono::seconds unloadTimeout_{300};  // 5 minutes in unload queue
    size_t maxMemoryBytes_ = 512 * 1024 * 1024;  // 512 MB default

    void processPeriodicSaves();
    void processSaveQueue();
    void evictIfNeeded();
};

}  // namespace finevox
```

### 5.4.4 Lifecycle Transitions

```cpp
void SubChunkManager::onRefsDropped(std::shared_ptr<SubChunk> subchunk) {
    uint64_t key = subchunk->pos().pack();

    if (subchunk->isDirty()) {
        // Dirty: must save before we can unload
        subchunk->setState(SubChunk::State::InSaveQueue);
        saveQueue_.push_back(subchunk);
    } else {
        // Clean: can go directly to unload queue
        subchunk->setState(SubChunk::State::InUnloadQueue);
        unloadQueue_.push_front(subchunk);
        unloadIndex_[key] = unloadQueue_.begin();
    }
}

std::shared_ptr<SubChunk> SubChunkManager::get(ChunkPos pos) {
    uint64_t key = pos.pack();

    // 1. Check active (most common case)
    if (auto it = active_.find(key); it != active_.end()) {
        if (auto sp = it->second.lock()) {
            sp->touch();
            return sp;
        }
    }

    // 2. Check save queue (dirty, waiting to save)
    for (auto& sc : saveQueue_) {
        if (sc->pos().pack() == key) {
            sc->touch();
            sc->setState(SubChunk::State::Active);
            // Remove from save queue, add back to active
            // (will re-enter save queue when refs drop again)
            return sc;
        }
    }

    // 3. Check if currently being saved - retrieve but DON'T load from disk!
    if (currentlySaving_.contains(key)) {
        // The save thread has it - wait for it to finish and put in unload queue
        // For now, return nullptr and let caller retry
        // (Better: have save thread notify when done)
        return nullptr;
    }

    // 4. Check unload queue (clean, retrievable)
    if (auto it = unloadIndex_.find(key); it != unloadIndex_.end()) {
        auto sc = *it->second;
        unloadQueue_.erase(it->second);
        unloadIndex_.erase(it);
        sc->touch();
        sc->setState(SubChunk::State::Active);
        active_[key] = sc;
        return sc;
    }

    // 5. Not in memory - must load from disk
    return nullptr;
}

void SubChunkManager::processPeriodicSaves() {
    auto now = std::chrono::steady_clock::now();
    if (now - lastPeriodicSave_ < periodicSaveInterval_) return;
    lastPeriodicSave_ = now;

    // Find active dirty subchunks and queue them for save
    for (auto& [key, weak] : active_) {
        if (auto sc = weak.lock()) {
            if (sc->isDirty()) {
                // Queue for save, but keep active (don't change state)
                // The save will mark it clean, then it may get dirty again
                saveQueue_.push_back(sc);
            }
        }
    }
}
```

### 5.4.5 Save Thread Safety

The critical rule: **Never load from disk while a subchunk is being saved.**

```cpp
void SaveThread::saveSubChunk(std::shared_ptr<SubChunk> subchunk) {
    uint64_t key = subchunk->pos().pack();

    // Mark as currently saving BEFORE starting I/O
    {
        std::lock_guard lock(manager_.mutex_);
        manager_.currentlySaving_.insert(key);
    }

    // Perform the actual save (slow I/O)
    subchunk->serialize(getRegionFile(subchunk->pos()));

    // Mark clean and move to unload queue
    {
        std::lock_guard lock(manager_.mutex_);
        manager_.currentlySaving_.erase(key);
        subchunk->markClean();

        if (subchunk->state() == SubChunk::State::Saving) {
            // No one reclaimed it during save - move to unload queue
            subchunk->setState(SubChunk::State::InUnloadQueue);
            manager_.unloadQueue_.push_front(subchunk);
            manager_.unloadIndex_[key] = manager_.unloadQueue_.begin();
        }
        // else: someone reclaimed it, it's now Active again
    }
}
```

### 5.4.6 Memory Pressure Eviction

```cpp
void SubChunkManager::evictIfNeeded() {
    size_t currentMemory = estimateMemoryUsage();
    auto now = std::chrono::steady_clock::now();

    while (!unloadQueue_.empty()) {
        auto& oldest = unloadQueue_.back();
        bool shouldEvict = false;

        // Time-based eviction
        if (now - oldest->lastAccessed() > unloadTimeout_) {
            shouldEvict = true;
        }

        // Memory pressure eviction
        if (currentMemory > maxMemoryBytes_) {
            shouldEvict = true;
        }

        if (!shouldEvict) break;

        // Evict
        uint64_t key = oldest->pos().pack();
        unloadIndex_.erase(key);
        unloadQueue_.pop_back();
        // shared_ptr destructor handles cleanup
        currentMemory = estimateMemoryUsage();
    }
}
```

### 5.4.7 Safe Concurrent Access with shared_ptr

Using `shared_ptr` for subchunks ensures that:

1. **Render thread safety** - Render thread holds `shared_ptr` during rendering, subchunk can't be destroyed mid-frame
2. **Save thread safety** - Save thread holds `shared_ptr`, subchunk can't be destroyed during I/O
3. **Natural ref counting** - When all users are done, `onRefsDropped` is called via weak_ptr monitoring

```cpp
// In Block struct
struct Block {
    std::shared_ptr<SubChunk> subchunk;  // Keeps subchunk alive during block operations
    // ...
};

// Render thread
void ChunkRenderer::render(CommandBuffer& cmd) {
    auto subchunk = manager.get(pos);  // shared_ptr keeps it alive
    if (!subchunk) return;

    // Safe to use subchunk throughout this frame
    // Even if player moves away, subchunk won't be destroyed
}
```

---

## 5.5 Column Loading Priority

Columns should be loaded in priority order based on:
1. Distance from player (closer = higher priority)
2. Visibility (in frustum = higher priority)
3. Player movement direction (columns in front = higher priority)

```cpp
namespace finevox {

struct ColumnLoadRequest {
    ColumnPos pos;
    float priority;  // Lower = load first

    bool operator<(const ColumnLoadRequest& other) const {
        // Priority queue is max-heap, so invert comparison
        return priority > other.priority;
    }
};

float calculateColumnPriority(ColumnPos col, const Entity& player, const Camera& camera) {
    glm::vec3 columnCenter = glm::vec3(col.x * 16 + 8, player.position().y, col.z * 16 + 8);
    glm::vec3 playerPos = player.position();

    float distance = glm::length(glm::vec2(columnCenter.x - playerPos.x,
                                            columnCenter.z - playerPos.z));
    float priority = distance;

    // Bonus for columns in view frustum
    if (camera.frustum().containsPoint(columnCenter)) {
        priority *= 0.5f;
    }

    // Bonus for columns in movement direction
    glm::vec2 toColumn = glm::normalize(glm::vec2(columnCenter.x - playerPos.x,
                                                   columnCenter.z - playerPos.z));
    glm::vec2 moveDir = glm::normalize(glm::vec2(player.velocity().x, player.velocity().z));
    float dotMovement = glm::dot(toColumn, moveDir);
    if (dotMovement > 0.5f) {
        priority *= 0.7f;
    }

    return priority;
}

}  // namespace finevox
```

---

[Next: Rendering System](06-rendering.md)
