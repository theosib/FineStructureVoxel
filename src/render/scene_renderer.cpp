#include "finevox/render/scene_renderer.hpp"

#include <finevk/device/command.hpp>
#include <finevk/device/logical_device.hpp>

#include <stb_image.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <tuple>
#include <unordered_map>

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
        .combinedImageSampler(1, VK_SHADER_STAGE_FRAGMENT_BIT)
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
    if (!sampler_) sampler_ = finevk::Sampler::createNearest(device_);
    writeTexture();

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
            .vertexAttribute(2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, color))
            .vertexAttribute(3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, uv));
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

    // A body behind a wall fails the usual test, and that's the part this draws. Where it's in
    // plain view its own depth is already there, which isn't greater, so nothing doubles up. Its
    // back faces are behind its front ones, so they'd pass: only the faces towards the eye are drawn.
    xrayPipeline_ = makeBuilder()
        .depthTest(true)
        .depthWrite(false)
        .depthCompareOp(VK_COMPARE_OP_GREATER)
        .alphaBlending()
        .cullBack()
        .frontFace(VK_FRONT_FACE_CLOCKWISE)
        .samples(surface_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();

    initialized_ = true;
}

void SceneRenderer::setTexture(finevk::Texture* texture) {
    texture_ = texture;
    writeTexture();
}

void SceneRenderer::writeTexture() {
    if (descriptorSets_.empty()) return;
    finevk::Texture* use = texture_;
    if (!use) {
        if (!whiteTexture_) {
            const uint8_t white[4] = {255, 255, 255, 255};
            whiteTexture_ = finevk::Texture::fromMemory(device_, white, 1, 1, surface_->commandPool(), false, false);
        }
        use = whiteTexture_.get();
    }
    finevk::DescriptorWriter writer(device_);
    for (VkDescriptorSet set : descriptorSets_) {
        writer.writeImage(set, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, use->view(), sampler_.get());
    }
    writer.update();
}

size_t SceneRenderer::buildEntityAtlas(finevk::CommandPool* pool, const std::string& textureRoot,
                                       EntityModelRegistry& models,
                                       const std::vector<std::string>& more) {
    struct Image {
        std::string model;
        int w = 0, h = 0;
        std::vector<uint8_t> pixels;
        int x = 0, y = 0;
    };
    std::vector<Image> images;
    std::unordered_map<std::string, size_t> byTexture;  // one copy of a shared skin
    std::vector<std::pair<std::string, std::string>> uses;
    auto add = [&](const std::string& texture) {
        if (byTexture.count(texture)) return;
        std::string path = textureRoot + "/" + texture + ".png";
        int w = 0, h = 0, n = 0;
        unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
        if (!data) {
            std::cerr << "[SceneRenderer] missing entity texture " << path << "\n";
            return;
        }
        Image img;
        img.model = texture;
        img.w = w;
        img.h = h;
        img.pixels.assign(data, data + static_cast<size_t>(w) * h * 4);
        stbi_image_free(data);
        byTexture[texture] = images.size();
        images.push_back(std::move(img));
    };
    std::vector<std::tuple<std::string, size_t, std::string>> partUses;
    models.forEach([&](const EntityModel& model) {
        for (size_t i = 0; i < model.mesh.parts.size(); ++i) {
            const auto& texture = model.mesh.parts[i].texture;
            if (texture.empty()) continue;
            partUses.emplace_back(model.name, i, texture);
            add(texture);
        }
        if (model.texture.empty()) return;
        uses.emplace_back(model.name, model.texture);
        add(model.texture);
    });
    for (const auto& texture : more) add(texture);
    if (images.empty()) return 0;

    // Shelves, tallest first, in an atlas as wide as the widest skin and at least 512
    std::vector<size_t> order(images.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return images[a].h > images[b].h; });
    int width = 512;
    for (const auto& img : images) width = std::max(width, img.w);
    int x = 0, y = 0, shelf = 0;
    for (size_t i : order) {
        auto& img = images[i];
        if (x + img.w > width) {
            x = 0;
            y += shelf;
            shelf = 0;
        }
        img.x = x;
        img.y = y;
        x += img.w;
        shelf = std::max(shelf, img.h);
    }
    int height = y + shelf;
    std::vector<uint8_t> atlas(static_cast<size_t>(width) * height * 4, 0);
    for (const auto& img : images) {
        for (int row = 0; row < img.h; ++row) {
            std::memcpy(&atlas[(static_cast<size_t>(img.y + row) * width + img.x) * 4],
                        &img.pixels[static_cast<size_t>(row) * img.w * 4], static_cast<size_t>(img.w) * 4);
        }
    }
    atlasTexture_ = finevk::Texture::fromMemory(device_, atlas.data(), static_cast<uint32_t>(width),
                                                static_cast<uint32_t>(height), pool, false, true);
    for (const auto& [model, texture] : uses) {
        auto it = byTexture.find(texture);
        if (it == byTexture.end()) continue;
        const auto& img = images[it->second];
        models.setAtlasRect(model, glm::vec4(float(img.x) / width, float(img.y) / height,
                                             float(img.x + img.w) / width, float(img.y + img.h) / height));
    }
    for (const auto& [model, part, texture] : partUses) {
        auto it = byTexture.find(texture);
        if (it == byTexture.end()) continue;
        const auto& img = images[it->second];
        models.setPartAtlasRect(model, part, glm::vec4(float(img.x) / width, float(img.y) / height,
                                                       float(img.x + img.w) / width, float(img.y + img.h) / height));
    }
    extraRects_.clear();
    for (const auto& texture : more) {
        auto it = byTexture.find(texture);
        if (it == byTexture.end()) continue;
        const auto& img = images[it->second];
        extraRects_[texture] = glm::vec4(float(img.x) / width, float(img.y) / height,
                                         float(img.x + img.w) / width, float(img.y + img.h) / height);
    }
    setTexture(atlasTexture_.get());
    return images.size();
}

