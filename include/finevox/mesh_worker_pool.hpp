#pragma once

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

namespace finevox {

// Forward declarations
class World;

// Result of mesh generation - ready for GPU upload
struct MeshBuildResult {
    ChunkPos pos;
    MeshData meshData;
    bool success = false;
    std::string errorMessage;

    MeshBuildResult() = default;
    explicit MeshBuildResult(ChunkPos p) : pos(p) {}
};

// Mesh worker thread pool
// Takes requests from MeshRebuildQueue, generates meshes, outputs to result queue
//
// Usage:
//   MeshWorkerPool pool(world, atlas, 4);  // 4 worker threads
//   pool.setInputQueue(&rebuildQueue);
//   pool.start();
//   // ... later ...
//   while (auto result = pool.popResult()) {
//       uploadToGPU(*result);
//   }
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

    // Pop a completed mesh result (thread-safe)
    // Returns nullopt if no results available
    std::optional<MeshBuildResult> popResult();

    // Pop up to maxCount results at once
    std::vector<MeshBuildResult> popResultBatch(size_t maxCount);

    // Get number of pending results
    [[nodiscard]] size_t resultQueueSize() const;

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

private:
    // Worker thread function
    void workerLoop();

    // Build mesh for a single subchunk
    MeshBuildResult buildMesh(ChunkPos pos);

    // Reference to world (for reading block data)
    World& world_;

    // Input queue (owned externally)
    MeshRebuildQueue* inputQueue_ = nullptr;

    // Result queue
    mutable std::mutex resultMutex_;
    std::queue<MeshBuildResult> results_;

    // Worker threads
    std::vector<std::thread> workers_;
    std::atomic<bool> running_{false};

    // Texture provider (optional)
    BlockTextureProvider textureProvider_;
    mutable std::mutex providerMutex_;

    // Mesh settings
    bool greedyMeshing_ = true;

    // Statistics
    Stats stats_;
};

}  // namespace finevox
