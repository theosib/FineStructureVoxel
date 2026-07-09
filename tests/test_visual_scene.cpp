/**
 * @file test_visual_scene.cpp
 * @brief Tier-2 golden-image visual test — a constructed voxel scene rendered
 *        through WorldRenderer into a headless OffscreenSurface.
 *
 * Where test_visual_sparks.cpp exercises the particle billboard pipeline, this
 * test exercises the *world* render path: block meshing (face culling + greedy
 * merge), ambient occlusion, per-face shading, and the WorldRenderer →
 * RenderSurface generalization introduced in phase 2b (WorldRenderer now
 * accepts any finevk::RenderSurface, so an OffscreenSurface can drive it
 * headlessly exactly like a SimpleRenderer drives the window).
 *
 * The single curated case (VisualScene.GoldenBasicBlocks) constructs a tiny
 * deterministic world — a stone platform with a few blocks stacked on top —
 * fixes the camera pose, drives the async mesh pipeline to quiescence
 * synchronously, renders one frame, and byte-compares a 96×96 ROI readback
 * against tests/golden/scene_basic_blocks.png.
 *
 * Determinism: no light provider is set and smooth/flat lighting is left off,
 * so MeshBuilder emits constant full sky-light (1.0) and zero block-light for
 * every vertex — there is no BFS light propagation to settle.  AO is a pure
 * function of the (fixed) block neighbourhood.  Greedy meshing merges faces
 * deterministically.  Fog is disabled (fogStart >= fogEnd → shader returns 0).
 * The mesh worker pool is non-deterministic in *ordering* only; the final
 * per-subchunk mesh content is order-independent, and each subchunk uploads
 * exactly one mesh, so the composited frame is identical every run.  The
 * DeterministicAcrossRuns companion re-renders in-process and byte-compares to
 * prove this independently of the committed golden.
 *
 * On first run (or an intentional visual change) mint the golden with:
 *
 *     FINEVOX_UPDATE_GOLDENS=1 ./finevox_render_tests \
 *         --gtest_filter='VisualScene.*'
 *
 * then re-run without the env var to confirm it passes against the commit.
 */

#include <gtest/gtest.h>

#include "finevox/core/block_type.hpp"
#include "finevox/core/world.hpp"
#include "finevox/render/block_atlas.hpp"
#include "finevox/render/frame_capture.hpp"
#include "finevox/render/visual_test_harness.hpp"
#include "finevox/render/world_renderer.hpp"

#include <finevk/core/instance.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/physical_device.hpp>
#include <finevk/device/staging_pool.hpp>
#include <finevk/engine/camera.hpp>
#include <finevk/rendering/sync.hpp>          // Full Fence type for OffscreenSurface member
#include <finevk/rendering/offscreen_surface.hpp>

#include <glm/glm.hpp>

#include <chrono>
#include <string>
#include <thread>

#ifndef FINEVOX_SHADER_DIR
#define FINEVOX_SHADER_DIR "shaders"
#endif
#ifndef FINEVOX_SOURCE_DIR
#define FINEVOX_SOURCE_DIR "."
#endif

using namespace finevox;
using namespace finevox::render;

namespace {

// Absolute per-channel byte tolerance.  The world render path is a fixed-
// function-ish rasterization of a static mesh: no RNG, no light BFS.  We keep
// the same modest tolerance the sparks tests use (4) to absorb any GLSL
// compiler / driver rounding drift rather than assuming a hard 0.
constexpr uint8_t kMaxAbsDiffPerChannel = 4;

constexpr uint32_t kCaptureWidth = 96;
constexpr uint32_t kCaptureHeight = 96;
constexpr uint32_t kSurfaceWidth = 256;
constexpr uint32_t kSurfaceHeight = 256;

struct HeadlessCtx {
    finevk::InstancePtr instance;
    finevk::PhysicalDevice physicalDevice;   // LogicalDevice borrows a pointer
    finevk::LogicalDevicePtr device;
    finevk::StagingPoolPtr stagingPool;
    std::unique_ptr<finevk::OffscreenSurface> surface;
};

HeadlessCtx makeHeadlessContext() {
    HeadlessCtx c;
    c.instance = finevk::Instance::create()
        .applicationName("finevox_visual_scene_test")
        .headless()
        .build();
    c.physicalDevice = c.instance->selectPhysicalDevice();
    // Enable anisotropy exactly as render::Engine does (engine.cpp).  The
    // block-atlas sampler WorldRenderer builds mirrors SimpleRenderer's
    // defaultSampler(), which turns on anisotropic filtering when the device
    // supports it; without enabling the feature on this headless device the
    // sampler's anisotropyEnable=VK_TRUE would trip a validation error and the
    // filtering would differ from window rendering.
    c.device = c.physicalDevice.createLogicalDevice()
        .enableAnisotropy()
        .build();
    c.stagingPool = finevk::StagingPool::create(c.device.get())
        .initialSize(4 * 1024 * 1024)
        .build();
    c.surface = finevk::OffscreenSurface::create(c.device.get())
        .extent(kSurfaceWidth, kSurfaceHeight)
        .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)   // UNORM = trivial byte round-trip
        .enableDepth()                            // depth needed for occlusion
        .build();
    return c;
}

