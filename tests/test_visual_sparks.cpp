/**
 * @file test_visual_sparks.cpp
 * @brief Golden-image visual tests — particle billboard-mode matrix.
 *
 * Renders deterministic particle bursts (fixed RNG seed, fixed camera, fixed
 * number of ticks) into a headless OffscreenSurface and byte-compares a small
 * ROI readback against committed PNGs at tests/golden/sparks_*.png.
 *
 * Three curated tests cover the three BillboardMode branches:
 *   - GoldenBasic          — VelocityStretched (spark-style)
 *   - GoldenCameraFacing   — CameraFacing      (block-fragment-style)
 *   - GoldenAxisLocked     — AxisLocked        (smoke-puff-style)
 * Plus GoldenDeterministicAcrossRuns which re-runs the basic scene twice and
 * byte-compares the two captures (catches nondeterminism independent of
 * whether the committed golden matches).
 *
 * Shared scaffolding lives in this file; when/if a fourth consumer appears,
 * lift it to VisualTestHarness.
 *
 * On the first run (or after an intentional visual change), set
 * FINEVOX_UPDATE_GOLDENS=1 to mint / refresh the golden:
 *
 *     FINEVOX_UPDATE_GOLDENS=1 ./finevox_render_tests \
 *         --gtest_filter='VisualSparks.*'
 *
 * Then re-run without the env var to confirm the test passes against the
 * committed golden.
 */

#include <gtest/gtest.h>

#include "finevox/core/particle.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/particle_system.hpp"
#include "finevox/core/particle_type.hpp"
#include "finevox/core/world.hpp"
#include "finevox/render/frame_capture.hpp"
#include "finevox/render/particle_atlas.hpp"
#include "finevox/render/particle_renderer.hpp"
#include "finevox/render/visual_test_harness.hpp"

#include <finevk/core/instance.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/physical_device.hpp>
#include <finevk/device/sampler.hpp>
#include <finevk/device/staging_pool.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/rendering/sync.hpp>         // Full type for OffscreenSurface's Fence member
#include <finevk/rendering/offscreen_surface.hpp>

#include <glm/glm.hpp>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

// Paths passed in from CMake so the test is robust to working-directory
// quirks in CTest / IDE runners.  Fall back to relative paths if somehow
// undefined (shouldn't happen via our CMakeLists but keeps the file legible).
#ifndef FINEVOX_SHADER_DIR
#define FINEVOX_SHADER_DIR "shaders"
#endif
#ifndef FINEVOX_SOURCE_DIR
#define FINEVOX_SOURCE_DIR "."
#endif

using namespace finevox;
using namespace finevox::render;

namespace {

// Tolerance: absolute byte delta per channel across RGBA.  Starting point is
// 4 per the spec's guidance; increase if driver / GLSL compiler drift shows
// up on CI.  Actual max diff is logged on every pass so regressions are
// visible before they hit the threshold.
constexpr uint8_t kMaxAbsDiffPerChannel = 4;

// ROI dimensions.  Small enough that golden PNG stays under a few KB on disk.
constexpr uint32_t kCaptureWidth = 96;
constexpr uint32_t kCaptureHeight = 96;

// Offscreen render extent is larger than the ROI so the particles can move
// around and so fog/depth math has room to exercise (rather than a tiny
// degenerate viewport).
constexpr uint32_t kSurfaceWidth = 256;
constexpr uint32_t kSurfaceHeight = 256;

// Deterministic simulation parameters.
constexpr uint64_t kRngSeed = 0xfeedbeefcafe1234ULL;
constexpr float    kTickDt = 1.0f / 30.0f;  // 30 TPS

// Scene config for a single visual test case.  Keeps the RNG seed, tick count,
// and camera pose explicit per-mode so a regression in the billboard-selection
// code path actually changes captured pixels for the mode it breaks.
struct SceneConfig {
    const char* particleType;
    const char* emitterPreset;
    BillboardMode billboard;
    Range lifetime{1.0f, 1.0f};
    Range initialSpeed{0.0f, 0.0f};
    glm::vec3 velocityBias{0.0f};
    float gravity = 0.0f;
    float drag = 0.0f;
    Range size{0.2f, 0.2f};
    float sizeEndScale = 1.0f;
    glm::vec4 startTint{1.0f};
    glm::vec4 endTint{1.0f};
    uint32_t burstCount = 16;
    glm::vec3 cameraPos{0.0f, 0.0f, 8.0f};
    glm::vec3 cameraTarget{0.0f, 0.0f, 0.0f};
    int ticksBeforeCapture = 3;
};

struct HeadlessCtx {
    finevk::InstancePtr instance;
    finevk::PhysicalDevice physicalDevice;  // LogicalDevice borrows a pointer
    finevk::LogicalDevicePtr device;
    finevk::StagingPoolPtr stagingPool;
    std::unique_ptr<finevk::OffscreenSurface> surface;
};

HeadlessCtx makeHeadlessContext() {
    HeadlessCtx c;
    c.instance = finevk::Instance::create()
        .applicationName("finevox_visual_test")
        .headless()
        .build();
    c.physicalDevice = c.instance->selectPhysicalDevice();
    c.device = c.physicalDevice.createLogicalDevice().build();
    c.stagingPool = finevk::StagingPool::create(c.device.get())
        .initialSize(4 * 1024 * 1024)
        .build();
    c.surface = finevk::OffscreenSurface::create(c.device.get())
        .extent(kSurfaceWidth, kSurfaceHeight)
        .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)   // UNORM = trivial byte round-trip
        .enableDepth()
        .build();
    return c;
}

