#pragma once

/**
 * @file mesh_worker_pool.hpp
 * @brief Parallel mesh generation worker threads
 *
 * Design: [06-rendering.md] §6.4 Async Workers
 */

#include "finevox/mesh_rebuild_queue.hpp"
#include "finevox/mesh.hpp"
#include "finevox/position.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <functional>
#include <optional>
#include <memory>
#include <chrono>

namespace finevox {

// Forward declarations
class World;
class SubChunk;

// Function type for checking if a chunk's mesh is stale
// Parameters: current block version and light version from SubChunk
// Returns: true if mesh needs rebuild
using StalenessChecker = std::function<bool(uint64_t currentBlockVersion, uint64_t currentLightVersion)>;

// Chunk entry for stale chunk scanning
// Contains what we need to check if a chunk is stale without owning it
struct ChunkTrackingEntry {
    ChunkPos pos;
    std::weak_ptr<SubChunk> subchunk;   // Weak reference to subchunk data
    StalenessChecker isStale;           // Callback to check if mesh is stale

    ChunkTrackingEntry() = default;
    ChunkTrackingEntry(ChunkPos p, std::weak_ptr<SubChunk> sc, StalenessChecker checker)
        : pos(p), subchunk(std::move(sc)), isStale(std::move(checker)) {}
};

// Cache entry for a subchunk's mesh data
// Workers write pending mesh data here; graphics thread reads and uploads
struct MeshCacheEntry {
    // Pending mesh data (written by worker, consumed by graphics thread)
    std::optional<MeshData> pendingMesh;
    uint64_t pendingVersion = 0;            // Block version the pending mesh was built from
    uint64_t pendingLightVersion = 0;       // Light version the pending mesh was built from
    LODLevel pendingLOD = LODLevel::LOD0;

    // Uploaded mesh state (tracked for staleness detection)
    uint64_t uploadedVersion = 0;           // Block version of currently uploaded mesh
    uint64_t uploadedLightVersion = 0;      // Light version of currently uploaded mesh
    LODLevel uploadedLOD = LODLevel::LOD0;

    // Weak reference to subchunk for version checking
    std::weak_ptr<SubChunk> subchunk;

    // Check if there's a pending mesh ready for upload
    [[nodiscard]] bool hasPendingMesh() const { return pendingMesh.has_value(); }

    // Check if uploaded mesh is stale (needs rebuild)
    // Returns true if the subchunk's current version differs from uploaded version
    [[nodiscard]] bool isStale() const {
        if (auto sc = subchunk.lock()) {
            return sc->blockVersion() != uploadedVersion ||
                   sc->lightVersion() != uploadedLightVersion;
        }
        return false;  // Subchunk unloaded, not stale (will be removed)
    }

    // Check if uploaded mesh satisfies an LOD request
    [[nodiscard]] bool satisfiesLOD(LODRequest request) const {
        return request.accepts(uploadedLOD);
    }
};

// Mesh worker thread pool with integrated mesh cache
//
// Workers build meshes and write them to an internal cache. The graphics thread
// queries the cache to get pending meshes for upload, and marks them as uploaded.
// Staleness detection uses version numbers to trigger background rebuilds.
//
// Usage:
//   MeshWorkerPool pool(world, 4);  // 4 worker threads
//   pool.setInputQueue(&rebuildQueue);
//   pool.start();
//
//   // Per-frame in graphics thread:
//   for (each visible chunk) {
//       auto [entry, needsRebuild] = pool.getMesh(pos, subchunk, lodRequest);
//       if (entry && entry->hasPendingMesh()) {
//           uploadToGPU(*entry->pendingMesh);
//           pool.markUploaded(pos);
//       }
//       // Use existing GPU mesh if available, even if stale
//   }
//
//   pool.stop();
//
class MeshWorkerPool {
public:
    // Create pool with specified number of worker threads
    // numThreads = 0 means use hardware concurrency - 1 (leave one for main thread)
    MeshWorkerPool(World& world, size_t numThreads = 0);
    ~MeshWorkerPool();

    // Non-copyable, non-movable (owns threads)
    MeshWorkerPool(const MeshWorkerPool&) = delete;
    MeshWorkerPool& operator=(const MeshWorkerPool&) = delete;
    MeshWorkerPool(MeshWorkerPool&&) = delete;
    MeshWorkerPool& operator=(MeshWorkerPool&&) = delete;

    // Set the input queue (required before start())
    void setInputQueue(MeshRebuildQueue* queue);

    // Set texture provider for UV lookups
    // If not set, meshes will have default UVs
    void setBlockTextureProvider(BlockTextureProvider provider);

    // Start/stop worker threads
    void start();
    void stop();
    [[nodiscard]] bool isRunning() const { return running_; }

    // ========================================================================
    // Mesh Cache API (for graphics thread)
    // ========================================================================

    /// Result from getMesh() - contains cache entry and whether rebuild was triggered
    struct GetMeshResult {
        MeshCacheEntry* entry = nullptr;  // Pointer to cache entry (null if not cached)
        bool rebuildTriggered = false;    // True if a rebuild request was queued
    };

