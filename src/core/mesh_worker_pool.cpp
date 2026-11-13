#include "finevox/core/mesh_worker_pool.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/fluid_mesh.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/block_type.hpp"
#include <algorithm>
#include <chrono>

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
// Worker Thread
// ============================================================================

void MeshWorkerPool::workerLoop() {
    while (running_) {
        // Wait for work (blocking) - wakes on push, alarm, or shutdown
        if (!inputQueue_->waitForWork()) {
            // Shutdown was signaled
            break;
        }

        // Try to pop a request
        if (auto request = inputQueue_->tryPop()) {
            auto [pos, rebuildRequest] = *request;
            buildMesh(pos, rebuildRequest);
        }
        // If tryPop returns empty (spurious wake or alarm), loop back
    }
}

bool MeshWorkerPool::buildMesh(ChunkPos pos, const MeshRebuildRequest& request) {
    // Get the actual LOD level to build from the request
    LODLevel buildLOD = request.lodRequest.buildLevel();

    MeshData meshData;
    MeshData fluidData;
    bool success = false;
    bool detailed = true;
    bool hasFarForms = false;

    try {
        // Get the subchunk from the world
        const SubChunk* subchunk = world_.getSubChunk(pos);

        if (!subchunk) {
            // Subchunk doesn't exist (might have been unloaded)
            // Push empty mesh to upload queue - graphics thread will detect via isEmpty()
            uploadQueue_.push(MeshUploadData(pos, MeshData{}, buildLOD));
            return true;
        }

        if (subchunk->isEmpty() && !subchunk->hasFluidLayer()) {
            // Empty subchunk with no fluid - push empty mesh to upload queue
            uploadQueue_.push(MeshUploadData(pos, MeshData{}, buildLOD));
            return true;
        }

        // Get texture, light, and geometry providers (copy under lock to avoid holding lock during build)
        BlockTextureProvider textureProvider;
        BlockLightProvider lightProvider;
        BlockGeometryProvider geometryProvider;
        BlockFaceOccludesProvider faceOccludesProvider;
        BlockConnectsProvider connectsProvider;
        BlockTintProvider tintProvider;
        BlockGeometryProvider farGeometryProvider;
        {
            std::lock_guard<std::mutex> lock(providerMutex_);
            textureProvider = textureProvider_;
            lightProvider = lightProvider_;
            geometryProvider = geometryProvider_;
            faceOccludesProvider = faceOccludesProvider_;
            connectsProvider = connectsProvider_;
            tintProvider = tintProvider_;
            farGeometryProvider = farGeometryProvider_;
        }

        // Use default texture provider if none set (returns (0,0,1,1) UVs)
        if (!textureProvider) {
            textureProvider = [](BlockTypeId, Face) {
                return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
            };
        }

        // Create mesh builder with all settings
        MeshBuilder builder;
        builder.setGreedyMeshing(greedyMeshing_);
        builder.setSmoothLighting(smoothLighting_);
        builder.setFlatLighting(flatLighting_);
        if (lightProvider) {
            builder.setLightProvider(lightProvider);
        }
        if (geometryProvider) {
            builder.setGeometryProvider(geometryProvider);
        }
        if (connectsProvider) {
            builder.setConnectsProvider(connectsProvider);
        }
        if (tintProvider) {
            builder.setTintProvider(tintProvider);
        }
        if (faceOccludesProvider) {
            builder.setFaceOccludesProvider(faceOccludesProvider);
        }
        if (farGeometryProvider) {
            builder.setFarGeometryProvider(farGeometryProvider);
            DetailRange range{detailNear_.load(std::memory_order_relaxed)};
            glm::dvec3 camera(detailCameraX_.load(std::memory_order_relaxed),
                              detailCameraY_.load(std::memory_order_relaxed),
                              detailCameraZ_.load(std::memory_order_relaxed));
            builder.setDetailed(range.detailed(DetailRange::distance(camera, pos)));
        }

        // Build the mesh at the requested LOD level
        if (buildLOD == LODLevel::LOD0) {
            // Full detail - use standard mesh building
            meshData = builder.buildSubChunkMesh(*subchunk, pos, world_, textureProvider);
        } else {
            // Lower detail - downsample and build LOD mesh
            LODSubChunk lodData(buildLOD);
            lodData.downsampleFrom(*subchunk, lodMergeMode_);

            // Use merge-mode-aware mesh building
            BlockOpaqueProvider alwaysTransparent = [](const BlockCoord&) { return false; };
            meshData = builder.buildLODMesh(lodData, pos, alwaysTransparent, textureProvider, lodMergeMode_);
        }
        // Build fluid mesh if subchunk has fluid data
        if (subchunk->hasFluidLayer()) {
            const FluidLayer* fl = subchunk->fluidLayer();
            if (fl && !fl->isEmpty()) {
                FluidMeshBuilder fluidBuilder;
                if (lightProvider) {
                    fluidBuilder.setLightProvider(lightProvider);
                }

                FluidNeighborProvider fluidNeighbor = [this](BlockCoord p) -> std::pair<FluidTypeId, uint8_t> {
                    return {world_.getFluid(p), world_.getFluidLevel(p)};
                };

                BlockSolidProvider solidProvider = [this](BlockCoord p) -> bool {
                    BlockTypeId bt = world_.getBlock(p);
                    if (bt.isAir()) return false;
                    const auto& blockType = BlockRegistry::global().getType(bt);
                    if (blockType.isOpaque() && !blockType.hasCustomMesh()) return true;
                    for (const auto& box : blockType.collisionShape().boxes()) {
                        if (box.min.x <= 0.001f && box.min.y <= 0.001f && box.min.z <= 0.001f &&
                            box.max.x >= 0.999f && box.max.y >= 0.999f && box.max.z >= 0.999f) {
                            return true;
                        }
                    }
                    return false;
                };

                fluidData = fluidBuilder.buildFluidMesh(*subchunk, pos, fluidNeighbor, solidProvider);
            }
        }

        success = true;
        detailed = builder.detailed();
        hasFarForms = builder.sawFarForms();

        // Update statistics
        stats_.meshesBuilt.fetch_add(1, std::memory_order_relaxed);
        stats_.totalVertices.fetch_add(meshData.vertexCount() + fluidData.vertexCount(),
                                       std::memory_order_relaxed);
        stats_.totalIndices.fetch_add(meshData.indexCount() + fluidData.indexCount(),
                                      std::memory_order_relaxed);

    } catch (const std::exception&) {
        stats_.meshesFailed.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    // Push to upload queue (move semantics - no copy)
    if (success) {
        MeshUploadData upload(pos, std::move(meshData), std::move(fluidData), buildLOD);
        upload.detailed = detailed;
        upload.hasFarForms = hasFarForms;
        uploadQueue_.push(std::move(upload));
    }

    return success;
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
