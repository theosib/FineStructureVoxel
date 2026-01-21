#include "finevox/world_renderer.hpp"

#include <finevk/device/logical_device.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/sampler.hpp>

#include <algorithm>

namespace finevox {

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
    if (device_) {
        device_->waitIdle();
    }
}

void WorldRenderer::loadShaders(const std::string& vertPath, const std::string& fragPath) {
    vertexShader_ = finevk::ShaderModule::fromFile(device_, vertPath);
    fragmentShader_ = finevk::ShaderModule::fromFile(device_, fragPath);
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

    initialized_ = true;
}

void WorldRenderer::createPipeline() {
    // Create pipeline layout with push constants
    pipelineLayout_ = finevk::PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorLayout_->handle())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(ChunkPushConstants))
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
}

void WorldRenderer::updateMeshes(uint32_t maxUpdates) {
    if (!initialized_) return;

    uint32_t updates = 0;

    // Collect chunks that need rebuilding based on version mismatch or LOD change
    // This replaces the explicit dirty tracking with self-throttling version checks
    struct ChunkUpdateInfo {
        ChunkPos pos;
        float distance;
        LODRequest lodRequest;  // Uses 2x encoding for hysteresis
        bool needsRebuild;
    };
    std::vector<ChunkUpdateInfo> chunksToUpdate;

    // First pass: check existing views for version mismatches or LOD changes
    for (auto& [pos, view] : views_) {
        const SubChunk* subchunk = world_.getSubChunk(pos);
        if (!subchunk) continue;

        // Calculate distance in blocks (not chunk coordinates)
        float distBlocks = LODConfig::distanceToChunk(highPrecisionCameraPos_, pos);

        // Get LOD request based on distance
        // - Returns exact request when clearly in one LOD zone
        // - Returns flexible request (accepts 2 levels) in hysteresis zones
        LODRequest lodRequest = lodEnabled_
            ? lodConfig_.getRequestForDistance(distBlocks)
            : LODRequest::exact(LODLevel::LOD0);

        // Check if rebuild needed: version mismatch OR LOD doesn't satisfy request
        // The LODRequest::accepts() method handles the hysteresis logic:
        // - Exact requests only match one LOD level
        // - Flexible requests match either neighboring level
        uint64_t currentVersion = subchunk->blockVersion();
        if (view->needsRebuild(currentVersion, lodRequest)) {
            chunksToUpdate.push_back({pos, distBlocks, lodRequest, true});
        }
    }

    // Second pass: check explicitly marked dirty chunks (for new chunks not yet in views_)
    for (const auto& pos : dirtyChunks_) {
        if (views_.find(pos) != views_.end()) continue;  // Already checked above
        if (!isInViewDistance(pos)) continue;

        float distBlocks = LODConfig::distanceToChunk(highPrecisionCameraPos_, pos);
        LODRequest lodRequest = lodEnabled_
            ? lodConfig_.getRequestForDistance(distBlocks)
            : LODRequest::exact(LODLevel::LOD0);

        chunksToUpdate.push_back({pos, distBlocks, lodRequest, true});
    }
    dirtyChunks_.clear();  // Clear explicit dirty list after processing

    // Sort by distance to camera (nearest first) - prioritize close chunks
    std::sort(chunksToUpdate.begin(), chunksToUpdate.end(),
        [](const ChunkUpdateInfo& a, const ChunkUpdateInfo& b) {
            return a.distance < b.distance;
        });

    // Process chunks that need updating
    for (const auto& info : chunksToUpdate) {
        if (maxUpdates > 0 && updates >= maxUpdates) break;

        // Skip if too far
        if (!isInViewDistance(info.pos)) continue;

        // Get or create view
        SubChunkView* view = getOrCreateView(info.pos);
        if (!view) continue;

        // CRITICAL: Capture version BEFORE reading any block data (see mesh_worker_pool.cpp)
        // This gives us a "floor" version - if the chunk is modified during mesh build,
        // we'll have a stale version number which triggers a rebuild next frame.
        const SubChunk* subchunk = world_.getSubChunk(info.pos);
        uint64_t versionBeforeBuild = subchunk ? subchunk->blockVersion() : 0;

        // Build mesh at the LOD level specified by the request
        // For flexible requests, buildLevel() returns the finer (lower number) of the two acceptable levels
        LODLevel buildLOD = info.lodRequest.buildLevel();
        MeshData meshData = buildMeshFor(info.pos, buildLOD);

        // Upload to GPU
        if (view->canUpdateInPlace(meshData)) {
            view->update(*renderer_->commandPool(), meshData);
        } else {
            view->upload(*device_, *renderer_->commandPool(), meshData, config_.meshCapacityMultiplier);
        }

        // Record the version and LOD we built
        view->setLastBuiltVersion(versionBeforeBuild);
        view->setLastBuiltLOD(buildLOD);

        ++updates;
    }
}