// Register the block types the scene uses.  registerType() is a no-op if the
// name is already present, so this is safe to call once per test body.  All
// are standard opaque cubes (default BlockType) — no custom geometry, so they
// take the greedy-meshing + AO path (see docs/PATTERNS.md: standard cubes must
// NOT carry geometry).
void registerSceneBlocks() {
    BlockRegistry& reg = BlockRegistry::global();
    for (const char* name : {"scene_test:stone", "scene_test:dirt",
                             "scene_test:grass", "scene_test:cobble"}) {
        BlockTypeId id = BlockTypeId::fromName(name);
        if (!reg.hasType(id)) {
            reg.registerType(id, BlockType{});   // default: opaque full cube
        }
    }
}

// Build a small deterministic world: an 8×8 stone platform at y=0 with a few
// blocks stacked on top to give the mesher varied faces / AO corners.
void buildScene(World& world) {
    const BlockTypeId stone  = BlockTypeId::fromName("scene_test:stone");
    const BlockTypeId dirt   = BlockTypeId::fromName("scene_test:dirt");
    const BlockTypeId grass  = BlockTypeId::fromName("scene_test:grass");
    const BlockTypeId cobble = BlockTypeId::fromName("scene_test:cobble");

    // 8×8 platform floor.
    for (int x = 0; x < 8; ++x) {
        for (int z = 0; z < 8; ++z) {
            world.setBlock(x, 0, z, stone);
        }
    }
    // A little staircase / cluster on top so faces and AO corners appear.
    world.setBlock(3, 1, 3, dirt);
    world.setBlock(4, 1, 3, dirt);
    world.setBlock(4, 1, 4, grass);
    world.setBlock(4, 2, 4, cobble);
    world.setBlock(2, 1, 5, cobble);
}

// Drive the async mesh pipeline to quiescence synchronously.
//
// WorldRenderer pushes rebuild requests to MeshRebuildQueue; the worker pool
// builds meshes on background threads and pushes results to the upload queue;
// updateMeshes() drains the upload queue and uploads to the GPU.  In the
// interactive loop this is spread across frames.  Here we loop until:
//   - the input rebuild queue is drained (no work left to schedule),
//   - the upload queue is empty (nothing pending),
//   - and the expected number of subchunk views have meshes.
// bounded by a wall-clock timeout so a stall fails loudly instead of hanging.
void pumpMeshesToQuiescence(WorldRenderer& renderer, World& world) {
    renderer.rebuildAllMeshes();

    const size_t expectedViews = world.getAllSubChunkPositions().size();
    auto* pool = renderer.meshWorkerPool();
    auto* queue = renderer.meshRebuildQueue();
    ASSERT_NE(pool, nullptr);
    ASSERT_NE(queue, nullptr);

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);

    // Track consecutive "settled" iterations: everything drained AND all views
    // present.  We require a couple of clean passes in a row so a momentarily-
    // empty upload queue (worker mid-build) doesn't fool us into stopping early.
    int settledPasses = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        renderer.updateMeshes();  // drain whatever workers have produced

        const bool inputDrained = queue->empty();
        const bool uploadsDrained = pool->uploadQueueSize() == 0;
        const bool allViews = renderer.loadedMeshCount() >= expectedViews;

        if (inputDrained && uploadsDrained && allViews) {
            if (++settledPasses >= 3) return;   // stable → done
        } else {
            settledPasses = 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    FAIL() << "mesh pipeline did not reach quiescence within timeout "
           << "(expectedViews=" << expectedViews
           << " loaded=" << renderer.loadedMeshCount()
           << " uploadQueue=" << pool->uploadQueueSize() << ")";
}

// Fixed camera pose looking down at the platform from an off-axis corner so
// tops, sides, and AO all show.
finevk::CameraState makeCamera() {
    finevk::Camera cam;
    cam.setPerspective(/*fov=*/55.0f,
                       /*aspect=*/float(kSurfaceWidth) / kSurfaceHeight,
                       /*near=*/0.1f, /*far=*/200.0f);
    cam.moveTo(glm::vec3(11.0f, 8.0f, 12.0f));
    cam.lookAt(glm::vec3(4.0f, 1.0f, 4.0f));
    cam.updateState();
    return cam.state();
}

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

