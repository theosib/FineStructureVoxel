/**
 * @file test_visual_sparks.cpp
 * @brief Golden-image visual test — VelocityStretched "spark" particles.
 *
 * Renders a deterministic spark burst (fixed RNG seed, fixed camera, fixed
 * number of ticks) into a headless OffscreenSurface and byte-compares a small
 * ROI readback against a committed PNG at tests/golden/sparks_basic.png.
 *
 * This is the first concrete consumer of the finevox::render::FrameCapture /
 * finevox::render::VisualTestHarness library components.  Success here proves:
 *
 *  - Headless Vulkan works (no GLFW, no window, no display).
 *  - ParticleRenderer renders correctly into an OffscreenSurface (no
 *    SimpleRenderer coupling).
 *  - VelocityStretched particles draw deterministically given seeded RNG.
 *  - Readback → PNG encode → PNG decode → byte compare round-trips within
 *    tolerance.
 *
 * On the first run, set FINEVOX_UPDATE_GOLDENS=1 to mint the golden:
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
constexpr int      kTicksBeforeCapture = 3; // enough for sparks to spread

// Test-only particle + emitter names.  Prefixed with "visual_test:" so they
// never collide with production types in the shared global registry.
constexpr const char* kParticleType = "visual_test:spark_basic";
constexpr const char* kEmitterPreset = "visual_test:spark_burst_basic";

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

void registerDeterministicSpark() {
    // Register once per process.  Multiple TEST() bodies will call this; the
    // registry's registerType returns false on collision and we simply treat
    // that as "already installed".
    ParticleTypeDef def;
    def.name = kParticleType;
    def.lifetime = Range{1.0f, 1.0f};          // fixed, not a range, so RNG
    def.initialSpeed = Range{6.0f, 6.0f};       // is only consumed by
    def.velocityBias = glm::vec3(0.0f, 0.0f, 0.0f); // direction sampling.
    def.gravity = 0.0f;                         // gravity-off for stable frame
    def.drag = 0.0f;
    def.size = Range{0.2f, 0.2f};
    def.sizeEndScale = 0.5f;
    def.startTint = glm::vec4(1.0f, 0.85f, 0.3f, 1.0f); // yellow-orange
    def.endTint   = glm::vec4(1.0f, 0.2f, 0.05f, 0.0f); // fades to red + alpha 0
    def.atlasRegion = "";                       // placeholder atlas → full UV
    def.billboard = BillboardMode::VelocityStretched;
    def.collidesWithTerrain = false;
    ParticleTypeRegistry::global().registerType(kParticleType, def);

    EmitterPresetDef preset;
    preset.name = kEmitterPreset;
    preset.shape = EmitterShape::Point;
    preset.burstCount = 24;                     // enough for visible spread
    preset.duration = 0.0f;                     // one-shot
    preset.particleType = kParticleType;
    EmitterPresetRegistry::global().registerPreset(kEmitterPreset, preset);
}

// Build a fixed camera state looking down -Z at the origin.
finevk::CameraState makeFixedCamera() {
    finevk::Camera cam;
    cam.setPerspective(/*fov=*/60.0f,
                        /*aspect=*/float(kSurfaceWidth) / kSurfaceHeight,
                        /*near=*/0.1f,
                        /*far=*/100.0f);
    cam.moveTo(glm::vec3(0.0f, 0.0f, 8.0f));
    cam.lookAt(glm::vec3(0.0f, 0.0f, 0.0f));
    cam.updateState();
    return cam.state();
}

void tickToCapturePoint(ParticleSystem& sim) {
    for (int i = 0; i < kTicksBeforeCapture; ++i) {
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
CapturedFrame renderOneFrame(HeadlessCtx& ctx) {
    World world;
    ParticleSystem sim(world);
    sim.setSeed(kRngSeed);

    registerDeterministicSpark();

    sim.burst(kEmitterPreset, glm::dvec3(0.0, 0.0, 0.0));
    tickToCapturePoint(sim);

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

    finevk::CameraState camState = makeFixedCamera();
    renderer.updateCamera(camState, glm::dvec3(0.0, 0.0, 8.0));
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

} // anonymous namespace

TEST(VisualSparks, GoldenBasic) {
    HeadlessCtx ctx = makeHeadlessContext();
    CapturedFrame frame = renderOneFrame(ctx);
    ASSERT_EQ(frame.rgba.size(),
              static_cast<size_t>(kCaptureWidth) * kCaptureHeight * 4);

    const std::string goldenPath =
        std::string(FINEVOX_SOURCE_DIR) + "/tests/golden/sparks_basic.png";

    GoldenCompareResult r = VisualTestHarness::compareAgainstGolden(
        frame.rgba, frame.width, frame.height,
        goldenPath, kMaxAbsDiffPerChannel);

    std::cerr << "[VisualSparks] tolerance=" << int(kMaxAbsDiffPerChannel)
              << " maxAbsDiff=" << int(r.maxAbsDiff)
              << " failingPixels=" << r.failingPixelCount
              << " goldenPath=" << goldenPath << "\n";

    if (!r.passed) {
        // Drop a "last actual" PNG next to the golden so the failure is easy
        // to inspect without re-running the test.
        const std::string lastActual =
            std::string(FINEVOX_SOURCE_DIR) + "/tests/golden/sparks_basic.actual.png";
        FrameCapture::savePNG(lastActual, frame.width, frame.height, frame.rgba);
        std::cerr << "[VisualSparks] wrote last actual to " << lastActual << "\n";
    }

    EXPECT_TRUE(r.passed) << "reason: " << r.failureReason
                           << " firstFail=(" << r.firstFailX << ","
                           << r.firstFailY << ")";
}

TEST(VisualSparks, DeterministicAcrossRuns) {
    // Render the same scene twice in the same process and verify the two
    // captures are byte-identical.  This catches nondeterminism sources
    // (unsorted iteration, frame-index state leaking between runs, etc.)
    // independent of whether the golden matches.
    HeadlessCtx ctx = makeHeadlessContext();
    CapturedFrame a = renderOneFrame(ctx);
    // Reset the OffscreenSurface before the second run so its internal
    // single-buffered state starts fresh.  Building a second ctx also works
    // but would double the test's Vulkan-init cost; resize-to-same-size is a
    // cheap way to drive OffscreenSurface through its recreate path.
    ctx.surface->resize(kSurfaceWidth, kSurfaceHeight);
    CapturedFrame b = renderOneFrame(ctx);

    ASSERT_EQ(a.rgba.size(), b.rgba.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        if (a.rgba[i] != b.rgba[i]) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0u)
        << "same-process rerun produced " << mismatches
        << " mismatched bytes — scene is not deterministic";
}