void registerScene(const SceneConfig& s) {
    // Register once per process.  registerType/registerPreset are idempotent —
    // a second call with the same name is a no-op, so TEST() bodies can
    // blindly re-register without worrying about ordering.
    ParticleTypeDef def;
    def.name = s.particleType;
    def.lifetime = s.lifetime;
    def.initialSpeed = s.initialSpeed;
    def.velocityBias = s.velocityBias;
    def.gravity = s.gravity;
    def.drag = s.drag;
    def.size = s.size;
    def.sizeEndScale = s.sizeEndScale;
    def.startTint = s.startTint;
    def.endTint = s.endTint;
    def.atlasRegion = "";                       // placeholder atlas → full UV
    def.billboard = s.billboard;
    def.collidesWithTerrain = false;
    ParticleTypeRegistry::global().registerType(s.particleType, def);

    EmitterPresetDef preset;
    preset.name = s.emitterPreset;
    preset.shape = EmitterShape::Point;
    preset.burstCount = s.burstCount;
    preset.duration = 0.0f;                     // one-shot
    preset.particleType = s.particleType;
    EmitterPresetRegistry::global().registerPreset(s.emitterPreset, preset);
}

finevk::CameraState makeCamera(const SceneConfig& s) {
    finevk::Camera cam;
    cam.setPerspective(/*fov=*/60.0f,
                        /*aspect=*/float(kSurfaceWidth) / kSurfaceHeight,
                        /*near=*/0.1f,
                        /*far=*/100.0f);
    cam.moveTo(s.cameraPos);
    cam.lookAt(s.cameraTarget);
    cam.updateState();
    return cam.state();
}

void tickToCapturePoint(ParticleSystem& sim, int ticks) {
    for (int i = 0; i < ticks; ++i) {
        sim.tick(kTickDt, /*entities=*/nullptr);
    }
}

// ROI centred on the offscreen surface.
CaptureRect centeredROI() {
    CaptureRect r;
    r.x = (kSurfaceWidth - kCaptureWidth) / 2;
    r.y = (kSurfaceHeight - kCaptureHeight) / 2;
    r.width = kCaptureWidth;
    r.height = kCaptureHeight;
    return r;
}

struct CapturedFrame {
    std::vector<uint8_t> rgba;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Run the entire scene end-to-end: register types, tick, render, read back.
// Returns the captured ROI as an RGBA byte vector.  Caller owns the context
// so it can be reused for the determinism repeat.
CapturedFrame renderOneFrame(HeadlessCtx& ctx, const SceneConfig& scene) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(kRngSeed);

    registerScene(scene);

    sim.burst(scene.emitterPreset, glm::dvec3(0.0, 0.0, 0.0));
    tickToCapturePoint(sim, scene.ticksBeforeCapture);

