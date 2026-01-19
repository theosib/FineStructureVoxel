#include "finevox/mesh_worker_pool.hpp"
#include "finevox/world.hpp"
#include "finevox/subchunk.hpp"
#include <algorithm>

namespace finevox {

MeshWorkerPool::MeshWorkerPool(World& world, size_t numThreads)
    : world_(world)
{
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }

    workers_.reserve(numThreads);
}

MeshWorkerPool::~MeshWorkerPool() {
    stop();
}

void MeshWorkerPool::setInputQueue(MeshRebuildQueue* queue) {
    inputQueue_ = queue;
}

void MeshWorkerPool::setBlockTextureProvider(BlockTextureProvider provider) {
    std::lock_guard<std::mutex> lock(providerMutex_);
    textureProvider_ = std::move(provider);
}

void MeshWorkerPool::start() {
    if (running_) {
        return;
    }

    if (!inputQueue_) {
        throw std::runtime_error("MeshWorkerPool: input queue not set");
    }

    running_ = true;

    // Reserve was done in constructor
    size_t numThreads = workers_.capacity();
    if (numThreads == 0) {
        numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    }

    for (size_t i = 0; i < numThreads; ++i) {
        workers_.emplace_back(&MeshWorkerPool::workerLoop, this);
    }
}

void MeshWorkerPool::stop() {
    if (!running_) {
        return;
    }

    // Signal the input queue to shut down - this wakes all waiting workers
    inputQueue_->shutdown();

    // Join all threads
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    workers_.clear();
    running_ = false;
}

std::optional<MeshBuildResult> MeshWorkerPool::popResult() {
    std::lock_guard<std::mutex> lock(resultMutex_);
    if (results_.empty()) {
        return std::nullopt;
    }

    MeshBuildResult result = std::move(results_.front());
    results_.pop();
    return result;
}

std::vector<MeshBuildResult> MeshWorkerPool::popResultBatch(size_t maxCount) {
    std::lock_guard<std::mutex> lock(resultMutex_);

    std::vector<MeshBuildResult> batch;
    batch.reserve(std::min(maxCount, results_.size()));

    while (batch.size() < maxCount && !results_.empty()) {
        batch.push_back(std::move(results_.front()));
        results_.pop();
    }

    return batch;
}

size_t MeshWorkerPool::resultQueueSize() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return results_.size();
}

void MeshWorkerPool::workerLoop() {
    while (true) {
        // Wait for work from the input queue (blocking)
        auto pos = inputQueue_->popWait();

        if (!pos) {
            // Queue was shut down
            break;
        }

        // Build the mesh
        MeshBuildResult result = buildMesh(*pos);

        // Add to result queue
        {
            std::lock_guard<std::mutex> lock(resultMutex_);
            results_.push(std::move(result));
        }
    }
}

MeshBuildResult MeshWorkerPool::buildMesh(ChunkPos pos) {
    MeshBuildResult result(pos);

    try {
        // Get the subchunk from the world
        const SubChunk* subchunk = world_.getSubChunk(pos);

        if (!subchunk) {
            // Subchunk doesn't exist (might have been unloaded)
            result.success = true;  // Not an error, just empty
            return result;
        }

        if (subchunk->isEmpty()) {
            // Empty subchunk - no mesh needed
            result.success = true;
            return result;
        }

        // Get texture provider (copy under lock to avoid holding lock during build)
        BlockTextureProvider textureProvider;
        {
            std::lock_guard<std::mutex> lock(providerMutex_);
            textureProvider = textureProvider_;
        }

        // Use default texture provider if none set (returns (0,0,1,1) UVs)
        if (!textureProvider) {
            textureProvider = [](BlockTypeId, Face) {
                return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            };
        }

        // Create mesh builder
        MeshBuilder builder;
        builder.setGreedyMeshing(greedyMeshing_);

        // Build the mesh using World overload
        result.meshData = builder.buildSubChunkMesh(*subchunk, pos, world_, textureProvider);
        result.success = true;

        // Update statistics
        stats_.meshesBuilt.fetch_add(1, std::memory_order_relaxed);
        stats_.totalVertices.fetch_add(result.meshData.vertexCount(),
                                       std::memory_order_relaxed);
        stats_.totalIndices.fetch_add(result.meshData.indexCount(),
                                      std::memory_order_relaxed);

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        stats_.meshesFailed.fetch_add(1, std::memory_order_relaxed);
    }

    return result;
}

}  // namespace finevox
