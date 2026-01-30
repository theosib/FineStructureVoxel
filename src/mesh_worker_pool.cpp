#include "finevox/mesh_worker_pool.hpp"
#include "finevox/world.hpp"
#include "finevox/subchunk.hpp"
#include <algorithm>
#include <chrono>
#include <limits>

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

// ============================================================================
// Mesh Cache API
// ============================================================================

MeshWorkerPool::GetMeshResult MeshWorkerPool::getMesh(
    ChunkPos pos,
    std::weak_ptr<SubChunk> subchunk,
    LODRequest lodRequest)
{
    GetMeshResult result;

    std::lock_guard<std::mutex> lock(cacheMutex_);

    // Get or create cache entry
    auto [it, inserted] = meshCache_.try_emplace(pos);
    MeshCacheEntry& entry = it->second;

    // Update the subchunk reference
    entry.subchunk = subchunk;

    result.entry = &entry;

    // Check if we need to trigger a rebuild
    auto sc = subchunk.lock();
    if (!sc) {
        // Subchunk unloaded, no rebuild needed
        return result;
    }

    uint64_t currentBlockVersion = sc->blockVersion();
    uint64_t currentLightVersion = sc->lightVersion();

    // Need rebuild if:
    // 1. No pending mesh AND (version stale OR LOD not satisfied)
    // 2. Pending mesh exists but is also stale (rare race condition)
    bool blockVersionStale = (entry.uploadedVersion != currentBlockVersion);
    bool lightVersionStale = (entry.uploadedLightVersion != currentLightVersion);
    bool lodMismatch = !lodRequest.accepts(entry.uploadedLOD);
    bool pendingStale = entry.hasPendingMesh() &&
        (entry.pendingVersion != currentBlockVersion || entry.pendingLightVersion != currentLightVersion);

    if ((!entry.hasPendingMesh() && (blockVersionStale || lightVersionStale || lodMismatch)) || pendingStale) {
        // Queue a rebuild request
        if (inputQueue_) {
            MeshRebuildRequest request;
            request.lodRequest = lodRequest;
            request.targetVersion = currentBlockVersion;
            request.targetLightVersion = currentLightVersion;
            inputQueue_->push(pos, request);
            result.rebuildTriggered = true;
        }
    }

    return result;
}

void MeshWorkerPool::markUploaded(ChunkPos pos) {
    std::lock_guard<std::mutex> lock(cacheMutex_);

    auto it = meshCache_.find(pos);
    if (it == meshCache_.end()) {
        return;
    }

    MeshCacheEntry& entry = it->second;
    if (entry.hasPendingMesh()) {
        // Transfer pending state to uploaded state
        entry.uploadedVersion = entry.pendingVersion;
        entry.uploadedLightVersion = entry.pendingLightVersion;
        entry.uploadedLOD = entry.pendingLOD;
        entry.pendingMesh.reset();
    }
}

void MeshWorkerPool::removeMesh(ChunkPos pos) {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    meshCache_.erase(pos);
}

size_t MeshWorkerPool::cacheSize() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return meshCache_.size();
}

