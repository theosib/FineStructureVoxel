#include "finevox/render/world_renderer.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"

#include <finevk/device/logical_device.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/sampler.hpp>

#include <algorithm>
#include <cmath>

namespace finevox::render {

WorldRenderer::WorldRenderer(
    finevk::LogicalDevice* device,
    finevk::SimpleRenderer* renderer,
    World& world,
    const WorldRendererConfig& config
)
    : config_(config)
    , device_(device)
    , renderer_(renderer)
    , world_(world)
{
    // Default texture provider: all faces use full UV range
    textureProvider_ = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };
}

WorldRenderer::~WorldRenderer() {
    // Stop worker pool before destroying GPU resources
    if (meshWorkerPool_) {
        meshWorkerPool_->uploadQueue().detach();
        meshWorkerPool_->stop();
    }

    if (device_) {
        device_->waitIdle();
    }
}

void WorldRenderer::loadShaders(const std::string& vertPath, const std::string& fragPath) {
    vertexShader_ = finevk::ShaderModule::fromFile(device_, vertPath);
    fragmentShader_ = finevk::ShaderModule::fromFile(device_, fragPath);
}

void WorldRenderer::loadFluidShader(const std::string& fragPath) {
    fluidFragmentShader_ = finevk::ShaderModule::fromFile(device_, fragPath);
}

void WorldRenderer::setBlockAtlas(finevk::Texture* atlas) {
    blockAtlas_ = atlas;
}

void WorldRenderer::setTextureProvider(BlockTextureProvider provider) {
    textureProvider_ = std::move(provider);
}

void WorldRenderer::initialize() {
    if (initialized_) return;

    // Create uniform buffers
    cameraUniform_ = finevk::UniformBuffer<finevk::CameraUniform>::create(
        device_, renderer_->framesInFlight());

    // Create descriptor layout
    descriptorLayout_ = finevk::DescriptorSetLayout::create(device_)
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
        .build();

    // Create descriptor pool
    descriptorPool_ = finevk::DescriptorPool::fromLayout(
        descriptorLayout_.get(), renderer_->framesInFlight())
        .build();

    // Allocate descriptor sets
    descriptorSets_ = descriptorPool_->allocate(
        descriptorLayout_.get(), renderer_->framesInFlight());

    // Write descriptor sets
    finevk::DescriptorWriter writer(device_);
    for (uint32_t i = 0; i < renderer_->framesInFlight(); ++i) {
        writer.writeBuffer(descriptorSets_[i], 0,
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                          *cameraUniform_->buffer(i));

        if (blockAtlas_) {
            writer.writeImage(descriptorSets_[i], 1,
                             VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                             blockAtlas_->view(), renderer_->defaultSampler());
        }
    }
    writer.update();

    // Create pipeline
    createPipeline();

    // Create the mesh rebuild queue and worker pool (always active)
    meshRebuildQueue_ = std::make_unique<MeshRebuildQueue>(mergeMeshRebuildRequest);

    meshWorkerPool_ = std::make_unique<MeshWorkerPool>(world_, 0);
    meshWorkerPool_->setInputQueue(meshRebuildQueue_.get());
    meshWorkerPool_->setBlockTextureProvider(textureProvider_);
    meshWorkerPool_->setGreedyMeshing(meshBuilder_.greedyMeshing());
    meshWorkerPool_->setLODMergeMode(lodMergeMode_);

    // Copy lighting settings to worker pool
    meshWorkerPool_->setSmoothLighting(meshBuilder_.smoothLighting());
    meshWorkerPool_->setFlatLighting(meshBuilder_.flatLighting());
    if (lightProvider_) {
        meshWorkerPool_->setLightProvider(lightProvider_);
    }

    // Copy geometry provider for custom block shapes
    if (geometryProvider_) {
        meshWorkerPool_->setGeometryProvider(geometryProvider_);
    }

    // Copy face occludes provider for directional face culling
    if (faceOccludesProvider_) {
        meshWorkerPool_->setFaceOccludesProvider(faceOccludesProvider_);
    }

    // Attach upload queue to wake signal for deadline-based waiting
    meshWorkerPool_->uploadQueue().attach(&wakeSignal_);

    // Start worker threads
    meshWorkerPool_->start();

    initialized_ = true;
}

