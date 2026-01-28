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

## 5.4 Column Lifecycle and Caching

**Implementation:** [column_manager.hpp](../include/finevox/column_manager.hpp), [column_manager.cpp](../src/column_manager.cpp)

ChunkColumns go through multiple lifecycle stages managed by the `ColumnManager`. The key insight is that **saving doesn't mean unloading** - a saved column can remain in memory for fast access.

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
│ SaveQueued  │◄──────────┼─── periodic save of active dirty columns
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
│   UnloadQueued (LRU)     │ ◄── clean columns wait here
│   (clean, retrievable)   │
└──────────┬───────────────┘
           │
           │ LRU eviction OR capacity reached
           ▼
┌─────────────┐
│   Evicted   │  (disk only, must reload)
└─────────────┘
```

### 5.4.2 Column State and ManagedColumn

The `ColumnManager` wraps each `ChunkColumn` in a `ManagedColumn` that tracks lifecycle state:

```cpp
namespace finevox {

// Lifecycle state for managed columns
enum class ColumnState {
    Active,        // In use, may be dirty or clean (refCount > 0 or recently used)
    SaveQueued,    // Dirty with refs == 0, waiting to be saved
    Saving,        // Currently being written to disk
    UnloadQueued,  // Clean, in LRU cache waiting for eviction
    Evicted        // Not in memory (conceptual, not tracked)
};

// Extended column info for lifecycle management
struct ManagedColumn {
    std::unique_ptr<ChunkColumn> column;
    ColumnState state = ColumnState::Active;
    bool dirty = false;
    std::chrono::steady_clock::time_point lastModified;
    std::chrono::steady_clock::time_point lastAccessed;
    int32_t refCount = 0;  // Number of active references

    explicit ManagedColumn(std::unique_ptr<ChunkColumn> col);

    void touch();       // Updates lastAccessed
    void markDirty();   // Sets dirty, updates lastModified
    void markClean();   // Called after successful save
};

}  // namespace finevox
```

### 5.4.3 ColumnManager

The `ColumnManager` coordinates all lifecycle state and tracks columns through their lifecycle:

```cpp
namespace finevox {

class ColumnManager {
public:
    explicit ColumnManager(size_t cacheCapacity = 64);

    // Retrieve a column - checks active, save queue, and unload cache
    // Returns nullptr if not in memory
    // Automatically moves retrieved columns to active state
    ManagedColumn* get(ColumnPos pos);

    // Add a new column to active management
    void add(std::unique_ptr<ChunkColumn> column);

    // Mark a column as dirty (needs saving)
    void markDirty(ColumnPos pos);

    // Reference counting for safe concurrent access
    void addRef(ColumnPos pos);   // Increment ref count
    void release(ColumnPos pos);  // Decrement ref count, may trigger save/unload

    // Check if a column is currently being saved (don't load from disk!)
    bool isSaving(ColumnPos pos) const;

    // Get columns queued for saving
    std::vector<ColumnPos> getSaveQueue();

    // Called when a save operation completes
    void onSaveComplete(ColumnPos pos);

    // Periodic maintenance (call from game loop)
    void tick();

    // Configuration
    void setPeriodicSaveInterval(std::chrono::seconds interval);
    void setCacheCapacity(size_t capacity);
    void setActivityTimeout(int64_t timeoutMs);

    // Callback to check if a column can be unloaded (for force-loading)
    using CanUnloadCallback = std::function<bool(ColumnPos)>;
    void setCanUnloadCallback(CanUnloadCallback callback);

    // IOManager integration for async persistence
    void bindIOManager(IOManager* io);
    void processSaveQueue();

    // Statistics
    size_t activeCount() const;
    size_t saveQueueSize() const;
    size_t cacheSize() const;

private:
    mutable std::shared_mutex mutex_;

    // Active columns (refCount > 0 or recently used)
    std::unordered_map<uint64_t, std::unique_ptr<ManagedColumn>> active_;

    // Save queue - dirty columns with refs == 0
    BlockingQueue<uint64_t> saveQueue_;

    // Currently being saved - CRITICAL: don't load from disk while here!
    std::unordered_set<uint64_t> currentlySaving_;

    // LRU cache for clean columns with refs == 0
    LRUCache<uint64_t, std::unique_ptr<ManagedColumn>> unloadCache_;

    // Periodic save tracking
    std::chrono::steady_clock::time_point lastPeriodicSave_;
    std::chrono::seconds periodicSaveInterval_{60};

    // Activity timeout for cross-chunk update protection (default 5 seconds)
    int64_t activityTimeoutMs_ = 5000;

    // IOManager for persistence (optional, not owned)
    IOManager* ioManager_ = nullptr;
};

}  // namespace finevox
```

### 5.4.4 Lifecycle Transitions

When a column's reference count drops to zero, it transitions based on dirty state:

```cpp
void ColumnManager::release(ColumnPos pos) {
    uint64_t key = pos.pack();
    std::unique_lock lock(mutex_);

    auto it = active_.find(key);
    if (it == active_.end()) return;

    --it->second->refCount;
    if (it->second->refCount > 0) return;

    // Refs dropped to zero - transition based on dirty state
    if (it->second->dirty) {
        transitionToSaveQueue(key);  // Dirty: must save first
    } else {
        transitionToUnloadCache(key);  // Clean: go to LRU cache
    }
}
```

Retrieving a column checks multiple locations:

```cpp
ManagedColumn* ColumnManager::get(ColumnPos pos) {
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
```

### 5.4.5 Save Thread Safety

The critical rule: **Never load from disk while a column is being saved.**

The `currentlySaving_` set ensures this:

```cpp
void ColumnManager::processSaveQueue() {
    // ... get columns from save queue ...

    for (auto& [pos, col] : toSave) {
        io->queueSave(pos, *col, [this](ColumnPos savedPos, bool success) {
            if (success) {
                onSaveComplete(savedPos);  // Mark clean, move to unload cache
            } else {
                // Save failed - re-queue for retry
                std::unique_lock lock(mutex_);
                currentlySaving_.erase(savedPos.pack());
                // Re-queue for next tick
            }
        });
    }
}

void ColumnManager::onSaveComplete(ColumnPos pos) {
    uint64_t key = pos.pack();
    std::unique_lock lock(mutex_);

    currentlySaving_.erase(key);

    auto it = active_.find(key);
    if (it == active_.end()) return;

    it->second->markClean();

    // If still no refs, move to unload cache
    if (it->second->refCount == 0) {
        transitionToUnloadCache(key);
    } else {
        it->second->state = ColumnState::Active;
    }
}
```

### 5.4.6 LRU Cache Eviction

Clean columns with zero references go to an LRU cache. When the cache reaches capacity, the least-recently-used columns are evicted:

```cpp
void ColumnManager::transitionToUnloadCache(uint64_t key) {
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
        // Force loader prevents unloading - keep in active state
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
```

### 5.4.7 Cross-Chunk Update Protection

When a block update event is delivered to a column (e.g., redstone propagation), the column's activity timer is touched. The `ColumnManager` respects this timer and won't unload the column until the timeout expires:

```cpp
// In ChunkColumn
void ChunkColumn::touchActivity() {
    lastActivityTime_ = std::chrono::steady_clock::now();
}

bool ChunkColumn::activityExpired(int64_t timeoutMs) const {
    return activityAgeMs() >= timeoutMs;
}

// In ColumnManager::transitionToUnloadCache
if (!column->activityExpired(activityTimeoutMs_)) {
    return;  // Don't unload - activity timer not expired
}
```

This prevents premature unload during redstone-like propagation across chunk boundaries

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