    // Placeholder atlas: 1×1 white, full-[0,1]² UV per region.  The test
    // cares about *geometry* and *tint interpolation*, not texture fidelity.
    ParticleAtlas atlas;
    atlas.createPlaceholder(ctx.device.get(),
                             ctx.device->defaultCommandPool());

    ParticleRenderer renderer(ctx.device.get(), ctx.surface.get(), sim);
    renderer.loadShaders(
        std::string(FINEVOX_SHADER_DIR) + "/particle.vert.spv",
        std::string(FINEVOX_SHADER_DIR) + "/particle.frag.spv");
    renderer.setAtlas(&atlas);
    renderer.initialize();

    finevk::CameraState camState = makeCamera(scene);
    renderer.updateCamera(camState, glm::dvec3(scene.cameraPos));
    renderer.setSkyParameters(
        /*sunDir=*/glm::vec3(0.0f, 1.0f, 0.0f),
        /*fogColor=*/glm::vec3(0.05f, 0.05f, 0.08f),
        /*fogStart=*/200.0f,   // beyond far plane → no fog
        /*fogEnd=*/500.0f,
        /*skyBrightness=*/1.0f,
        /*ambientLevel=*/0.3f);

    ctx.surface->beginFrame();
    // Clear to a very dark blue so sparks read clearly in the capture.
    ctx.surface->beginRenderPass({0.02f, 0.02f, 0.05f, 1.0f}, 1.0f);
    renderer.render(*ctx.surface->currentCommandBuffer());
    ctx.surface->endRenderPass();
    ctx.surface->endFrame();

    CapturedFrame out;
    const bool ok = FrameCapture::readbackRGBA(
        *ctx.surface, *ctx.stagingPool, centeredROI(),
        out.rgba, out.width, out.height);
    EXPECT_TRUE(ok) << "FrameCapture::readbackRGBA failed";
    return out;
}

// Scene presets.  Each one exercises one BillboardMode branch; poses are
// chosen so the rendered pixels would change if the branch were misrouted.

SceneConfig basicScene() {
    SceneConfig s;
    s.particleType = "visual_test:spark_basic";
    s.emitterPreset = "visual_test:spark_burst_basic";
    s.billboard = BillboardMode::VelocityStretched;
    s.initialSpeed = Range{6.0f, 6.0f};
    s.size = Range{0.2f, 0.2f};
    s.sizeEndScale = 0.5f;
    s.startTint = glm::vec4(1.0f, 0.85f, 0.3f, 1.0f);
    s.endTint   = glm::vec4(1.0f, 0.2f, 0.05f, 0.0f);
    s.burstCount = 24;
    // Camera down -Z — velocity-stretched quads show their radial spread.
    s.cameraPos = glm::vec3(0.0f, 0.0f, 8.0f);
    s.cameraTarget = glm::vec3(0.0f);
    s.ticksBeforeCapture = 3;
    return s;
}

SceneConfig cameraFacingScene() {
    SceneConfig s;
    s.particleType = "visual_test:puff_camera";
    s.emitterPreset = "visual_test:puff_camera_burst";
    s.billboard = BillboardMode::CameraFacing;
    // Slow drift so particles spread a little but stay on-screen at tick 3.
    s.initialSpeed = Range{1.2f, 1.2f};
    s.velocityBias = glm::vec3(0.0f, 0.6f, 0.0f);
    s.size = Range{0.35f, 0.35f};
    s.sizeEndScale = 1.0f;
    s.startTint = glm::vec4(0.6f, 0.8f, 1.0f, 1.0f);
    s.endTint   = glm::vec4(0.2f, 0.4f, 0.8f, 0.4f);
    s.burstCount = 16;
    // Off-axis camera: if the mode were (wrongly) AxisLocked, quads would
    // rotate around world Y and project differently from CameraFacing.
    s.cameraPos = glm::vec3(3.5f, 2.0f, 6.0f);
    s.cameraTarget = glm::vec3(0.0f);
    s.ticksBeforeCapture = 3;
    return s;
}