bool SceneRenderer::loadCloudMask(const std::string& path, CloudMask& mask) {
    int w = 0, h = 0, n = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!data) return false;
    mask.width = w;
    mask.height = h;
    mask.solid.resize(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < mask.solid.size(); ++i) mask.solid[i] = data[i * 4 + 3] > 128 ? 1 : 0;
    stbi_image_free(data);
    return true;
}

std::optional<glm::vec4> SceneRenderer::atlasRect(const std::string& texture) const {
    auto it = extraRects_.find(texture);
    if (it == extraRects_.end()) return std::nullopt;
    return it->second;
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

    uint32_t frame = surface_->currentFrame();
    if (target.buffers.size() < surface_->framesInFlight()) {
        target.buffers.resize(surface_->framesInFlight());
    }

    VkDeviceSize bytes = vertices.size() * sizeof(SceneVertex);
    finevk::BufferPtr& buffer = target.buffers[frame];
    if (!buffer || buffer->size() < bytes) {
        if (buffer) surface_->deferDelete(std::move(buffer));
        buffer = finevk::Buffer::create(device_)
            .size(bytes * 2)
            .usage(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
            .memoryUsage(finevk::MemoryUsage::CpuToGpu)
            .build();
    }
    std::memcpy(buffer->map(), vertices.data(), bytes);

    pipeline.bind(cmd.handle());
    VkExtent2D extent = surface_->extent();
    cmd.setViewportAndScissor(extent.width, extent.height);
    pipelineLayout_->bindDescriptorSet(cmd.handle(), descriptorSets_[frame]);

    ScenePushConstants pc{};
    pc.fogStart = fogStart_;
    pc.fogColor = fogColor_;
    pc.fogEnd = fogEnd_;
    pc.sunDirection = sunDirection_;
    pc.skyBrightness = skyBrightness_;
    pc.ambientLevel = ambientLevel_;
    pipelineLayout_->pushConstants(cmd.handle(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pc);

    VkBuffer handle = buffer->handle();
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd.handle(), 0, 1, &handle, &offset);
    vkCmdDraw(cmd.handle(), static_cast<uint32_t>(vertices.size()), 1, 0, 0);
    vertices.clear();
}

void SceneRenderer::drawOpaque(finevk::CommandBuffer& cmd) {
    draw(cmd, *opaquePipeline_, opaqueMesh_, opaque_);
}

void SceneRenderer::drawTranslucent(finevk::CommandBuffer& cmd) {
    draw(cmd, *translucentPipeline_, translucentMesh_, translucent_);
}

void SceneRenderer::drawXray(finevk::CommandBuffer& cmd) {
    draw(cmd, *xrayPipeline_, xrayMesh_, xray_);
}

}  // namespace finevox::render
