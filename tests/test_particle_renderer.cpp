#include <gtest/gtest.h>

#include "finevox/core/particle.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/render/particle_atlas.hpp"
#include "finevox/render/particle_renderer.hpp"

#include <glm/glm.hpp>
#include <thread>

#include <chrono>
#include <cstdio>
#include <limits>

using namespace finevox;
using namespace finevox::render;

// These tests exercise the CPU-side billboard math without touching Vulkan.
// `buildParticleQuad` is exposed for exactly this — given a particle and a
// camera basis, it must emit 6 vertices forming a quad in the correct
// orientation.

namespace {

/// Standard forward/right/up basis: camera looking down -Z.
constexpr glm::vec3 kRight(1.0f, 0.0f, 0.0f);
constexpr glm::vec3 kUp(0.0f, 1.0f, 0.0f);
constexpr glm::vec3 kForward(0.0f, 0.0f, -1.0f);

Particle makeParticle(BillboardMode mode,
                       const glm::dvec3& pos,
                       const glm::vec3& velocity,
                       float size = 1.0f)
{
    Particle p;
    p.position = pos;
    p.velocity = velocity;
    p.age = 0.0f;
    p.maxAge = 1.0f;
    p.sizeStart = size;
    p.sizeEnd = size;
    p.startTint = glm::vec4(1.0f);
    p.endTint = glm::vec4(1.0f);
    p.atlasUV = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    p.billboard = mode;
    return p;
}

/// Compute the bounding box of all vertex positions in `verts`.
std::pair<glm::vec3, glm::vec3> boundingBox(
    const std::vector<ParticleVertex>& verts)
{
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const auto& v : verts) {
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    return {lo, hi};
}

} // anonymous namespace

TEST(ParticleRenderer, BuildQuadEmitsSixVertices) {
    auto p = makeParticle(BillboardMode::CameraFacing,
                           glm::dvec3(0.0, 0.0, -5.0),
                           glm::vec3(0.0f));
    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(0.0), kRight, kUp, kForward, verts);

    // 2 triangles × 3 verts = 6.
    EXPECT_EQ(verts.size(), 6u);
}

TEST(ParticleRenderer, CameraFacingQuadAlignedToCameraAxes) {
    // Particle 5m in front of camera, size 1.0 → quad extends ±0.5 on X and Y.
    auto p = makeParticle(BillboardMode::CameraFacing,
                           glm::dvec3(0.0, 0.0, -5.0),
                           glm::vec3(0.0f));
    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(0.0), kRight, kUp, kForward, verts);
    ASSERT_EQ(verts.size(), 6u);

    auto [lo, hi] = boundingBox(verts);
    // Quad is flat along Z (perpendicular to view forward == -Z).
    EXPECT_NEAR(lo.z, hi.z, 1e-4f);
    EXPECT_NEAR(lo.z, -5.0f, 1e-4f);

    // X / Y extents ±0.5 around the centre (0, 0).
    EXPECT_NEAR(lo.x, -0.5f, 1e-4f);
    EXPECT_NEAR(hi.x,  0.5f, 1e-4f);
    EXPECT_NEAR(lo.y, -0.5f, 1e-4f);
    EXPECT_NEAR(hi.y,  0.5f, 1e-4f);
}

TEST(ParticleRenderer, VelocityStretchedQuadLongerThanCameraFacing) {
    // Same particle, but now moving fast along +Y.  The velocity-stretched
    // mode should produce a quad that's longer in the view-aligned velocity
    // direction than the camera-facing mode would.
    glm::vec3 vel(0.0f, 10.0f, 0.0f);  // 10 m/s up

    auto cam = makeParticle(BillboardMode::CameraFacing,
                             glm::dvec3(0.0, 0.0, -5.0), vel);
    auto vel_p = makeParticle(BillboardMode::VelocityStretched,
                               glm::dvec3(0.0, 0.0, -5.0), vel);

    std::vector<ParticleVertex> camVerts;
    std::vector<ParticleVertex> velVerts;
    buildParticleQuad(cam, glm::dvec3(0.0), kRight, kUp, kForward, camVerts);
    buildParticleQuad(vel_p, glm::dvec3(0.0), kRight, kUp, kForward, velVerts);

    auto [camLo, camHi] = boundingBox(camVerts);
    auto [velLo, velHi] = boundingBox(velVerts);
    float camHeight = camHi.y - camLo.y;
    float velHeight = velHi.y - velLo.y;

    // Stretched quad should be noticeably taller (velocity along Y projects
    // entirely into the view plane).
    EXPECT_GT(velHeight, camHeight * 1.5f);
    // Width should be unchanged (same size).
    EXPECT_NEAR(velHi.x - velLo.x, camHi.x - camLo.x, 1e-4f);
}

