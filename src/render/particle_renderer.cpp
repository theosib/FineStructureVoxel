#include "finevox/render/particle_renderer.hpp"

#include "finevox/core/particle_system.hpp"

#include <finevk/device/command.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/sampler.hpp>
#include <finevk/rendering/pipeline.hpp>  // ShaderModule lives here

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace finevox::render {

// ============================================================================
// Vertex input descriptions
// ============================================================================

VkVertexInputBindingDescription getParticleVertexBindingDescription() {
    VkVertexInputBindingDescription b{};
    b.binding = 0;
    b.stride = sizeof(ParticleVertex);
    b.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return b;
}

std::vector<VkVertexInputAttributeDescription>
getParticleVertexAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(3);
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                offsetof(ParticleVertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,
                offsetof(ParticleVertex, texCoord)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                offsetof(ParticleVertex, tint)};
    return attrs;
}

// ============================================================================
// Push constants (binary-compatible with the chunk pipeline's layout so we
// could share PipelineLayout; we use a dedicated layout anyway to keep the
// render dependencies local).
// ============================================================================

struct ParticlePushConstants {
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

// ============================================================================
// buildParticleQuad — CPU billboard orientation
// ============================================================================

namespace {

// Push two triangles forming a quad into `out` given 4 corners + UV + tint.
inline void pushQuad(
    std::vector<ParticleVertex>& out,
    const glm::vec3& bl, const glm::vec3& br,
    const glm::vec3& tl, const glm::vec3& tr,
    const glm::vec4& uv, const glm::vec4& tint)
{
    // UV layout: min = (uv.x, uv.y) = bottom-left, max = (uv.z, uv.w) = top-right.
    glm::vec2 uvBL(uv.x, uv.w);
    glm::vec2 uvBR(uv.z, uv.w);
    glm::vec2 uvTL(uv.x, uv.y);
    glm::vec2 uvTR(uv.z, uv.y);
    // Triangle 1: bl, br, tr
    out.push_back({bl, uvBL, tint});
    out.push_back({br, uvBR, tint});
    out.push_back({tr, uvTR, tint});
    // Triangle 2: bl, tr, tl
    out.push_back({bl, uvBL, tint});
    out.push_back({tr, uvTR, tint});
    out.push_back({tl, uvTL, tint});
}

} // anonymous namespace

void buildParticleQuad(
    const Particle& p,
    const glm::dvec3& cameraPos,
    const glm::vec3& cameraRight,
    const glm::vec3& cameraUp,
    const glm::vec3& cameraForward,
    std::vector<ParticleVertex>& out)
{
    // View-relative centre: subtract camera in double precision, then store as float.
    glm::vec3 centre = glm::vec3(p.position - cameraPos);
    float halfSize = 0.5f * p.currentSize();
    glm::vec4 tint = p.currentTint();

    switch (p.billboard) {
    case BillboardMode::CameraFacing: {
        glm::vec3 right = cameraRight * halfSize;
        glm::vec3 up    = cameraUp    * halfSize;
        pushQuad(out,
                 centre - right - up,   // bl
                 centre + right - up,   // br
                 centre - right + up,   // tl
                 centre + right + up,   // tr
                 p.atlasUV, tint);
        break;
    }
    case BillboardMode::VelocityStretched: {
        // Long axis = particle velocity projected onto the view plane.
        // Short axis = cross(long, cameraForward).  Length scales with speed.
        float speed = glm::length(p.velocity);
        glm::vec3 velDir = (speed > 1e-4f)
            ? p.velocity / speed
            : cameraUp;   // fall back to camera-facing orientation

        // Project velocity onto the plane perpendicular to view direction.
        glm::vec3 viewAligned = velDir - cameraForward * glm::dot(velDir, cameraForward);
        float viewAlignedLen = glm::length(viewAligned);
        glm::vec3 longAxis = (viewAlignedLen > 1e-4f)
            ? viewAligned / viewAlignedLen
            : cameraUp;

        glm::vec3 shortAxis = glm::cross(longAxis, cameraForward);
        float shortLen = glm::length(shortAxis);
        if (shortLen > 1e-4f) shortAxis /= shortLen; else shortAxis = cameraRight;

        // Stretch length ~= size * (1 + speed·k), clamped.
        const float kStretch = 0.15f;  // 1 m/s adds 15 % length
        float lengthScale = std::min(4.0f, 1.0f + speed * kStretch);
        glm::vec3 lv = longAxis  * (halfSize * lengthScale);
        glm::vec3 sv = shortAxis * halfSize;

        pushQuad(out,
                 centre - lv - sv,  // bl
                 centre - lv + sv,  // br  (short axis = 'right')
                 centre + lv - sv,  // tl
                 centre + lv + sv,  // tr
                 p.atlasUV, tint);
        break;
    }
    case BillboardMode::AxisLocked: {
        // Long axis fixed to world +Y; short axis = perpendicular in XZ,
        // pointing toward the camera for best visibility.
        glm::vec3 longAxis(0.0f, 1.0f, 0.0f);
        glm::vec3 toCam = -cameraForward; // from particle toward camera
        glm::vec3 shortAxis = glm::cross(longAxis, toCam);
        float shortLen = glm::length(shortAxis);
        if (shortLen > 1e-4f) shortAxis /= shortLen; else shortAxis = cameraRight;

        glm::vec3 lv = longAxis  * halfSize;
        glm::vec3 sv = shortAxis * halfSize;
        pushQuad(out,
                 centre - sv - lv,  // bl
                 centre + sv - lv,  // br
                 centre - sv + lv,  // tl
                 centre + sv + lv,  // tr
                 p.atlasUV, tint);
        break;
    }
    }
}

// ============================================================================
// ParticleRenderer
// ============================================================================

ParticleRenderer::ParticleRenderer(
    finevk::LogicalDevice* device,
    finevk::RenderSurface* surface,
    ParticleSystem& system)
    : device_(device), surface_(surface), system_(system) {}

ParticleRenderer::~ParticleRenderer() = default;

void ParticleRenderer::loadShaders(const std::string& vertPath,
                                    const std::string& fragPath) {
    vertexShader_ = finevk::ShaderModule::fromFile(device_, vertPath);
    fragmentShader_ = finevk::ShaderModule::fromFile(device_, fragPath);
}

void ParticleRenderer::initialize() {
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

    descriptorSets_ = descriptorPool_->allocate(
        descriptorLayout_.get(), surface_->framesInFlight());

    sampler_ = finevk::Sampler::createLinear(device_);

    createPipeline();
    initialized_ = true;
}

void ParticleRenderer::createPipeline() {
    pipelineLayout_ = finevk::PipelineLayout::create(device_)
        .addDescriptorSetLayout(descriptorLayout_->handle())
        .addPushConstantRange(
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(ParticlePushConstants))
        .build();

    auto binding = getParticleVertexBindingDescription();
    auto attrs = getParticleVertexAttributeDescriptions();

    auto builder = finevk::GraphicsPipeline::create(
        device_, surface_->renderPass(), pipelineLayout_.get());

    builder.vertexShader(vertexShader_.get())
           .fragmentShader(fragmentShader_.get())
           .vertexBinding(binding.binding, binding.stride, binding.inputRate);

    for (const auto& a : attrs) {
        builder.vertexAttribute(a.location, a.binding, a.format, a.offset);
    }

    pipeline_ = builder
        .enableDepth()
        .depthWrite(false)     // depth test against opaque, don't stamp
        .alphaBlending()       // SRC_ALPHA, ONE_MINUS_SRC_ALPHA
        .cullNone()
        .frontFace(VK_FRONT_FACE_CLOCKWISE)
        .samples(surface_->msaaSamples())
        .dynamicViewportAndScissor()
        .build();
}

void ParticleRenderer::ensureDescriptorSets() {
    if (descriptorsWritten_) return;
    if (!atlas_ || !atlas_->texture()) return;

    finevk::DescriptorWriter writer(device_);
    for (uint32_t i = 0; i < surface_->framesInFlight(); ++i) {
        writer.writeBuffer(descriptorSets_[i], 0,
                            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                            *cameraUniform_->buffer(i));
        writer.writeImage(descriptorSets_[i], 1,
                            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                            atlas_->texture()->view(),
                            sampler_.get());
    }
    writer.update();
    descriptorsWritten_ = true;
}

void ParticleRenderer::updateCamera(const finevk::CameraState& cameraState,
                                     const glm::dvec3& highPrecisionPos) {
    highPrecCameraPos_ = highPrecisionPos;

    // View matrix rows give the camera basis in world space.
    // The view matrix's rotation part is the *transpose* of the world-space basis,
    // so columns of the inverse-rotation (== rows of the view rotation) give the
    // world-space camera axes.  FineVK's viewRelative keeps camera at origin.
    const glm::mat4& v = cameraState.viewRelative;
    cameraRight_   = glm::vec3(v[0][0], v[1][0], v[2][0]);
    cameraUp_      = glm::vec3(v[0][1], v[1][1], v[2][1]);
    cameraForward_ = -glm::vec3(v[0][2], v[1][2], v[2][2]);

    finevk::CameraUniform u{};
    u.view = cameraState.viewRelative;
    u.projection = cameraState.projection;
    u.viewProjection = cameraState.projection * cameraState.viewRelative;
    u.position = glm::vec3(highPrecisionPos);
    cameraUniform_->update(surface_->currentFrame(), u);
}

void ParticleRenderer::setSkyParameters(
    const glm::vec3& sunDirection,
    const glm::vec3& fogColor,
    float fogStart, float fogEnd,
    float skyBrightness, float ambientLevel)
{
    sunDirection_ = sunDirection;
    fogColor_ = fogColor;
    fogStart_ = fogStart;
    fogEnd_ = fogEnd;
    skyBrightness_ = skyBrightness;
    ambientLevel_ = ambientLevel;
}

void ParticleRenderer::uploadVertices(const std::vector<ParticleVertex>& verts) {
    if (verts.empty()) {
        lastVertexCount_ = 0;
        return;
    }

    // We don't deduplicate — 6 vertices per particle, trivial 0..N-1 index list.
    // Particles are dynamic enough that deduplication wouldn't buy much.
    std::vector<uint32_t> indices(verts.size());
    for (uint32_t i = 0; i < verts.size(); ++i) indices[i] = i;

    auto* cmdPool = surface_->commandPool();

    if (!mesh_) {
        // First-time build.  reserveCapacity(2.0) gives 100 % headroom so
        // small growth doesn't thrash allocations; update() handles growth
        // past capacity on its own.
        mesh_ = finevk::RawMesh::create(device_)
            .vertexLayout(getParticleVertexBindingDescription(),
                          getParticleVertexAttributeDescriptions())
            .vertices(verts.data(), verts.size())
            .indices(indices.data(), indices.size())
            .reserveCapacity(2.0f)
            .build(cmdPool);
        meshCapacity_ = verts.size();
    } else {
        mesh_->update(*cmdPool, verts.data(), verts.size(),
                      indices.data(), indices.size());
        meshCapacity_ = std::max(meshCapacity_, verts.size());
    }
    lastVertexCount_ = static_cast<uint32_t>(verts.size());
}

void ParticleRenderer::render(finevk::CommandBuffer& cmd) {
    if (!initialized_) return;
    if (!atlas_) return;

    ensureDescriptorSets();
    if (!descriptorsWritten_) return;

    auto snap = system_.snapshot();
    lastParticleCount_ = static_cast<uint32_t>(snap.particles.size());
    if (snap.particles.empty()) return;

    // Sort back-to-front by squared distance to camera.  We store the
    // squared distance once per particle to avoid repeated subtraction in
    // the comparator.
    std::vector<std::pair<float, const Particle*>> ordered;
    ordered.reserve(snap.particles.size());
    for (const auto& p : snap.particles) {
        glm::dvec3 d = p.position - highPrecCameraPos_;
        float d2 = static_cast<float>(d.x * d.x + d.y * d.y + d.z * d.z);
        ordered.emplace_back(d2, &p);
    }
    std::sort(ordered.begin(), ordered.end(),
        [](const auto& a, const auto& b) { return a.first > b.first; });

    // Build vertex buffer (6 verts per particle).
    std::vector<ParticleVertex> verts;
    verts.reserve(ordered.size() * 6);
    for (const auto& [_, pPtr] : ordered) {
        buildParticleQuad(*pPtr, highPrecCameraPos_,
                          cameraRight_, cameraUp_, cameraForward_, verts);
    }

    uploadVertices(verts);
    if (!mesh_ || lastVertexCount_ == 0) return;

    pipeline_->bind(cmd.handle());
    VkExtent2D extent = surface_->extent();
    cmd.setViewportAndScissor(extent.width, extent.height);

    VkDescriptorSet ds = descriptorSets_[surface_->currentFrame()];
    pipelineLayout_->bindDescriptorSet(cmd.handle(), ds);

    ParticlePushConstants pc{};
    pc.chunkOffset = glm::vec3(0.0f);
    pc.fogStart = fogStart_;
    pc.fogColor = fogColor_;
    pc.fogEnd = fogEnd_;
    pc.sunDirection = sunDirection_;
    pc.skyBrightness = skyBrightness_;
    pc.ambientLevel = ambientLevel_;

    pipelineLayout_->pushConstants(cmd.handle(),
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, pc);

    mesh_->bind(cmd);
    mesh_->draw(cmd);
}

} // namespace finevox::render