void WorldRenderer::markDirty(ChunkPos pos) {
    // Avoid duplicates
    for (const auto& p : dirtyChunks_) {
        if (p == pos) return;
    }
    dirtyChunks_.push_back(pos);
}

void WorldRenderer::markColumnDirty(ColumnPos pos) {
    // Get the column and mark all its non-empty subchunks dirty
    ChunkColumn* column = world_.getColumn(pos);
    if (!column) return;

    // ChunkColumn uses sparse storage - iterate over existing subchunks only
    column->forEachSubChunk([this, &pos](int32_t chunkY, const SubChunk& subchunk) {
        if (!subchunk.isEmpty()) {
            markDirty(ChunkPos(pos.x, chunkY, pos.z));
        }
    });
}

void WorldRenderer::markAllDirty() {
    // Invalidate version on all existing views (forces rebuild on next updateMeshes)
    for (auto& [pos, view] : views_) {
        view->markDirty();  // Sets lastBuiltVersion to 0, guaranteeing version mismatch
    }

    // Also scan the world for all subchunks with data and add to dirty list
    // This is needed for initial population when views_ is empty
    auto subchunkPositions = world_.getAllSubChunkPositions();
    for (const auto& pos : subchunkPositions) {
        markDirty(pos);
    }
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

        // Frustum culling
        if (!isInFrustum(pos)) {
            ++lastCulledCount_;
            continue;
        }

        // View distance culling
        if (!isInViewDistance(pos)) {
            ++lastCulledCount_;
            continue;
        }

        // Calculate view-relative offset
        ChunkPushConstants pushConstants;
        pushConstants.chunkOffset = calculateViewRelativeOffset(pos);
        pushConstants.padding = 0.0f;

        // Push constants
        pipelineLayout_->pushConstants(cmd.handle(), VK_SHADER_STAGE_VERTEX_BIT, pushConstants);

        // Bind and draw
        view->bind(cmd);
        view->draw(cmd);

        ++lastRenderedCount_;
        lastRenderedVertices_ += view->vertexCount();
        lastRenderedTriangles_ += view->triangleCount();
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

MeshData WorldRenderer::buildMeshFor(ChunkPos pos, LODLevel lodLevel) {
    // Get the subchunk from the world
    const SubChunk* subchunk = world_.getSubChunk(pos);
    if (!subchunk) {
        return MeshData{};
    }

    // For LOD0, use normal mesh building
    if (lodLevel == LODLevel::LOD0) {
        return meshBuilder_.buildSubChunkMesh(*subchunk, pos, world_, textureProvider_);
    }

    // For higher LOD levels, generate downsampled mesh
    // Create temporary LOD subchunk, downsample, and build mesh
    LODSubChunk lodData(lodLevel);
    lodData.downsampleFrom(*subchunk);
    return meshBuilder_.buildLODMesh(lodData, pos, textureProvider_);
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
    dirtyChunks_.clear();
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

}  // namespace finevox