void WorldRenderer::createPipeline() {
    // Create pipeline layout with push constants (shared between vertex and fragment)
    pipelineLayout_ = finevk::PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorLayout_->handle())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ChunkPushConstants))
        .build();

    // Get vertex layout from ChunkVertex
    auto binding = getChunkVertexBindingDescription();
    auto attributes = getChunkVertexAttributeDescriptions();

    // Create graphics pipeline using FineVK's builder pattern
    // Builder is move-only, so we move it into a local variable
    auto builder = finevk::GraphicsPipeline::create(
        device_, renderer_->renderPass(), pipelineLayout_.get());

    builder.vertexShader(vertexShader_.get())
           .fragmentShader(fragmentShader_.get())
           .vertexBinding(binding.binding, binding.stride, binding.inputRate);

    for (const auto& attr : attributes) {
        builder.vertexAttribute(attr.location, attr.binding, attr.format, attr.offset);
    }

    pipeline_ = builder
        .enableDepth()
        .cullBack()  // Use FineVK's convenience method
        .frontFace(VK_FRONT_FACE_CLOCKWISE)  // Mesh uses CCW, but Vulkan Y-flip reverses apparent winding
        .samples(renderer_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();

    // Create fluid pipeline (alpha-blended, depth-write OFF, no backface cull)
    // Only if fluid fragment shader was loaded
    if (fluidFragmentShader_) {
        auto fluidBuilder = finevk::GraphicsPipeline::create(
            device_, renderer_->renderPass(), pipelineLayout_.get());

        fluidBuilder.vertexShader(vertexShader_.get())
                    .fragmentShader(fluidFragmentShader_.get())
                    .vertexBinding(binding.binding, binding.stride, binding.inputRate);

        for (const auto& attr : attributes) {
            fluidBuilder.vertexAttribute(attr.location, attr.binding, attr.format, attr.offset);
        }

        fluidPipeline_ = fluidBuilder
            .enableDepth()
            .depthWrite(false)  // Test against opaque depth, don't write fluid depth
            .alphaBlending()    // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
            .cullNone()         // See fluid from both sides (underwater)
            .frontFace(VK_FRONT_FACE_CLOCKWISE)
            .samples(renderer_->msaaSamples())
            .dynamicViewportAndScissor()
            .build();
    }
}

void WorldRenderer::updateCamera(const finevk::CameraState& cameraState) {
    // Delegate to high-precision version using float position
    updateCamera(cameraState, glm::dvec3(cameraState.position));
}

void WorldRenderer::updateCamera(const finevk::CameraState& cameraState, const glm::dvec3& highPrecisionPos) {
    // Store the actual camera state for culling (uses real position and frustum)
    cameraState_ = cameraState;

    // Store high-precision position for view-relative calculations
    highPrecisionCameraPos_ = highPrecisionPos;

    // Cache cull camera position in chunk coordinates (double precision for accuracy)
    cameraChunkPos_ = glm::vec3(highPrecisionPos / 16.0);

    // Extract camera basis vectors from view matrix for debug offset
    glm::vec3 right = glm::vec3(cameraState_.view[0][0], cameraState_.view[1][0], cameraState_.view[2][0]);
    glm::vec3 up = glm::vec3(cameraState_.view[0][1], cameraState_.view[1][1], cameraState_.view[2][1]);
    glm::vec3 forward = -glm::vec3(cameraState_.view[0][2], cameraState_.view[1][2], cameraState_.view[2][2]);

    // Calculate render camera position (may be offset for debug visualization)
    glm::dvec3 renderCameraPosD = highPrecisionPos;
    if (config_.debugCameraOffset) {
        // Apply offset in camera's local space
        renderCameraPosD += glm::dvec3(right) * static_cast<double>(config_.debugOffset.x)
                         + glm::dvec3(up) * static_cast<double>(config_.debugOffset.y)
                         + glm::dvec3(forward) * static_cast<double>(config_.debugOffset.z);
    }

    // Store float version for GPU (view-relative rendering means this is only used for lighting)
    renderCameraPos_ = glm::vec3(renderCameraPosD);

    // Use FineVK's view-relative view matrix (camera at origin, rotation only)
    // This is the key to avoiding precision loss at large coordinates!
    // FineVK also pre-computes viewRelativeFrustumPlanes for us in CameraState

    // Update uniform buffer with view-relative matrices
    finevk::CameraUniform uniform{};
    uniform.view = cameraState_.viewRelative;
    uniform.projection = cameraState_.projection;
    uniform.viewProjection = cameraState_.projection * cameraState_.viewRelative;
    uniform.position = renderCameraPos_;  // Pass position for lighting/effects

    cameraUniform_->update(renderer_->currentFrame(), uniform);

    // Detect if camera is underwater (inside a fluid block)
    BlockCoord cameraBlock{
        static_cast<int32_t>(std::floor(highPrecisionPos.x)),
        static_cast<int32_t>(std::floor(highPrecisionPos.y)),
        static_cast<int32_t>(std::floor(highPrecisionPos.z))
    };
    FluidTypeId cameraFluid = world_.getFluid(cameraBlock);
    isUnderwater_ = !cameraFluid.isEmpty();
    if (isUnderwater_) {
        const FluidType* ft = FluidRegistry::global().getType(cameraFluid);
        if (ft) {
            underwaterFogColor_ = glm::vec3(ft->underwaterFogColor);
            underwaterFogEnd_ = 1.0f / std::max(ft->underwaterFogDensity, 0.001f);
            underwaterFogStart_ = 0.0f;
        }
    }
}

