#pragma once

/**
 * @file column_manager.hpp
 * @brief Column lifecycle state machine with LRU caching
 *
 * Design: [05-world-management.md] §5.4 Column Lifecycle
 */

#include "finevox/position.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/lru_cache.hpp"
#include "finevox/blocking_queue.hpp"
#include <memory>
#include <shared_mutex>
#include <chrono>
#include <unordered_set>
#include <functional>

namespace finevox {

// Forward declarations
class World;
class IOManager;

// Lifecycle state for managed columns
enum class ColumnState {
    Active,        // In use, may be dirty or clean
    SaveQueued,    // Dirty, waiting to be saved
    Saving,        // Currently being saved to disk
    UnloadQueued,  // Clean, in LRU cache waiting for eviction
    Evicted        // Not in memory (conceptual, we don't track these)
};

// Extended column info for lifecycle management
struct ManagedColumn {
    std::unique_ptr<ChunkColumn> column;
    ColumnState state = ColumnState::Active;
    bool dirty = false;
    std::chrono::steady_clock::time_point lastModified;
    std::chrono::steady_clock::time_point lastAccessed;
    int32_t refCount = 0;  // Number of active references

    explicit ManagedColumn(std::unique_ptr<ChunkColumn> col)
        : column(std::move(col))
        , lastModified(std::chrono::steady_clock::now())
        , lastAccessed(std::chrono::steady_clock::now()) {}

    void touch() {
        lastAccessed = std::chrono::steady_clock::now();
    }

    void markDirty() {
        dirty = true;
        lastModified = std::chrono::steady_clock::now();
    }

    void markClean() {
        dirty = false;
    }
};

/**
 * @brief Manages ChunkColumn lifecycle: loading, saving, and unloading
 *
 * Design: [05-world-management.md] §5.4 Column Lifecycle
 *
 * Coordinates column lifecycle:
 * - Tracks active columns and their reference counts
 * - Manages save queue for dirty columns
 * - Maintains LRU cache for clean columns awaiting eviction
 * - Prevents loading from disk while saving
 *
 * Thread-safety: Uses internal locking for thread-safe access
 */
class ColumnManager {
public:
    explicit ColumnManager(size_t cacheCapacity = 64);
    ~ColumnManager();

    // Get a column - checks active, save queue, and unload cache
    // Returns nullptr if not in memory
    // Automatically moves retrieved columns to active state
    ManagedColumn* get(ColumnPos pos);

    // Add a new column to active management
    // Takes ownership of the column
    void add(std::unique_ptr<ChunkColumn> column);

    // Mark a column as dirty (needs saving)
    void markDirty(ColumnPos pos);

    // Increment reference count (caller is using the column)
    void addRef(ColumnPos pos);

    // Decrement reference count (caller is done with column)
    // When refs drop to zero, column may be queued for save/unload
    void release(ColumnPos pos);

    // Check if a column is currently being saved (don't load from disk!)
    [[nodiscard]] bool isSaving(ColumnPos pos) const;

    // Get columns that are queued for saving
    // Caller should save these and call onSaveComplete when done
    std::vector<ColumnPos> getSaveQueue();

    // Called when a save operation completes
    void onSaveComplete(ColumnPos pos);

    // Periodic maintenance - call from game loop
    // Processes periodic saves of dirty active columns
    void tick();

    // Force save of all dirty columns (for shutdown)
    std::vector<ColumnPos> getAllDirty();

    // Configuration
    void setPeriodicSaveInterval(std::chrono::seconds interval);
    void setCacheCapacity(size_t capacity);

    // Set activity timeout for cross-chunk update protection (default 5000ms)
    // Columns with recent activity won't be unloaded until the timeout expires
    void setActivityTimeout(int64_t timeoutMs);

    // Set callback to check if a column can be unloaded
    // Used by World to prevent unloading force-loaded columns
    // Return true if unload is allowed, false to keep column loaded
    using CanUnloadCallback = std::function<bool(ColumnPos)>;
    void setCanUnloadCallback(CanUnloadCallback callback);

    // ========================================================================
    // IOManager integration
    // ========================================================================

    // Bind an IOManager for automatic persistence
    // When bound, ColumnManager will:
    // - Queue saves to IOManager when columns need saving
    // - Handle IOManager callbacks for save completion
    void bindIOManager(IOManager* io);

    // Unbind IOManager (for shutdown)
    void unbindIOManager();

    // Request async load of a column via bound IOManager
    // Callback is invoked when load completes (or with nullptr if not found)
    // Returns false if no IOManager is bound or column is currently being saved
    using LoadCallback = std::function<void(ColumnPos pos, std::unique_ptr<ChunkColumn>)>;
    bool requestLoad(ColumnPos pos, LoadCallback callback);

    // Process pending saves via bound IOManager
    // Call this from game loop or dedicated thread
    void processSaveQueue();

    // Statistics
    [[nodiscard]] size_t activeCount() const;
    [[nodiscard]] size_t saveQueueSize() const;
    [[nodiscard]] size_t cacheSize() const;

    // Callback for when a column is evicted from cache
    using EvictionCallback = std::function<void(std::unique_ptr<ChunkColumn>)>;
    void setEvictionCallback(EvictionCallback callback);

    // Callback for when a new column becomes available (added or loaded)
    // Called with the column position after it's added to the manager.
    // The callback is invoked under the manager's lock - keep it fast!
    // Use this to notify the graphics system about newly available chunks.
    using ChunkLoadCallback = std::function<void(ColumnPos pos)>;
    void setChunkLoadCallback(ChunkLoadCallback callback);

private:
    mutable std::shared_mutex mutex_;

    // Active columns (have refs > 0 or recently used)
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

    // Eviction callback
    EvictionCallback evictionCallback_;

    // Chunk load callback (notifies when new chunks become available)
    ChunkLoadCallback chunkLoadCallback_;

    // Can-unload callback (for force loader checks)
    CanUnloadCallback canUnloadCallback_;

    // Activity timeout for cross-chunk update protection (default 5 seconds)
    int64_t activityTimeoutMs_ = 5000;

    // IOManager for persistence (optional, not owned)
    IOManager* ioManager_ = nullptr;

    // Internal helper to move column between states
    void transitionToSaveQueue(uint64_t key);
    void transitionToUnloadCache(uint64_t key);
    void transitionToActive(uint64_t key, std::unique_ptr<ManagedColumn> column);
};

}  // namespace finevox