CapturedFrame renderScene(HeadlessCtx& ctx) {
    registerSceneBlocks();

    World world;
    buildScene(world);

    WorldRendererConfig cfg;
    cfg.viewDistance = 512.0f;   // everything visible
    cfg.fog.enabled = false;     // deterministic: no distance fog

    // Pass the OffscreenSurface as a RenderSurface — the phase-2b
    // generalization.  Previously this parameter required a SimpleRenderer.
    WorldRenderer renderer(ctx.device.get(), ctx.surface.get(), world, cfg);
    renderer.loadShaders(std::string(FINEVOX_SHADER_DIR) + "/chunk.vert.spv",
                         std::string(FINEVOX_SHADER_DIR) + "/chunk.frag.spv");

    // Placeholder solid-color atlas; assign distinct cells per block type so
    // the composited frame has color variety (helps a human eyeball the golden).
    BlockAtlas atlas;
    atlas.createPlaceholderAtlas(ctx.device.get(),
                                 ctx.device->defaultCommandPool(), 16, 16);
    atlas.setBlockTexture(BlockTypeId::fromName("scene_test:stone"), 0, 0);
    atlas.setBlockTexture(BlockTypeId::fromName("scene_test:dirt"), 1, 0);
    atlas.setBlockTexture(BlockTypeId::fromName("scene_test:grass"), 2, 0);
    atlas.setBlockTexture(BlockTypeId::fromName("scene_test:cobble"), 3, 0);
    renderer.setBlockAtlas(atlas.texture());
    renderer.setTextureProvider(atlas.createProvider());
    renderer.initialize();

    // Fixed sky: sun straight down, full brightness, no fog.  No light
    // provider → MeshBuilder bakes constant sky-light, nothing to settle.
    SkyParameters sky{};
    sky.skyColor = glm::vec4(0.5f, 0.7f, 1.0f, 1.0f);
    sky.fogColor = glm::vec3(0.5f, 0.7f, 1.0f);
    sky.sunDirection = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
    sky.skyBrightness = 1.0f;
    sky.ambientLevel = 0.4f;
    sky.sunIntensity = 1.0f;
    renderer.setSkyParameters(sky);

    finevk::CameraState camState = makeCamera();
    renderer.updateCamera(camState, glm::dvec3(11.0, 8.0, 12.0));

    pumpMeshesToQuiescence(renderer, world);
    // Refresh camera uniform against the surface's (single) frame slot after
    // meshes are up — harmless, keeps the uniform current for the draw.
    renderer.updateCamera(camState, glm::dvec3(11.0, 8.0, 12.0));

    ctx.surface->beginFrame();
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

TEST(VisualScene, GoldenBasicBlocks) {
    HeadlessCtx ctx = makeHeadlessContext();
    CapturedFrame frame = renderScene(ctx);
    ASSERT_EQ(frame.rgba.size(),
              static_cast<size_t>(kCaptureWidth) * kCaptureHeight * 4);

    const std::string goldenPath =
        std::string(FINEVOX_SOURCE_DIR) + "/tests/golden/scene_basic_blocks.png";

    GoldenCompareResult r = VisualTestHarness::compareAgainstGolden(
        frame.rgba, frame.width, frame.height, goldenPath,
        kMaxAbsDiffPerChannel);

    std::cerr << "[VisualScene] golden=scene_basic_blocks"
              << " tolerance=" << int(kMaxAbsDiffPerChannel)
              << " maxAbsDiff=" << int(r.maxAbsDiff)
              << " failingPixels=" << r.failingPixelCount << "\n";

    if (!r.passed) {
        const std::string lastActual =
            std::string(FINEVOX_SOURCE_DIR) +
            "/tests/golden/scene_basic_blocks.actual.png";
        FrameCapture::savePNG(lastActual, frame.width, frame.height, frame.rgba);
        std::cerr << "[VisualScene] wrote last actual to " << lastActual << "\n";
    }

    EXPECT_TRUE(r.passed) << "reason: " << r.failureReason
                          << " firstFail=(" << r.firstFailX << ","
                          << r.firstFailY << ")";
}

TEST(VisualScene, DeterministicAcrossRuns) {
    // Render the same scene twice (fresh context each time to avoid any
    // per-surface state carryover) and byte-compare.  Proves the world render
    // path is deterministic independent of the committed golden — catches
    // mesh-ordering or light-settling nondeterminism if it were ever
    // introduced.
    HeadlessCtx ctxA = makeHeadlessContext();
    CapturedFrame a = renderScene(ctxA);

    HeadlessCtx ctxB = makeHeadlessContext();
    CapturedFrame b = renderScene(ctxB);

    ASSERT_EQ(a.rgba.size(), b.rgba.size());
    size_t mismatches = 0;
    for (size_t i = 0; i < a.rgba.size(); ++i) {
        if (a.rgba[i] != b.rgba[i]) ++mismatches;
    }
    EXPECT_EQ(mismatches, 0u)
        << "world-render rerun produced " << mismatches
        << " mismatched bytes — scene is not deterministic";
}