SceneConfig axisLockedScene() {
    SceneConfig s;
    s.particleType = "visual_test:column_axis";
    s.emitterPreset = "visual_test:column_axis_burst";
    s.billboard = BillboardMode::AxisLocked;
    // Mostly vertical motion so the axis-locked (Y) quads stay visible.
    s.initialSpeed = Range{0.5f, 0.5f};
    s.velocityBias = glm::vec3(0.0f, 1.2f, 0.0f);
    s.size = Range{0.35f, 0.35f};
    s.sizeEndScale = 1.4f;
    s.startTint = glm::vec4(0.8f, 0.8f, 0.8f, 0.9f);
    s.endTint   = glm::vec4(0.4f, 0.4f, 0.4f, 0.2f);
    s.burstCount = 16;
    // Same off-axis pose as CameraFacing so the two goldens differ because of
    // the billboard math, not a different camera.  An AxisLocked quad's X
    // vector lies in the world XZ plane; a CameraFacing quad's X vector lies
    // in the view plane — from this pose the projected footprints differ.
    s.cameraPos = glm::vec3(3.5f, 2.0f, 6.0f);
    s.cameraTarget = glm::vec3(0.0f);
    s.ticksBeforeCapture = 3;
    return s;
}

void runGoldenTest(const SceneConfig& scene, const char* goldenName) {
    HeadlessCtx ctx = makeHeadlessContext();
    CapturedFrame frame = renderOneFrame(ctx, scene);
    ASSERT_EQ(frame.rgba.size(),
              static_cast<size_t>(kCaptureWidth) * kCaptureHeight * 4);

    const std::string goldenPath =
        std::string(FINEVOX_SOURCE_DIR) + "/tests/golden/" + goldenName + ".png";

    GoldenCompareResult r = VisualTestHarness::compareAgainstGolden(
        frame.rgba, frame.width, frame.height,
        goldenPath, kMaxAbsDiffPerChannel);

    std::cerr << "[VisualSparks] golden=" << goldenName
              << " tolerance=" << int(kMaxAbsDiffPerChannel)
              << " maxAbsDiff=" << int(r.maxAbsDiff)
              << " failingPixels=" << r.failingPixelCount << "\n";

    if (!r.passed) {
        const std::string lastActual =
            std::string(FINEVOX_SOURCE_DIR) + "/tests/golden/" + goldenName +
            ".actual.png";
        FrameCapture::savePNG(lastActual, frame.width, frame.height, frame.rgba);
        std::cerr << "[VisualSparks] wrote last actual to " << lastActual << "\n";
    }

    EXPECT_TRUE(r.passed) << "reason: " << r.failureReason
                           << " firstFail=(" << r.firstFailX << ","
                           << r.firstFailY << ")";
}

} // anonymous namespace

TEST(VisualSparks, GoldenBasic) {
    runGoldenTest(basicScene(), "sparks_basic");
}

TEST(VisualSparks, GoldenCameraFacing) {
    runGoldenTest(cameraFacingScene(), "sparks_camera_facing");
}

TEST(VisualSparks, GoldenAxisLocked) {
    runGoldenTest(axisLockedScene(), "sparks_axis_locked");
}

TEST(VisualSparks, DeterministicAcrossRuns) {
    // Render the same scene twice in the same process and verify the two
    // captures are byte-identical.  This catches nondeterminism sources
    // (unsorted iteration, frame-index state leaking between runs, etc.)
    // independent of whether the golden matches.
    HeadlessCtx ctx = makeHeadlessContext();
    const SceneConfig scene = basicScene();
    CapturedFrame a = renderOneFrame(ctx, scene);
    // Reset the OffscreenSurface before the second run so its internal
    // single-buffered state starts fresh.  Building a second ctx also works
    // but would double the test's Vulkan-init cost; resize-to-same-size is a
    // cheap way to drive OffscreenSurface through its recreate path.
    ctx.surface->resize(kSurfaceWidth, kSurfaceHeight);
    CapturedFrame b = renderOneFrame(ctx, scene);

    ASSERT_EQ(a.rgba.size(), b.rgba.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        if (a.rgba[i] != b.rgba[i]) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0u)
        << "same-process rerun produced " << mismatches
        << " mismatched bytes — scene is not deterministic";
}
