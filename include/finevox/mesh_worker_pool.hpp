#pragma once

/**
 * @file mesh_worker_pool.hpp
 * @brief Parallel mesh generation worker threads
 *
 * Design: [06-rendering.md] §6.4 Async Workers
 *
 * Pure push-based architecture:
 * - Game logic / lighting thread pushes MeshRebuildRequest to input queue
 * - Worker threads pop requests, build meshes, push to upload queue
 * - Graphics thread pops from upload queue and uploads to GPU
 *
 * No caching or staleness detection - all rebuilds are event-driven.
 */

#include "finevox/mesh_rebuild_queue.hpp"
#include "finevox/mesh.hpp"
#include "finevox/position.hpp"
#include "finevox/queue.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <optional>
#include <memory>
#include <chrono>

namespace finevox {

// Forward declarations
class World;
class SubChunk;

// Data for a completed mesh ready for GPU upload
// Workers push these to the upload queue; graphics thread pops and uploads
struct MeshUploadData {
    ChunkPos pos;                           // Position of the subchunk
    MeshData mesh;                          // The generated mesh data
    uint64_t blockVersion = 0;              // Block version mesh was built from
    uint64_t lightVersion = 0;              // Light version mesh was built from
    LODLevel lodLevel = LODLevel::LOD0;     // LOD level of the mesh

    MeshUploadData() = default;
    MeshUploadData(ChunkPos p, MeshData m, uint64_t bv, uint64_t lv, LODLevel lod)
        : pos(p), mesh(std::move(m)), blockVersion(bv), lightVersion(lv), lodLevel(lod) {}
};

/// Queue type for mesh uploads (workers push, graphics thread pops)
/// Uses unified Queue<T> with CV, alarms, and WakeSignal support.
using MeshUploadQueue = Queue<MeshUploadData>;

// Mesh worker thread pool - pure push-based architecture
//
// Workers build meshes from requests and push completed meshes to upload queue.
// No caching - meshes are built on demand and discarded after GPU upload.
//
// Usage:
//   MeshWorkerPool pool(world, 4);  // 4 worker threads
//   pool.setInputQueue(&rebuildQueue);
//   pool.start();
//
//   // Game logic or lighting thread:
//   rebuildQueue.push(pos, MeshRebuildRequest::normal(blockVersion, lightVersion));
//
//   // Per-frame in graphics thread:
//   while (auto data = pool.tryPopUpload()) {
//       uploadToGPU(data->mesh);
//       // mesh data is discarded after upload
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
    // Upload Queue API (push-based mesh updates)
    // ========================================================================

    /// Get access to the upload queue (for WakeSignal attachment)
    /// Workers push completed meshes here; graphics thread pops them.
    MeshUploadQueue& uploadQueue() { return uploadQueue_; }

    /// Try to pop a completed mesh from the upload queue (non-blocking)
    /// Returns nullopt if queue is empty
    std::optional<MeshUploadData> tryPopUpload() { return uploadQueue_.tryPop(); }

    /// Get number of meshes waiting in the upload queue
    [[nodiscard]] size_t uploadQueueSize() const { return uploadQueue_.size(); }

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

    // Set light provider for smooth/flat lighting calculations
    // Thread-safe: can be called while workers are running
    void setLightProvider(BlockLightProvider provider) {
        std::lock_guard<std::mutex> lock(providerMutex_);
        lightProvider_ = std::move(provider);
    }

    // Configure smooth lighting (interpolated vertex lighting)
    void setSmoothLighting(bool enabled) { smoothLighting_ = enabled; }
    [[nodiscard]] bool smoothLighting() const { return smoothLighting_; }

    // Configure flat lighting (raw L1 ball, no smoothing)
    void setFlatLighting(bool enabled) { flatLighting_ = enabled; }
    [[nodiscard]] bool flatLighting() const { return flatLighting_; }

    // Set geometry provider for custom mesh blocks (slabs, stairs, etc.)
    // Thread-safe: can be called while workers are running
    void setGeometryProvider(BlockGeometryProvider provider) {
        std::lock_guard<std::mutex> lock(providerMutex_);
        geometryProvider_ = std::move(provider);
    }

    // Set face occludes provider for directional face culling (slabs, stairs, etc.)
    // Thread-safe: can be called while workers are running
    void setFaceOccludesProvider(BlockFaceOccludesProvider provider) {
        std::lock_guard<std::mutex> lock(providerMutex_);
        faceOccludesProvider_ = std::move(provider);
    }

    // ========================================================================
    // Alarm-based Wake Support
    // ========================================================================

    /// Set an alarm to wake workers at specified time
    /// @param wakeTime When to wake
    void setAlarm(std::chrono::steady_clock::time_point wakeTime);

    /// Clear any pending alarm
    void clearAlarm();

private:
    // Worker thread main loop
    void workerLoop();

    // Build mesh for a single subchunk and push to upload queue
    // @param pos Subchunk position
    // @param request Rebuild request with LOD info
    // @return true if mesh was built successfully
    bool buildMesh(ChunkPos pos, const MeshRebuildRequest& request);

    // Reference to world (for reading block data)
    World& world_;

    // Input queue (owned externally)
    MeshRebuildQueue* inputQueue_ = nullptr;

    // Upload queue - workers push completed meshes, graphics thread pops
    MeshUploadQueue uploadQueue_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // Texture provider (optional)
    BlockTextureProvider textureProvider_;
    mutable std::mutex providerMutex_;

    // Mesh settings
    bool greedyMeshing_ = true;
    LODMergeMode lodMergeMode_ = LODMergeMode::FullHeight;

    // Lighting settings
    BlockLightProvider lightProvider_;
    bool smoothLighting_ = false;
    bool flatLighting_ = false;

    // Custom geometry for non-cube blocks
    BlockGeometryProvider geometryProvider_;
    BlockFaceOccludesProvider faceOccludesProvider_;

    // Statistics
    Stats stats_;
};

}  // namespace finevox
