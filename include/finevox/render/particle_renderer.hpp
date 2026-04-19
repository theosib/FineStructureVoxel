#pragma once

/**
 * @file particle_renderer.hpp
 * @brief Per-frame particle billboard renderer (Phase 26-B).
 *
 * Pulls a snapshot from ParticleSystem every frame, orients each particle's
 * quad on the CPU (three billboard modes: camera-facing, velocity-stretched,
 * axis-locked), sorts back-to-front, uploads a single RawMesh (one draw call)
 * and draws into the translucent pass.
 */

#include "finevox/core/particle.hpp"
#include "finevox/render/particle_atlas.hpp"

#include <finevk/engine/camera.hpp>
#include <finevk/rendering/render_surface.hpp>
#include <finevk/rendering/pipeline.hpp>
#include <finevk/rendering/descriptors.hpp>
#include <finevk/device/sampler.hpp>
#include <finevk/high/uniform_buffer.hpp>
#include <finevk/high/raw_mesh.hpp>

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace finevox {
class ParticleSystem;
}

namespace finevox::render {

// ============================================================================
// ParticleVertex — vertex format for particle billboards (see particle.vert)
// ============================================================================

struct ParticleVertex {
    glm::vec3 position;   // view-relative corner
    glm::vec2 texCoord;   // atlas UV
    glm::vec4 tint;       // RGBA (premultiply-free)
};

[[nodiscard]] VkVertexInputBindingDescription getParticleVertexBindingDescription();
[[nodiscard]] std::vector<VkVertexInputAttributeDescription>
    getParticleVertexAttributeDescriptions();

// ============================================================================
// CPU-side billboard builder (public for testing)
// ============================================================================

/// Append 6 vertices (two triangles forming a quad) for `p` into `out`,
/// oriented per its BillboardMode.  View-relative: subtracts `cameraPos`
/// from the world-space particle position.  `cameraRight`/`cameraUp`/
/// `cameraForward` must be orthonormal.
///
/// Exposed here so tests can validate vertex generation without a GPU.
void buildParticleQuad(
    const Particle& p,
    const glm::dvec3& cameraPos,
    const glm::vec3& cameraRight,
    const glm::vec3& cameraUp,
    const glm::vec3& cameraForward,
    std::vector<ParticleVertex>& out);

// ============================================================================
// ParticleRenderer
// ============================================================================

class ParticleRenderer {
public:
    /// Construct the renderer against any RenderSurface — SimpleRenderer
    /// (swap-chain) or OffscreenSurface (test / capture).  Both inherit
    /// RenderSurface; call sites remain source-compatible.
    ParticleRenderer(
        finevk::LogicalDevice* device,
        finevk::RenderSurface* surface,
        ParticleSystem& system);

    ~ParticleRenderer();

    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    /// Load particle.vert / particle.frag SPIR-V from the given paths.
    void loadShaders(const std::string& vertPath, const std::string& fragPath);

    /// Point at a ParticleAtlas.  The renderer samples this texture every frame
    /// and does not take ownership — the atlas must outlive the renderer.
    void setAtlas(const ParticleAtlas* atlas) { atlas_ = atlas; }

    /// Create descriptor sets, pipeline, and initial mesh buffers.
    void initialize();

    /// Update the per-frame camera uniform.  Called by WorldRenderer with the
    /// same CameraState it uses for the chunk pipeline.
    void updateCamera(const finevk::CameraState& cameraState,
                       const glm::dvec3& highPrecisionPos);

    /// Sky/fog parameters used by the fragment shader.  Pushed as constants.
    void setSkyParameters(const glm::vec3& sunDirection,
                           const glm::vec3& fogColor,
                           float fogStart, float fogEnd,
                           float skyBrightness, float ambientLevel);

    /// Build vertex data from the current ParticleSystem snapshot and draw.
    /// Must be called inside an active render pass, after the fluid pass.
    void render(finevk::CommandBuffer& cmd);

    // Stats --------------------------------------------------------------

    [[nodiscard]] uint32_t lastParticleCount() const { return lastParticleCount_; }
    [[nodiscard]] uint32_t lastVertexCount() const { return lastVertexCount_; }

private:
    void createPipeline();
    void ensureDescriptorSets();
    void uploadVertices(const std::vector<ParticleVertex>& verts);

    finevk::LogicalDevice* device_;
    finevk::RenderSurface* surface_;
    ParticleSystem& system_;
    const ParticleAtlas* atlas_ = nullptr;

    // Locally-owned sampler: we accept any RenderSurface, and only
    // SimpleRenderer exposes a shared default sampler.  Building one per
    // renderer keeps the class source-compatible with both surface types
    // without needing a setter plumbed in.
    finevk::SamplerPtr sampler_;

    finevk::ShaderModulePtr vertexShader_;
    finevk::ShaderModulePtr fragmentShader_;

    finevk::DescriptorSetLayoutPtr descriptorLayout_;
    finevk::DescriptorPoolPtr descriptorPool_;
    finevk::PipelineLayoutPtr pipelineLayout_;
    finevk::GraphicsPipelinePtr pipeline_;

    std::unique_ptr<finevk::UniformBuffer<finevk::CameraUniform>> cameraUniform_;
    std::vector<VkDescriptorSet> descriptorSets_;
    bool descriptorsWritten_ = false;

    // Per-frame dynamic mesh.  RawMesh supports in-place update when the new
    // vertex count stays within reserved capacity.
    finevk::RawMeshPtr mesh_;
    size_t meshCapacity_ = 0;  // vertices

    // Sky params pushed each frame.
    glm::vec3 sunDirection_{0.0f, 1.0f, 0.0f};
    glm::vec3 fogColor_{0.6f, 0.7f, 0.8f};
    float fogStart_ = 0.0f;
    float fogEnd_ = 0.0f;
    float skyBrightness_ = 1.0f;
    float ambientLevel_ = 0.3f;

    // Camera state for orienting quads.  Kept in double precision so the
    // world→view-relative subtraction doesn't lose precision far from origin.
    glm::dvec3 highPrecCameraPos_{0.0};
    glm::vec3 cameraRight_{1.0f, 0.0f, 0.0f};
    glm::vec3 cameraUp_{0.0f, 1.0f, 0.0f};
    glm::vec3 cameraForward_{0.0f, 0.0f, -1.0f};

    uint32_t lastParticleCount_ = 0;
    uint32_t lastVertexCount_ = 0;

    bool initialized_ = false;
};

} // namespace finevox::render