    /// Get mesh for a subchunk, triggering rebuild if stale or LOD mismatch
    ///
    /// This is the main API for the graphics thread. Call once per visible chunk per frame.
    /// If the mesh is stale or doesn't satisfy the LOD request, a rebuild is queued.
    /// The returned entry may have a pending mesh ready for upload.
    ///
    /// @param pos Subchunk position
    /// @param subchunk Weak pointer to subchunk (for version checking)
    /// @param lodRequest Desired LOD level (may be flexible for hysteresis)
    /// @return Entry pointer (may be null if never built) and whether rebuild was triggered
    GetMeshResult getMesh(ChunkPos pos, std::weak_ptr<SubChunk> subchunk, LODRequest lodRequest);

    /// Mark a mesh as uploaded after GPU upload completes
    ///
    /// Clears the pending mesh and updates uploaded version/LOD tracking.
    /// Call after successfully uploading the pending mesh to GPU.
    ///
    /// @param pos Subchunk position
    void markUploaded(ChunkPos pos);

    /// Remove a mesh from the cache (when chunk goes out of view)
    /// @param pos Subchunk position
    void removeMesh(ChunkPos pos);

    /// Get number of cached meshes
    [[nodiscard]] size_t cacheSize() const;

    /// Get number of pending meshes (ready for upload)
    [[nodiscard]] size_t pendingMeshCount() const;

    // Get number of worker threads
    [[nodiscard]] size_t threadCount() const { return workers_.size(); }

    // Statistics
    struct Stats {
        std::atomic<uint64_t> meshesBuilt{0};
        std::atomic<uint64_t> meshesFailed{0};
        std::atomic<uint64_t> totalVertices{0};
        std::atomic<uint64_t> totalIndices{0};
    };
    [[nodiscard]] const Stats& stats() const { return stats_; }

    // Configure mesh builder settings
    void setGreedyMeshing(bool enabled) { greedyMeshing_ = enabled; }
    [[nodiscard]] bool greedyMeshing() const { return greedyMeshing_; }

    // Configure LOD merge mode (how LOD blocks are sized)
    void setLODMergeMode(LODMergeMode mode) { lodMergeMode_ = mode; }
    [[nodiscard]] LODMergeMode lodMergeMode() const { return lodMergeMode_; }

    // ========================================================================
    // Stale Chunk Tracking (for background updates)
    // ========================================================================

    /// Register a chunk for stale scanning
    /// Called by graphics thread when chunks become visible
    /// @param pos Chunk position
    /// @param subchunk Weak pointer to subchunk data
    /// @param isStale Callback that checks if mesh needs rebuild given current block version
    void trackChunk(ChunkPos pos, std::weak_ptr<SubChunk> subchunk, StalenessChecker isStale);

    /// Unregister a chunk from stale scanning
    /// Called when a chunk goes out of view or is unloaded
    void untrackChunk(ChunkPos pos);

    /// Clear all tracked chunks
    void clearTrackedChunks();

    /// Move a chunk to the front of the scan list (recently accessed)
    /// Called when an explicit rebuild request is made
    void touchChunk(ChunkPos pos);

    /// Enable/disable background stale chunk scanning
    void setBackgroundScanning(bool enabled) { backgroundScanning_ = enabled; }
    [[nodiscard]] bool backgroundScanning() const { return backgroundScanning_; }

    // ========================================================================
    // Alarm-based Wake Support
    // ========================================================================

    /// Set an alarm to wake workers for background scanning
    /// Called by graphics thread once per frame when no explicit work is queued
    /// @param wakeTime When to wake (typically half a frame before next render)
    void setAlarm(std::chrono::steady_clock::time_point wakeTime);

    /// Clear any pending alarm
    void clearAlarm();

private:
    // Worker thread function - new design with alarm support:
    // 1. Poll queue (tryPop) - if work, process and continue
    // 2. Scan for stale chunks - if found, process and continue
    // 3. Block (waitForWork) until push, alarm, or shutdown
    void workerLoop();

    // Build mesh for a single subchunk and write to cache
    // @param pos Subchunk position
    // @param request Rebuild request with LOD info
    // @return true if mesh was built successfully
    bool buildMeshToCache(ChunkPos pos, const MeshRebuildRequest& request);

    // Find the next stale chunk that needs a background rebuild
    // Uses the mesh cache for staleness detection
    // Returns nullopt if no stale chunks found
    std::optional<std::pair<ChunkPos, MeshRebuildRequest>> findStaleChunk();

    // Reference to world (for reading block data)
    World& world_;

    // Input queue (owned externally)
    MeshRebuildQueue* inputQueue_ = nullptr;

    // Mesh cache - stores pending and uploaded mesh state per subchunk
    mutable std::mutex cacheMutex_;
    std::unordered_map<ChunkPos, MeshCacheEntry> meshCache_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // Texture provider (optional)
    BlockTextureProvider textureProvider_;
    mutable std::mutex providerMutex_;

    // Mesh settings
    bool greedyMeshing_ = true;
    bool backgroundScanning_ = true;
    LODMergeMode lodMergeMode_ = LODMergeMode::FullHeight;

    // Tracked chunks for stale scanning
    // Vector allows O(1) iteration; map provides O(1) lookup for touch/remove
    mutable std::mutex trackedChunksMutex_;
    std::vector<ChunkTrackingEntry> trackedChunks_;
    std::unordered_map<ChunkPos, size_t> trackedChunkIndices_;  // pos -> index in vector

    // Statistics
    Stats stats_;
};

}  // namespace finevox