void WorldRenderer::updateMeshes(uint32_t maxUpdates) {
    if (!initialized_) return;

    // Pop completed meshes from the upload queue and send to GPU.
    // Rebuilds are triggered by game logic, lighting thread, and LOD checks
    // in render() — no polling or dirty list scanning needed here.
    uint32_t uploads = 0;
    while (maxUpdates == 0 || uploads < maxUpdates) {
        auto uploadData = meshWorkerPool_->tryPopUpload();
        if (!uploadData) break;

        const ChunkPos& pos = uploadData->pos;
        SubChunkView* view = getOrCreateView(pos);

        if (uploadData->mesh.isEmpty()) {
            view->release();
            view->setLastBuiltLOD(uploadData->lodLevel);
            ++uploads;
            continue;
        }

        if (view->canUpdateInPlace(uploadData->mesh)) {
            view->update(*renderer_->commandPool(), uploadData->mesh);
        } else {
            view->upload(*device_, *renderer_->commandPool(), uploadData->mesh, config_.meshCapacityMultiplier);
        }

        // Handle fluid mesh
        if (!uploadData->fluidMesh.isEmpty()) {
            if (view->canUpdateFluidInPlace(uploadData->fluidMesh)) {
                view->updateFluid(*renderer_->commandPool(), uploadData->fluidMesh);
            } else {
                view->uploadFluid(*device_, *renderer_->commandPool(),
                                  uploadData->fluidMesh, config_.meshCapacityMultiplier);
            }
        } else {
            view->releaseFluid();
        }

        view->setLastBuiltLOD(uploadData->lodLevel);
        ++uploads;
    }
}

void WorldRenderer::markDirty(ChunkPos pos) {
    if (meshRebuildQueue_) {
        meshRebuildQueue_->push(pos, MeshRebuildRequest::normal());
    }
}

void WorldRenderer::markColumnDirty(ColumnPos pos) {
    if (!meshRebuildQueue_) return;

    ChunkColumn* column = world_.getColumn(pos);
    if (!column) return;

    column->forEachSubChunk([this, &pos](int32_t chunkY, const SubChunk& subchunk) {
        if (!subchunk.isEmpty()) {
            meshRebuildQueue_->push(ChunkPos(pos.x, chunkY, pos.z), MeshRebuildRequest::normal());
        }
    });
}

void WorldRenderer::rebuildAllMeshes() {
    if (!meshRebuildQueue_) return;

    // Push all existing views
    for (auto& [pos, view] : views_) {
        meshRebuildQueue_->push(pos,
            MeshRebuildRequest::normal(LODRequest::exact(view->lastBuiltLOD())));
    }
    // Also queue any world subchunks not yet in views_
    for (const auto& pos : world_.getAllSubChunkPositions()) {
        if (views_.find(pos) == views_.end()) {
            meshRebuildQueue_->push(pos, MeshRebuildRequest::normal());
        }
    }
}

void WorldRenderer::markAllDirty() {
    rebuildAllMeshes();
}

