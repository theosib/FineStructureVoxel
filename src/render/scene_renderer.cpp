#include "finevox/render/scene_renderer.hpp"

#include <finevk/device/command.hpp>
#include <finevk/device/logical_device.hpp>

#include <cstddef>

namespace finevox::render {

namespace {

// Same layout as the chunk and particle pipelines
struct ScenePushConstants {
    alignas(16) glm::vec3 chunkOffset;
    alignas(4)  float     fogStart;
    alignas(16) glm::vec3 fogColor;
    alignas(4)  float     fogEnd;
    alignas(16) glm::vec3 sunDirection;
    alignas(4)  float     skyBrightness;
    alignas(4)  float     ambientLevel;
    alignas(4)  float     pad0;
    alignas(4)  float     pad1;
    alignas(4)  float     pad2;
};

}  // namespace

SceneRenderer::SceneRenderer(finevk::LogicalDevice* device, finevk::RenderSurface* surface)
    : device_(device), surface_(surface) {}

SceneRenderer::~SceneRenderer() = default;

void SceneRenderer::loadShaders(const std::string& vertPath, const std::string& fragPath) {
    vertexShader_ = finevk::ShaderModule::fromFile(device_, vertPath);
    fragmentShader_ = finevk::ShaderModule::fromFile(device_, fragPath);
}

void SceneRenderer::initialize() {
    if (initialized_) return;

    cameraUniform_ = finevk::UniformBuffer<finevk::CameraUniform>::create(
        device_, surface_->framesInFlight());

    descriptorLayout_ = finevk::DescriptorSetLayout::create(device_)
        .uniformBuffer(0, VK_SHADER_STAGE_VERTEX_BIT)
        .build();

    descriptorPool_ = finevk::DescriptorPool::fromLayout(
        descriptorLayout_.get(), surface_->framesInFlight())
        .build();

    descriptorSets_ = descriptorPool_->allocate(descriptorLayout_.get(), surface_->framesInFlight());

    finevk::DescriptorWriter writer(device_);
    for (uint32_t i = 0; i < surface_->framesInFlight(); ++i) {
        writer.writeBuffer(descriptorSets_[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                           *cameraUniform_->buffer(i));
    }
    writer.update();

    pipelineLayout_ = finevk::PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorLayout_->handle())
        .addPushConstantRange(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                              sizeof(ScenePushConstants))
        .build();

    auto makeBuilder = [this]() {
        auto builder = finevk::GraphicsPipeline::create(device_, surface_->renderPass(),
                                                        pipelineLayout_.get());
        builder.vertexShader(vertexShader_.get())
            .fragmentShader(fragmentShader_.get())
            .vertexBinding(0, sizeof(SceneVertex), VK_VERTEX_INPUT_RATE_VERTEX)
            .vertexAttribute(0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, position))
            .vertexAttribute(1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, normal))
            .vertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, color));
        return builder;
    };

    opaquePipeline_ = makeBuilder()
        .enableDepth()
        .cullNone()
        .frontFace(VK_FRONT_FACE_CLOCKWISE)
        .samples(surface_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();

    translucentPipeline_ = makeBuilder()
        .enableDepth()
        .depthWrite(false)
        .alphaBlending()
        .cullNone()
        .frontFace(VK_FRONT_FACE_CLOCKWISE)
        .samples(surface_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();

    initialized_ = true;
}

void SceneRenderer::updateCamera(const finevk::CameraState& state, const glm::dvec3& position) {
    finevk::CameraUniform uniform{};
    uniform.view = state.viewRelative;
    uniform.projection = state.projection;
    uniform.viewProjection = state.projection * state.viewRelative;
    uniform.position = glm::vec3(position);
    cameraUniform_->update(surface_->currentFrame(), uniform);
}

void SceneRenderer::setSky(const glm::vec3& sunDirection, const glm::vec3& fogColor, float fogStart,
                           float fogEnd, float skyBrightness, float ambientLevel) {
    sunDirection_ = sunDirection;
    fogColor_ = fogColor;
    fogStart_ = fogStart;
    fogEnd_ = fogEnd;
    skyBrightness_ = skyBrightness;
    ambientLevel_ = ambientLevel;
}

void SceneRenderer::draw(finevk::CommandBuffer& cmd, finevk::GraphicsPipeline& pipeline,
                         DynamicMesh& target, std::vector<SceneVertex>& vertices) {
    if (!initialized_ || vertices.empty()) {
        vertices.clear();
        return;
    }

    target.indices.resize(vertices.size());
    for (uint32_t i = 0; i < vertices.size(); ++i) target.indices[i] = i;

    auto* commandPool = surface_->commandPool();
    if (!target.mesh) {
        VkVertexInputBindingDescription binding{0, sizeof(SceneVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::vector<VkVertexInputAttributeDescription> attributes = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, position))},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, normal))},
            {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, static_cast<uint32_t>(offsetof(SceneVertex, color))},
        };
        target.mesh = finevk::RawMesh::create(device_)
            .vertexLayout(binding, attributes)
            .vertices(vertices.data(), vertices.size())
            .indices(target.indices.data(), target.indices.size())
            .reserveCapacity(2.0f)
            .build(commandPool);
    } else {
        target.mesh->update(*commandPool, vertices.data(), vertices.size(), target.indices.data(),
                            target.indices.size());
    }

    pipeline.bind(cmd.handle());
    VkExtent2D extent = surface_->extent();
    cmd.setViewportAndScissor(extent.width, extent.height);
    pipelineLayout_->bindDescriptorSet(cmd.handle(), descriptorSets_[surface_->currentFrame()]);

    ScenePushConstants pc{};
    pc.fogStart = fogStart_;
    pc.fogColor = fogColor_;
    pc.fogEnd = fogEnd_;
    pc.sunDirection = sunDirection_;
    pc.skyBrightness = skyBrightness_;
    pc.ambientLevel = ambientLevel_;
    pipelineLayout_->pushConstants(cmd.handle(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pc);

    target.mesh->bind(cmd);
    target.mesh->draw(cmd);
    vertices.clear();
}

void SceneRenderer::drawOpaque(finevk::CommandBuffer& cmd) {
    draw(cmd, *opaquePipeline_, opaqueMesh_, opaque_);
}

void SceneRenderer::drawTranslucent(finevk::CommandBuffer& cmd) {
    draw(cmd, *translucentPipeline_, translucentMesh_, translucent_);
}

}  // namespace finevox::render