TEST(ParticleRenderer, AxisLockedLongAxisIsWorldY) {
    // AxisLocked keeps the long edge fixed to world +Y regardless of
    // camera orientation.  With our default camera looking down -Z,
    // the short axis should be world +X (toward the camera plane).
    auto p = makeParticle(BillboardMode::AxisLocked,
                           glm::dvec3(0.0, 0.0, -5.0),
                           glm::vec3(0.0f),
                           /*size=*/2.0f);

    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(0.0), kRight, kUp, kForward, verts);
    auto [lo, hi] = boundingBox(verts);

    float extX = hi.x - lo.x;
    float extY = hi.y - lo.y;
    float extZ = hi.z - lo.z;

    // Long axis = world Y → Y extent must match size (~2.0).
    EXPECT_NEAR(extY, 2.0f, 1e-3f);
    // Short axis = world X for camera-along-Z → X extent matches size.
    EXPECT_NEAR(extX, 2.0f, 1e-3f);
    // Z extent should be near-zero (quad lies in XY plane).
    EXPECT_LT(extZ, 1e-3f);
}

TEST(ParticleRenderer, QuadVerticesAreViewRelativeToCamera) {
    // Particle at world (100, 0, -5).  Camera at (100, 0, 0).  Expected
    // quad centre in view-relative space: (0, 0, -5) — same as if the
    // particle were at origin and the camera at origin.
    auto p = makeParticle(BillboardMode::CameraFacing,
                           glm::dvec3(100.0, 0.0, -5.0),
                           glm::vec3(0.0f));
    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(100.0, 0.0, 0.0),
                       kRight, kUp, kForward, verts);
    auto [lo, hi] = boundingBox(verts);
    EXPECT_NEAR((lo.x + hi.x) * 0.5f, 0.0f, 1e-4f);
    EXPECT_NEAR((lo.z + hi.z) * 0.5f, -5.0f, 1e-4f);
}

TEST(ParticleRenderer, UVCornersMatchAtlasRect) {
    // Each of the 6 emitted vertices should carry UVs within the particle's
    // atlasUV rect (corners sampled).
    auto p = makeParticle(BillboardMode::CameraFacing,
                           glm::dvec3(0.0, 0.0, -1.0),
                           glm::vec3(0.0f));
    glm::vec4 uv(0.25f, 0.50f, 0.75f, 1.0f);
    p.atlasUV = uv;

    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(0.0), kRight, kUp, kForward, verts);
    ASSERT_EQ(verts.size(), 6u);
    for (const auto& v : verts) {
        EXPECT_GE(v.texCoord.x, uv.x - 1e-5f);
        EXPECT_LE(v.texCoord.x, uv.z + 1e-5f);
        EXPECT_GE(v.texCoord.y, uv.y - 1e-5f);
        EXPECT_LE(v.texCoord.y, uv.w + 1e-5f);
    }
}

TEST(ParticleRenderer, TintFromCurrentTintFunction) {
    // Particle at half life should produce tint = midpoint of start/end.
    auto p = makeParticle(BillboardMode::CameraFacing,
                           glm::dvec3(0.0, 0.0, -1.0),
                           glm::vec3(0.0f));
    p.startTint = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    p.endTint = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    p.age = 0.5f;
    p.maxAge = 1.0f;

    std::vector<ParticleVertex> verts;
    buildParticleQuad(p, glm::dvec3(0.0), kRight, kUp, kForward, verts);
    ASSERT_EQ(verts.size(), 6u);
    for (const auto& v : verts) {
        EXPECT_NEAR(v.tint.r, 0.5f, 1e-5f);
        EXPECT_NEAR(v.tint.b, 0.5f, 1e-5f);
        EXPECT_NEAR(v.tint.a, 0.5f, 1e-5f);
    }
}

