#include "finevox/world_renderer.hpp"

#include <finevk/device/logical_device.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/sampler.hpp>

#include <algorithm>
#include <cmath>

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

    // Create graphics pipeline
    auto builder = finevk::GraphicsPipeline::create(
        device_, renderer_->renderPass(), pipelineLayout_.get())
        .vertexShader(vertexShader_.get())
        .fragmentShader(fragmentShader_.get())
        .vertexBinding(binding.binding, binding.stride, binding.inputRate);

    for (const auto& attr : attributes) {
        builder.vertexAttribute(attr.location, attr.binding, attr.format, attr.offset);
    }

    pipeline_ = builder
        .enableDepth()
        .cullMode(VK_CULL_MODE_BACK_BIT)
        .frontFace(VK_FRONT_FACE_COUNTER_CLOCKWISE)
        .samples(renderer_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();
}

void WorldRenderer::updateCamera(const finevk::CameraState& cameraState) {
    // Store the actual camera state for culling (uses real position and frustum)
    cameraState_ = cameraState;

    // Cache cull camera position in chunk coordinates
    cameraChunkPos_ = cameraState_.position / 16.0f;

    // Calculate render camera position (may be offset for debug visualization)
    if (config_.debugCameraOffset) {
        // Apply offset in camera's local space (offset is relative to view direction)
        // Transform the offset by the camera's rotation
        glm::mat3 rotation = glm::mat3(glm::inverse(cameraState_.view));
        renderCameraPos_ = cameraState_.position + rotation * config_.debugOffset;
    } else {
        renderCameraPos_ = cameraState_.position;
    }

    // Update uniform buffer with render camera position
    // This affects where the geometry appears to be rendered from
    finevk::CameraUniform uniform{};
    uniform.view = cameraState_.view;
    uniform.projection = cameraState_.projection;
    uniform.viewProjection = cameraState_.viewProjection;
    uniform.position = renderCameraPos_;  // Use render position for shader

    cameraUniform_->update(renderer_->currentFrame(), uniform);
}

void WorldRenderer::updateMeshes(uint32_t maxUpdates) {
    if (!initialized_) return;

    uint32_t updates = 0;

    // Sort dirty chunks by distance to camera for priority
    std::sort(dirtyChunks_.begin(), dirtyChunks_.end(),
        [this](const ChunkPos& a, const ChunkPos& b) {
            float distA = glm::length(glm::vec3(a.x, a.y, a.z) - cameraChunkPos_);
            float distB = glm::length(glm::vec3(b.x, b.y, b.z) - cameraChunkPos_);
            return distA < distB;
        });

    // Process dirty chunks
    auto it = dirtyChunks_.begin();
    while (it != dirtyChunks_.end()) {
        if (maxUpdates > 0 && updates >= maxUpdates) break;

        ChunkPos pos = *it;

        // Skip if too far
        if (!isInViewDistance(pos)) {
            it = dirtyChunks_.erase(it);
            continue;
        }

        // Get or create view
        SubChunkView* view = getOrCreateView(pos);
        if (!view) {
            it = dirtyChunks_.erase(it);
            continue;
        }

        // Build mesh
        MeshData meshData = buildMeshFor(pos);

        // Upload to GPU
        if (view->canUpdateInPlace(meshData)) {
            view->update(*renderer_->commandPool(), meshData);
        } else {
            view->upload(*device_, *renderer_->commandPool(), meshData, config_.meshCapacityMultiplier);
        }

        view->clearDirty();
        ++updates;
        it = dirtyChunks_.erase(it);
    }
}

void WorldRenderer::markDirty(ChunkPos pos) {
    // Avoid duplicates
    for (const auto& p : dirtyChunks_) {
        if (p == pos) return;
    }
    dirtyChunks_.push_back(pos);
}

void WorldRenderer::markAllDirty() {
    for (auto& [pos, view] : views_) {
        view->markDirty();
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
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd.handle(), 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd.handle(), 0, 1, &scissor);

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

MeshData WorldRenderer::buildMeshFor(ChunkPos pos) {
    // Get the subchunk from the world
    const SubChunk* subchunk = world_.getSubChunk(pos);
    if (!subchunk) {
        return MeshData{};
    }

    return meshBuilder_.buildSubChunkMesh(*subchunk, pos, world_, textureProvider_);
}

bool WorldRenderer::isInViewDistance(ChunkPos pos) const {
    float viewDistChunks = config_.viewDistance / 16.0f;
    glm::vec3 chunkCenter(pos.x + 0.5f, pos.y + 0.5f, pos.z + 0.5f);
    float dist = glm::length(chunkCenter - cameraChunkPos_);
    return dist <= viewDistChunks;
}

bool WorldRenderer::isInFrustum(ChunkPos pos) const {
    // Create AABB for the subchunk (in world space)
    glm::vec3 minWorld(pos.x * 16.0f, pos.y * 16.0f, pos.z * 16.0f);
    glm::vec3 maxWorld = minWorld + glm::vec3(16.0f);

    finevk::AABB aabb = finevk::AABB::fromMinMax(minWorld, maxWorld);
    return aabb.intersectsFrustum(cameraState_.frustumPlanes);
}

glm::vec3 WorldRenderer::calculateViewRelativeOffset(ChunkPos pos) const {
    // Calculate subchunk origin in world space
    glm::vec3 worldPos(pos.x * 16.0f, pos.y * 16.0f, pos.z * 16.0f);

    // Subtract render camera position for view-relative coordinates
    // This prevents floating-point precision issues at large distances
    // Note: Uses renderCameraPos_ which may be offset from cull position for debug
    return worldPos - renderCameraPos_;
}

void WorldRenderer::unloadChunk(ChunkPos pos) {
    views_.erase(pos);
}

void WorldRenderer::unloadDistantChunks() {
    auto it = views_.begin();
    while (it != views_.end()) {
        if (!isInViewDistance(it->first)) {
            it = views_.erase(it);
        } else {
            ++it;
        }
    }
}

void WorldRenderer::unloadAll() {
    views_.clear();
    dirtyChunks_.clear();
}

}  // namespace finevox
