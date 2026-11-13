#pragma once

/**
 * @file scene_renderer.hpp
 * @brief Draws per-frame geometry that isn't chunk meshes
 *
 * Callers rebuild the opaque and translucent vertex lists each frame, then
 * WorldRenderer's pass callbacks draw them at the right point: opaque after
 * chunks (so entities depth-test against the world), translucent after fluids, and x-ray last,
 * where only what's behind something shows.
 */

#include <unordered_map>
#include <optional>
#include "finevox/core/entity_model.hpp"
#include <finevk/device/sampler.hpp>
#include <finevk/high/texture.hpp>
#include "finevox/render/scene_geometry.hpp"

#include <finevk/engine/camera.hpp>
#include <finevk/device/buffer.hpp>
#include <finevk/high/uniform_buffer.hpp>
#include <finevk/rendering/descriptors.hpp>
#include <finevk/rendering/pipeline.hpp>
#include <finevk/rendering/render_surface.hpp>

#include <memory>
#include <string>
#include <vector>

namespace finevox::render {

class SceneRenderer {
public:
    SceneRenderer(finevk::LogicalDevice* device, finevk::RenderSurface* surface);
    ~SceneRenderer();

    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void loadShaders(const std::string& vertPath, const std::string& fragPath);
    void initialize();

    void updateCamera(const finevk::CameraState& state, const glm::dvec3& position);

    /// The texture vertices with a uv sample (an atlas of entity skins). Without one, a plain
    /// white texel, so uv does nothing. Call before initialize, or between frames.
    void setTexture(finevk::Texture* texture);

    /// Pack every model's texture into one, set each model's place in it, and use it. Pixel
    /// art stays sharp: sampled nearest. Returns how many textures it placed.
    size_t buildEntityAtlas(finevk::CommandPool* pool, const std::string& textureRoot,
                            EntityModelRegistry& models, const std::vector<std::string>& more = {});

    /// Read a cloud picture: a pixel is cloud where it isn't mostly clear. False if unreadable.
    static bool loadCloudMask(const std::string& path, CloudMask& mask);

    /// Where buildEntityAtlas put one of `more`, or nothing
    [[nodiscard]] std::optional<glm::vec4> atlasRect(const std::string& texture) const;
    void setSky(const glm::vec3& sunDirection, const glm::vec3& fogColor, float fogStart,
                float fogEnd, float skyBrightness, float ambientLevel);

    /// Geometry for this frame. Cleared after drawing.
    [[nodiscard]] std::vector<SceneVertex>& opaque() { return opaque_; }
    [[nodiscard]] std::vector<SceneVertex>& translucent() { return translucent_; }
    /// Drawn only where something nearer already has been: a shape seen through what hides it
    [[nodiscard]] std::vector<SceneVertex>& xray() { return xray_; }

    void drawOpaque(finevk::CommandBuffer& cmd);
    void drawTranslucent(finevk::CommandBuffer& cmd);
    void drawXray(finevk::CommandBuffer& cmd);

private:
    /// Host-visible vertex storage, one buffer per frame in flight. Geometry changes
    /// every frame, and a device-local upload would stall the frame on a submit.
    struct DynamicMesh {
        std::vector<finevk::BufferPtr> buffers;
    };

    void draw(finevk::CommandBuffer& cmd, finevk::GraphicsPipeline& pipeline, DynamicMesh& target,
              std::vector<SceneVertex>& vertices);

    finevk::LogicalDevice* device_;
    finevk::RenderSurface* surface_;
    finevk::ShaderModulePtr vertexShader_;
    finevk::ShaderModulePtr fragmentShader_;
    finevk::DescriptorSetLayoutPtr descriptorLayout_;
    finevk::DescriptorPoolPtr descriptorPool_;
    finevk::PipelineLayoutPtr pipelineLayout_;
    finevk::GraphicsPipelinePtr opaquePipeline_;
    finevk::GraphicsPipelinePtr translucentPipeline_;
    finevk::GraphicsPipelinePtr xrayPipeline_;
    std::unique_ptr<finevk::UniformBuffer<finevk::CameraUniform>> cameraUniform_;
    std::vector<VkDescriptorSet> descriptorSets_;
    finevk::Texture* texture_ = nullptr;
    finevk::TextureRef whiteTexture_;
    finevk::TextureRef atlasTexture_;
    finevk::SamplerPtr sampler_;
    void writeTexture();
    std::unordered_map<std::string, glm::vec4> extraRects_;

    DynamicMesh opaqueMesh_;
    DynamicMesh translucentMesh_;
    DynamicMesh xrayMesh_;
    std::vector<SceneVertex> opaque_;
    std::vector<SceneVertex> translucent_;
    std::vector<SceneVertex> xray_;

    glm::vec3 sunDirection_{0.0f, 1.0f, 0.0f};
    glm::vec3 fogColor_{0.6f, 0.7f, 0.8f};
    float fogStart_ = 0.0f;
    float fogEnd_ = 0.0f;
    float skyBrightness_ = 1.0f;
    float ambientLevel_ = 0.3f;
    bool initialized_ = false;
};

}  // namespace finevox::render