size_t MeshWorkerPool::pendingMeshCount() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    size_t count = 0;
    for (const auto& [pos, entry] : meshCache_) {
        if (entry.hasPendingMesh()) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// Worker Thread
// ============================================================================

void MeshWorkerPool::workerLoop() {
    while (running_) {
        // 1. Poll queue for explicit work (non-blocking)
        if (auto request = inputQueue_->tryPop()) {
            auto [pos, rebuildRequest] = *request;

            // Move the processed chunk to front of tracking list
            // (it was recently requested, so less likely to be stale soon)
            touchChunk(pos);

            // Build the mesh directly to cache
            buildMeshToCache(pos, rebuildRequest);
            continue;  // Back to top - check for more work
        }

        // 2. No explicit work - scan for stale chunks (background work)
        if (backgroundScanning_) {
            if (auto staleWork = findStaleChunk()) {
                auto [pos, rebuildRequest] = *staleWork;

                // Build the mesh directly to cache
                buildMeshToCache(pos, rebuildRequest);
                continue;  // Back to top - check for more work
            }
        }

        // 3. No work at all - block until something happens
        // waitForWork() blocks until: push, alarm fires, or shutdown
        // Does NOT pop - we'll poll on the next iteration
        if (!inputQueue_->waitForWork()) {
            // Shutdown was signaled
            break;
        }
        // Woke up - go back to top to poll
    }
}

bool MeshWorkerPool::buildMeshToCache(ChunkPos pos, const MeshRebuildRequest& request) {
    // Get the actual LOD level to build from the request
    // For flexible requests, buildLevel() returns the finer (lower number) of acceptable levels
    LODLevel buildLOD = request.lodRequest.buildLevel();

    MeshData meshData;
    uint64_t builtBlockVersion = 0;
    uint64_t builtLightVersion = 0;
    bool success = false;

    try {
        // Get the subchunk from the world
        const SubChunk* subchunk = world_.getSubChunk(pos);

        if (!subchunk) {
            // Subchunk doesn't exist (might have been unloaded)
            // Write empty mesh to cache
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = meshCache_.find(pos);
            if (it != meshCache_.end()) {
                it->second.pendingMesh = MeshData{};
                it->second.pendingVersion = 0;
                it->second.pendingLightVersion = 0;
                it->second.pendingLOD = buildLOD;
            }
            return true;
        }

        // CRITICAL: Capture versions BEFORE reading any block/light data.
        //
        // Memory ordering ensures correctness:
        // - Writer: version_.fetch_add(1, release) happens AFTER data is written
        // - Reader: version_.load(acquire) synchronizes with those writes
        //
        // By reading versions FIRST, we get a "floor" on what version we're building:
        // - If chunk is modified during our read, we may see some newer data but miss others
        // - The mesh will have version V but contain some data from V+1
        // - This is safe: the version mismatch will trigger a rebuild next frame
        // - If we read version AFTER, we'd claim version V+1 but have stale V data (unsafe!)
        builtBlockVersion = subchunk->blockVersion();
        builtLightVersion = subchunk->lightVersion();

        if (subchunk->isEmpty()) {
            // Empty subchunk - write empty mesh to cache
            std::lock_guard<std::mutex> lock(cacheMutex_);
            auto it = meshCache_.find(pos);
            if (it != meshCache_.end()) {
                it->second.pendingMesh = MeshData{};
                it->second.pendingVersion = builtBlockVersion;
                it->second.pendingLightVersion = builtLightVersion;
                it->second.pendingLOD = buildLOD;
            }
            return true;
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

        // Build the mesh at the requested LOD level
        if (buildLOD == LODLevel::LOD0) {
            // Full detail - use standard mesh building
            meshData = builder.buildSubChunkMesh(*subchunk, pos, world_, textureProvider);
        } else {
            // Lower detail - downsample and build LOD mesh
            LODSubChunk lodData(buildLOD);
            lodData.downsampleFrom(*subchunk, lodMergeMode_);

            // Use merge-mode-aware mesh building
            BlockOpaqueProvider alwaysTransparent = [](const BlockPos&) { return false; };
            meshData = builder.buildLODMesh(lodData, pos, alwaysTransparent, textureProvider, lodMergeMode_);
        }
        success = true;

        // Update statistics
        stats_.meshesBuilt.fetch_add(1, std::memory_order_relaxed);
        stats_.totalVertices.fetch_add(meshData.vertexCount(),
                                       std::memory_order_relaxed);
        stats_.totalIndices.fetch_add(meshData.indexCount(),
                                      std::memory_order_relaxed);

    } catch (const std::exception&) {
        stats_.meshesFailed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Write to cache
    if (success) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        auto it = meshCache_.find(pos);
        if (it != meshCache_.end()) {
            it->second.pendingMesh = std::move(meshData);
            it->second.pendingVersion = builtBlockVersion;
            it->second.pendingLightVersion = builtLightVersion;
            it->second.pendingLOD = buildLOD;
        }
    }

    return success;
}

// ============================================================================
// Stale Chunk Tracking
// ============================================================================

void MeshWorkerPool::trackChunk(ChunkPos pos, std::weak_ptr<SubChunk> subchunk, StalenessChecker isStale) {
    std::lock_guard<std::mutex> lock(trackedChunksMutex_);

    // Check if already tracked
    auto it = trackedChunkIndices_.find(pos);
    if (it != trackedChunkIndices_.end()) {
        // Update the existing entry
        trackedChunks_[it->second] = ChunkTrackingEntry(pos, std::move(subchunk), std::move(isStale));
        return;
    }

    // Add new entry at the end
    size_t index = trackedChunks_.size();
    trackedChunks_.emplace_back(pos, std::move(subchunk), std::move(isStale));
    trackedChunkIndices_[pos] = index;
}

void MeshWorkerPool::untrackChunk(ChunkPos pos) {
    std::lock_guard<std::mutex> lock(trackedChunksMutex_);

    auto it = trackedChunkIndices_.find(pos);
    if (it == trackedChunkIndices_.end()) {
        return;  // Not tracked
    }

    size_t index = it->second;
    size_t lastIndex = trackedChunks_.size() - 1;

    if (index != lastIndex) {
        // Swap with last element
        trackedChunks_[index] = std::move(trackedChunks_[lastIndex]);
        // Update the swapped element's index
        trackedChunkIndices_[trackedChunks_[index].pos] = index;
    }

    // Remove last element and index entry
    trackedChunks_.pop_back();
    trackedChunkIndices_.erase(it);
}

void MeshWorkerPool::clearTrackedChunks() {
    std::lock_guard<std::mutex> lock(trackedChunksMutex_);
    trackedChunks_.clear();
    trackedChunkIndices_.clear();
}

void MeshWorkerPool::touchChunk(ChunkPos pos) {
    std::lock_guard<std::mutex> lock(trackedChunksMutex_);

    auto it = trackedChunkIndices_.find(pos);
    if (it == trackedChunkIndices_.end()) {
        return;  // Not tracked
    }

    size_t currentIndex = it->second;
    if (currentIndex == 0) {
        return;  // Already at front
    }

    // Move to front by swapping with element at front
    ChunkTrackingEntry entry = std::move(trackedChunks_[currentIndex]);

    // Shift all elements before currentIndex one position back
    // This maintains relative order of other elements
    for (size_t i = currentIndex; i > 0; --i) {
        trackedChunks_[i] = std::move(trackedChunks_[i - 1]);
        trackedChunkIndices_[trackedChunks_[i].pos] = i;
    }

    // Place at front
    trackedChunks_[0] = std::move(entry);
    trackedChunkIndices_[pos] = 0;
}

std::optional<std::pair<ChunkPos, MeshRebuildRequest>> MeshWorkerPool::findStaleChunk() {
    // First, check the mesh cache for stale entries
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        for (auto& [pos, entry] : meshCache_) {
            // Skip if already has pending mesh
            if (entry.hasPendingMesh()) {
                continue;
            }

            // Check if stale
            if (entry.isStale()) {
                auto sc = entry.subchunk.lock();
                if (sc) {
                    uint64_t currentBlockVersion = sc->blockVersion();
                    uint64_t currentLightVersion = sc->lightVersion();
                    return std::make_pair(pos, MeshRebuildRequest::background(currentBlockVersion, currentLightVersion));
                }
            }
        }
    }

    // Fall back to tracked chunks (for chunks not yet in cache)
    {
        std::lock_guard<std::mutex> lock(trackedChunksMutex_);

        // Iterate from back to front (recently touched chunks are at front)
        // This prioritizes chunks that haven't been explicitly requested recently
        for (auto it = trackedChunks_.rbegin(); it != trackedChunks_.rend(); ++it) {
            // Try to lock the weak pointer
            auto subchunk = it->subchunk.lock();
            if (!subchunk) {
                continue;  // Chunk was unloaded
            }

            // Check if staleness checker exists
            if (!it->isStale) {
                continue;
            }

            uint64_t currentBlockVersion = subchunk->blockVersion();
            uint64_t currentLightVersion = subchunk->lightVersion();
            if (it->isStale(currentBlockVersion, currentLightVersion)) {
                // Found a stale chunk
                return std::make_pair(it->pos, MeshRebuildRequest::background(currentBlockVersion, currentLightVersion));
            }
        }
    }

    return std::nullopt;
}

// ============================================================================
// Alarm Support
// ============================================================================

void MeshWorkerPool::setAlarm(std::chrono::steady_clock::time_point wakeTime) {
    if (inputQueue_) {
        inputQueue_->setAlarm(wakeTime);
    }
}

void MeshWorkerPool::clearAlarm() {
    if (inputQueue_) {
        inputQueue_->clearAlarm();
    }
}

}  // namespace finevox