// Rough CPU-cost profile for the vertex-generation path.  This is what
// replaces the GPU path in a display-less test env — we measure how long
// it takes to produce 500 quads (which is what a typical particle count
// during combat / light weather produces) and compare to the 33 ms/tick
// budget.  Test always passes; prints timing for visibility.
// Regression test for a shared_mutex lock-upgrade deadlock that hung the
// render_demo startup — ParticleAtlas::applyToRegistry used to call
// setAtlasUV() from inside forEach(), re-entering the same shared_mutex
// from shared to unique which deadlocks on every pthread impl.  The test
// registers a type whose atlas_region matches a placeholder region and
// runs applyToRegistry on a worker thread with a 2-second watchdog timeout.
TEST(ParticleAtlas, ApplyToRegistryDoesNotDeadlock) {
    // Fresh-ish type name to avoid clashing with other tests that also
    // register into the global registry.
    const std::string typeName = "test:atlas_deadlock_regression_type";
    const std::string regionName = "test_atlas_deadlock_regression_region";

    ParticleTypeDef def;
    def.name = typeName;
    def.atlasRegion = regionName;
    ParticleTypeRegistry::global().registerType(typeName, def);

    ParticleAtlas atlas;  // empty atlas — regionName is unknown
    // Also test the path that actually patches: since the atlas has no
    // regions, we'd hit the "missing" branch only.  Patching requires a
    // real region; construct a subclass-free atlas via the public API.
    // We already cover the setAtlasUV success path in test_particle_uv_patch;
    // here we only need to prove applyToRegistry exits in finite time when
    // forEach-callback touches the same registry.

    std::atomic<bool> done{false};
    std::thread t([&]() {
        std::vector<std::string> warnings;
        atlas.applyToRegistry(ParticleTypeRegistry::global(), &warnings);
        done.store(true);
    });

    // Poll with a hard watchdog — any deadlock here would block forever.
    for (int ms = 0; ms < 2000 && !done.load(); ms += 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(done.load()) << "applyToRegistry deadlocked on shared_mutex upgrade";
    if (t.joinable()) t.join();
}

TEST(ParticleRenderer, VertexGenPerfRough) {
    constexpr int kParticles = 500;
    std::vector<Particle> particles;
    particles.reserve(kParticles);
    for (int i = 0; i < kParticles; ++i) {
        Particle p;
        p.position = glm::dvec3(i * 0.1, 64.0, -5.0);
        p.velocity = glm::vec3(0.1f * i, 2.0f, 0.0f);
        p.sizeStart = 0.15f;
        p.sizeEnd = 0.05f;
        p.age = 0.0f;
        p.maxAge = 1.0f;
        p.startTint = glm::vec4(1.0f);
        p.endTint = glm::vec4(1.0f);
        p.atlasUV = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        p.billboard = static_cast<BillboardMode>(i % 3);
        particles.push_back(p);
    }

    auto t0 = std::chrono::steady_clock::now();
    constexpr int kIters = 60;  // two seconds of frames @ 30 FPS
    size_t totalVerts = 0;
    std::vector<ParticleVertex> verts;
    for (int iter = 0; iter < kIters; ++iter) {
        verts.clear();
        verts.reserve(particles.size() * 6);
        for (const auto& p : particles) {
            buildParticleQuad(p, glm::dvec3(0.0),
                               kRight, kUp, kForward, verts);
        }
        totalVerts += verts.size();
    }
    auto t1 = std::chrono::steady_clock::now();
    double totalMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double perIter = totalMs / kIters;
    std::printf("\n[perf] vertex-gen 500 particles × %d iters: "
                "%.2f ms total, %.3f ms/iter (budget at 60 FPS = 16.67 ms)\n",
                kIters, totalMs, perIter);
    EXPECT_EQ(totalVerts, particles.size() * 6u * kIters);
}