void WorldRenderer::render(finevk::CommandBuffer& cmd) {
    if (!initialized_) return;

    lastRenderedCount_ = 0;
    lastCulledCount_ = 0;
    lastRenderedVertices_ = 0;
    lastRenderedTriangles_ = 0;

    // Bind pipeline
    pipeline_->bind(cmd.handle());

    // Set viewport and scissor
    VkExtent2D extent = renderer_->extent();
    cmd.setViewportAndScissor(extent.width, extent.height);

    // Bind descriptor set
    VkDescriptorSet currentSet = descriptorSets_[renderer_->currentFrame()];
    pipelineLayout_->bindDescriptorSet(cmd.handle(), currentSet);

    // Render each visible subchunk
    for (auto& [pos, view] : views_) {
        if (!view->hasGeometry()) continue;

        // Frustum culling (can be disabled for profiling)
        if (!config_.disableFrustumCulling && !isInFrustum(pos)) {
            ++lastCulledCount_;
            continue;
        }

        // View distance culling
        if (!isInViewDistance(pos)) {
            ++lastCulledCount_;
            continue;
        }

        // LOD check — piggyback on render iteration (essentially free)
        if (lodEnabled_ && meshRebuildQueue_) {
            float distBlocks = LODConfig::distanceToChunk(highPrecisionCameraPos_, pos);
            LODRequest lodRequest = lodConfig_.getRequestForDistance(distBlocks);
            if (!lodRequest.accepts(view->lastBuiltLOD())) {
                uint32_t priority = (distBlocks < 32.0f) ? 0 : 100;
                meshRebuildQueue_->push(pos, MeshRebuildRequest(priority, lodRequest));
            }
        }

        // Calculate view-relative offset and set fog/sky parameters
        ChunkPushConstants pushConstants{};
        pushConstants.chunkOffset = calculateViewRelativeOffset(pos);
        pushConstants.fogStart = config_.fog.enabled ? config_.fog.startDistance : 0.0f;
        pushConstants.fogColor = config_.fog.enabled ? config_.fog.color : glm::vec3(skyParams_.fogColor);
        pushConstants.fogEnd = config_.fog.enabled ? config_.fog.endDistance : 0.0f;
        pushConstants.sunDirection = skyParams_.sunDirection;
        pushConstants.skyBrightness = skyParams_.skyBrightness;
        pushConstants.ambientLevel = skyParams_.ambientLevel;

        // Push constants (shared between vertex and fragment stages)
        pipelineLayout_->pushConstants(cmd.handle(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pushConstants);

        // Bind and draw
        view->bind(cmd);
        view->draw(cmd);

        ++lastRenderedCount_;
        lastRenderedVertices_ += view->vertexCount();
        lastRenderedTriangles_ += view->triangleCount();
    }

    // ====================================================================
    // Fluid pass (alpha-blended, depth-write OFF, back-to-front)
    // ====================================================================
    if (fluidPipeline_) {
        fluidPipeline_->bind(cmd.handle());
        cmd.setViewportAndScissor(extent.width, extent.height);
        pipelineLayout_->bindDescriptorSet(cmd.handle(), currentSet);

        // Collect visible fluid subchunks and sort back-to-front
        struct FluidDrawEntry {
            ChunkPos pos;
            SubChunkView* view;
            float dist;
        };
        std::vector<FluidDrawEntry> fluidDrawList;

        for (auto& [pos, view] : views_) {
            if (!view->hasFluidGeometry()) continue;
            if (!config_.disableFrustumCulling && !isInFrustum(pos)) continue;
            if (!isInViewDistance(pos)) continue;

            float dist = glm::length(
                glm::vec3(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f) - cameraChunkPos_);
            fluidDrawList.push_back({pos, view.get(), dist});
        }

        std::sort(fluidDrawList.begin(), fluidDrawList.end(),
            [](const FluidDrawEntry& a, const FluidDrawEntry& b) {
                return a.dist > b.dist;
            });

        for (const auto& entry : fluidDrawList) {
            ChunkPushConstants pushConstants{};
            pushConstants.chunkOffset = calculateViewRelativeOffset(entry.pos);

            if (isUnderwater_) {
                pushConstants.fogStart = underwaterFogStart_;
                pushConstants.fogEnd = underwaterFogEnd_;
                pushConstants.fogColor = underwaterFogColor_;
            } else {
                pushConstants.fogStart = config_.fog.enabled ? config_.fog.startDistance : 0.0f;
                pushConstants.fogColor = config_.fog.enabled
                    ? config_.fog.color : glm::vec3(skyParams_.fogColor);
                pushConstants.fogEnd = config_.fog.enabled ? config_.fog.endDistance : 0.0f;
            }

            pushConstants.sunDirection = skyParams_.sunDirection;
            pushConstants.skyBrightness = skyParams_.skyBrightness;
            pushConstants.ambientLevel = skyParams_.ambientLevel;

            pipelineLayout_->pushConstants(
                cmd.handle(),
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                pushConstants);

            entry.view->bindFluid(cmd);
            entry.view->drawFluid(cmd);

            lastRenderedVertices_ += entry.view->fluidVertexCount();
            lastRenderedTriangles_ += entry.view->fluidIndexCount() / 3;
        }
    }
}

SubChunkView* WorldRenderer::getOrCreateView(ChunkPos pos) {
    auto it = views_.find(pos);
    if (it != views_.end()) {
        return it->second.get();
    }

    auto view = std::make_unique<SubChunkView>(pos);
    auto* ptr = view.get();
    views_[pos] = std::move(view);
    return ptr;
}

bool WorldRenderer::isInViewDistance(ChunkPos pos) const {
    float viewDistChunks = config_.viewDistance / 16.0f;
    glm::vec3 chunkCenter(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
    float dist = glm::length(chunkCenter - cameraChunkPos_);
    return dist <= viewDistChunks;
}

bool WorldRenderer::isInFrustum(ChunkPos pos) const {
    // Create AABB in view-relative coordinates for precision at large world coords
    // Use double precision for the subtraction, then convert to float for the AABB
    glm::dvec3 worldMinD(
        static_cast<double>(pos.x) * 16.0,
        static_cast<double>(pos.y) * 16.0,
        static_cast<double>(pos.z) * 16.0
    );
    glm::dvec3 worldMaxD = worldMinD + glm::dvec3(16.0);

    // Convert to view-relative coordinates (small values, safe for float32)
    glm::vec3 minViewRel = glm::vec3(worldMinD - highPrecisionCameraPos_);
    glm::vec3 maxViewRel = glm::vec3(worldMaxD - highPrecisionCameraPos_);

    finevk::AABB aabb = finevk::AABB::fromMinMax(minViewRel, maxViewRel);

    // Use FineVK's pre-computed view-relative frustum planes
    return aabb.intersectsFrustum(cameraState_.viewRelativeFrustumPlanes);
}

glm::vec3 WorldRenderer::calculateViewRelativeOffset(ChunkPos pos) const {
    // Calculate subchunk origin in world space using double precision
    // This is critical for large world coordinates!
    glm::dvec3 worldPosD(
        static_cast<double>(pos.x) * 16.0,
        static_cast<double>(pos.y) * 16.0,
        static_cast<double>(pos.z) * 16.0
    );

    // Subtract camera position using double precision
    // The result is small (view-relative), so converting to float is safe
    glm::dvec3 offsetD = worldPosD - highPrecisionCameraPos_;

    // Apply debug offset if enabled (already factored into highPrecisionCameraPos_ handling)
    // The offset should match what was used for renderCameraPos_
    if (config_.debugCameraOffset) {
        glm::vec3 right = glm::vec3(cameraState_.view[0][0], cameraState_.view[1][0], cameraState_.view[2][0]);
        glm::vec3 up = glm::vec3(cameraState_.view[0][1], cameraState_.view[1][1], cameraState_.view[2][1]);
        glm::vec3 forward = -glm::vec3(cameraState_.view[0][2], cameraState_.view[1][2], cameraState_.view[2][2]);

        offsetD -= glm::dvec3(right) * static_cast<double>(config_.debugOffset.x)
                 + glm::dvec3(up) * static_cast<double>(config_.debugOffset.y)
                 + glm::dvec3(forward) * static_cast<double>(config_.debugOffset.z);
    }

    return glm::vec3(offsetD);
}

void WorldRenderer::unloadChunk(ChunkPos pos) {
    views_.erase(pos);
}

uint32_t WorldRenderer::unloadDistantChunks() {
    // Use hysteresis: unload distance is viewDistance * unloadDistanceMultiplier
    // This prevents thrashing when camera is near the view distance boundary
    float unloadDistChunks = (config_.viewDistance * config_.unloadDistanceMultiplier) / 16.0f;

    uint32_t unloaded = 0;
    auto it = views_.begin();
    while (it != views_.end()) {
        if (unloaded >= config_.maxUnloadsPerFrame) break;

        // Calculate distance to camera in chunk coordinates
        glm::vec3 chunkCenter(it->first.x + 0.5f, it->first.y + 0.5f, it->first.z + 0.5f);
        float dist = glm::length(chunkCenter - cameraChunkPos_);

        if (dist > unloadDistChunks) {
            it = views_.erase(it);
            ++unloaded;
        } else {
            ++it;
        }
    }

    lastUnloadedCount_ = unloaded;
    return unloaded;
}

size_t WorldRenderer::gpuMemoryUsed() const {
    size_t total = 0;
    for (const auto& [pos, view] : views_) {
        total += view->gpuMemoryBytes();
        total += view->fluidGpuMemoryBytes();
    }
    return total;
}

uint32_t WorldRenderer::enforceMemoryBudget() {
    size_t currentUsage = gpuMemoryUsed();
    if (currentUsage <= config_.gpuMemoryBudget) {
        return 0;  // Within budget, nothing to do
    }

    // Collect chunks with their distances, sorted by distance (furthest first)
    struct ChunkDistance {
        ChunkPos pos;
        float distance;
        size_t memoryBytes;
    };
    std::vector<ChunkDistance> chunks;
    chunks.reserve(views_.size());

    float viewDistChunks = config_.viewDistance / 16.0f;

    for (const auto& [pos, view] : views_) {
        glm::vec3 chunkCenter(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
        float dist = glm::length(chunkCenter - cameraChunkPos_);

        // Only consider chunks beyond view distance for unloading
        // (won't unload visible chunks even if over budget)
        if (dist > viewDistChunks) {
            chunks.push_back({pos, dist, view->gpuMemoryBytes()});
        }
    }

    // Sort by distance descending (furthest first)
    std::sort(chunks.begin(), chunks.end(),
        [](const ChunkDistance& a, const ChunkDistance& b) {
            return a.distance > b.distance;
        });

    // Unload until under budget or no more chunks can be unloaded
    uint32_t unloaded = 0;
    for (const auto& chunk : chunks) {
        if (currentUsage <= config_.gpuMemoryBudget) break;
        if (unloaded >= config_.maxUnloadsPerFrame) break;

        views_.erase(chunk.pos);
        currentUsage -= chunk.memoryBytes;
        ++unloaded;
    }

    return unloaded;
}

void WorldRenderer::performCleanup() {
    // First unload distant chunks (with hysteresis)
    uint32_t distantUnloaded = unloadDistantChunks();

    // Then enforce memory budget if still over
    uint32_t budgetUnloaded = enforceMemoryBudget();

    // Update combined statistic
    lastUnloadedCount_ = distantUnloaded + budgetUnloaded;
}

void WorldRenderer::unloadAll() {
    views_.clear();
}

size_t WorldRenderer::totalVertexCount() const {
    size_t total = 0;
    for (const auto& [pos, view] : views_) {
        total += view->vertexCount();
    }
    return total;
}

size_t WorldRenderer::totalIndexCount() const {
    size_t total = 0;
    for (const auto& [pos, view] : views_) {
        total += view->indexCount();
    }
    return total;
}

size_t WorldRenderer::loadedMeshCount() const {
    return views_.size();
}

// ============================================================================
// LOD (Level of Detail)
// ============================================================================

void WorldRenderer::cycleLODDebugMode() {
    switch (lodDebugMode_) {
        case LODDebugMode::None:
            lodDebugMode_ = LODDebugMode::ColorByLOD;
            break;
        case LODDebugMode::ColorByLOD:
            lodDebugMode_ = LODDebugMode::WireframeByLOD;
            break;
        case LODDebugMode::WireframeByLOD:
            lodDebugMode_ = LODDebugMode::ShowBoundaries;
            break;
        case LODDebugMode::ShowBoundaries:
            lodDebugMode_ = LODDebugMode::None;
            break;
    }
}

WorldRenderer::LODStats WorldRenderer::getLODStats() const {
    LODStats stats;

    for (const auto& [pos, view] : views_) {
        int level = static_cast<int>(view->lastBuiltLOD());
        if (level >= 0 && level < static_cast<int>(LOD_LEVEL_COUNT)) {
            stats.chunksPerLevel[level]++;
            stats.totalChunks++;
        }
    }

    return stats;
}

// ============================================================================
// LOD Merge Mode
// ============================================================================

void WorldRenderer::setLODMergeMode(LODMergeMode mode) {
    if (lodMergeMode_ == mode) return;

    lodMergeMode_ = mode;

    // Propagate to worker pool
    if (meshWorkerPool_) {
        meshWorkerPool_->setLODMergeMode(mode);
    }

    // Rebuild all meshes with new mode
    rebuildAllMeshes();
}

LODMergeMode WorldRenderer::lodMergeMode() const {
    return lodMergeMode_;
}

// ============================================================================
// Lighting Settings
// ============================================================================

void WorldRenderer::setSmoothLighting(bool enabled) {
    meshBuilder_.setSmoothLighting(enabled);

    if (meshWorkerPool_) {
        meshWorkerPool_->setSmoothLighting(enabled);
    }
}

void WorldRenderer::setFlatLighting(bool enabled) {
    meshBuilder_.setFlatLighting(enabled);

    if (meshWorkerPool_) {
        meshWorkerPool_->setFlatLighting(enabled);
    }
}

void WorldRenderer::setLightProvider(BlockLightProvider provider) {
    lightProvider_ = provider;
    meshBuilder_.setLightProvider(provider);

    if (meshWorkerPool_) {
        meshWorkerPool_->setLightProvider(provider);
    }
}

void WorldRenderer::setGeometryProvider(BlockGeometryProvider provider) {
    geometryProvider_ = provider;
    meshBuilder_.setGeometryProvider(provider);

    if (meshWorkerPool_) {
        meshWorkerPool_->setGeometryProvider(provider);
    }
}

void WorldRenderer::setFaceOccludesProvider(BlockFaceOccludesProvider provider) {
    faceOccludesProvider_ = provider;
    meshBuilder_.setFaceOccludesProvider(provider);

    if (meshWorkerPool_) {
        meshWorkerPool_->setFaceOccludesProvider(provider);
    }
}

// ============================================================================
// Frame Timing and Deadline-Based Waiting
// ============================================================================

bool WorldRenderer::waitForMeshUploads(std::chrono::steady_clock::time_point deadline) {
    wakeSignal_.setDeadline(deadline);
    return wakeSignal_.wait();
}

bool WorldRenderer::waitForMeshUploads(std::chrono::milliseconds timeout) {
    return waitForMeshUploads(std::chrono::steady_clock::now() + timeout);
}

std::chrono::microseconds WorldRenderer::recordFrameStart() {
    auto now = std::chrono::steady_clock::now();

    if (lastFrameStart_.time_since_epoch().count() != 0) {
        // Calculate actual frame time
        auto actualFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(
            now - lastFrameStart_);

        // Clamp to reasonable range (10Hz to 240Hz) before storing
        auto clampedUs = std::clamp(actualFrameTime.count(), int64_t{4167}, int64_t{100000});

        // Store in circular buffer
        frameHistory_[frameHistoryIndex_] = std::chrono::microseconds(clampedUs);
        frameHistoryIndex_ = (frameHistoryIndex_ + 1) % kFrameHistorySize;
        if (frameHistoryCount_ < kFrameHistorySize) {
            ++frameHistoryCount_;
        }
    }

    lastFrameStart_ = now;
    return estimatedFramePeriod();
}

std::chrono::microseconds WorldRenderer::estimatedFramePeriod() const {
    // If no frames observed yet, use conservative default (60Hz)
    if (frameHistoryCount_ == 0) {
        return std::chrono::microseconds{16667};
    }

    // Use minimum of recent frames - conservative for deadline calculation
    // This handles variable frame rates by using the shortest observed period
    auto minPeriod = frameHistory_[0];
    for (size_t i = 1; i < frameHistoryCount_; ++i) {
        if (frameHistory_[i] < minPeriod) {
            minPeriod = frameHistory_[i];
        }
    }
    return minPeriod;
}

}  // namespace finevox::render
