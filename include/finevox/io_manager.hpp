#pragma once

/**
 * @file io_manager.hpp
 * @brief Async save/load for world persistence
 *
 * Design: [11-persistence.md] §11.5 IOManager
 */

#include "finevox/position.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/region_file.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <filesystem>

namespace finevox {

// Forward declaration
class ColumnManager;

// ============================================================================
// IOManager - Async save/load for world persistence
// ============================================================================
//
// Manages background I/O operations for world persistence:
// - Save thread processes dirty columns from ColumnManager
// - Load thread handles async column loading requests
// - Maintains open region files (LRU-cached)
// - Coordinates with ColumnManager to prevent save/load races
//
// Thread safety: All public methods are thread-safe
//
class IOManager {
public:
    // Callback when a column load completes
    // Called on the load thread - implementation should be fast!
    using LoadCallback = std::function<void(ColumnPos pos, std::unique_ptr<ChunkColumn>)>;

    // Callback when a save completes
    // Called on the save thread - implementation should be fast!
    using SaveCallback = std::function<void(ColumnPos pos, bool success)>;

    // Create IOManager with given world directory
    explicit IOManager(const std::filesystem::path& worldPath);

    // Create IOManager using ResourceLocator for a registered world
    // Uses regionPath(worldName, dimension) to find the region directory
    // Returns nullptr if world is not registered with ResourceLocator
    static std::unique_ptr<IOManager> forWorld(const std::string& worldName,
                                                const std::string& dimension = "overworld");

    ~IOManager();

    // Non-copyable, non-movable (owns threads)
    IOManager(const IOManager&) = delete;
    IOManager& operator=(const IOManager&) = delete;

    // Start background I/O threads
    void start();

    // Stop I/O threads (waits for current operations to complete)
    void stop();

    // Request async load of a column
    // Callback will be invoked when load completes (or with nullptr if not found)
    void requestLoad(ColumnPos pos, LoadCallback callback);

    // Queue a column for saving
    // The column data is copied, so the original can continue to be used
    void queueSave(ColumnPos pos, const ChunkColumn& column);

    // Queue save with callback notification
    void queueSave(ColumnPos pos, const ChunkColumn& column, SaveCallback callback);

    // Flush all pending saves (blocks until complete)
    void flush();

    // Check if there are pending operations
    [[nodiscard]] bool hasPendingLoads() const;
    [[nodiscard]] bool hasPendingSaves() const;

    // Statistics
    [[nodiscard]] size_t pendingLoadCount() const;
    [[nodiscard]] size_t pendingSaveCount() const;
    [[nodiscard]] size_t regionFileCount() const;

    // Configuration
    void setMaxOpenRegions(size_t count);

private:
    std::filesystem::path worldPath_;

    // Region file cache
    mutable std::mutex regionMutex_;
    std::unordered_map<uint64_t, std::unique_ptr<RegionFile>> regionFiles_;
    size_t maxOpenRegions_ = 16;

    // Load queue
    struct LoadRequest {
        ColumnPos pos;
        LoadCallback callback;
    };
    mutable std::mutex loadMutex_;
    std::condition_variable loadCond_;
    std::vector<LoadRequest> loadQueue_;

    // Save queue
    struct SaveRequest {
        ColumnPos pos;
        std::vector<uint8_t> serializedData;  // Pre-serialized CBOR
        SaveCallback callback;
    };
    mutable std::mutex saveMutex_;
    std::condition_variable saveCond_;
    std::vector<SaveRequest> saveQueue_;

    // Threads
    std::thread loadThread_;
    std::thread saveThread_;
    std::atomic<bool> running_{false};

    // Internal methods
    void loadThreadFunc();
    void saveThreadFunc();

    RegionFile* getOrOpenRegion(RegionPos pos);
    void evictOldestRegion();
};

}  // namespace finevox
