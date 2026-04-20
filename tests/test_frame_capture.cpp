/**
 * @file test_frame_capture.cpp
 * @brief Regression coverage for FrameCapture's Image&-primary overload.
 *
 * Phase 1d generalized FrameCapture from an OffscreenSurface&-only API to
 * take finevk::Image& as the primary entry point (the form used by the F12
 * screenshot hotkey against a swap-chain image).  test_visual_sparks exercises
 * the OffscreenSurface& forwarder path end-to-end; this test exercises the
 * Image& primary path directly using an OffscreenSurface's colorImage() as the
 * source Image — no particle rendering, no golden, just readback + PNG encode.
 */

#include <gtest/gtest.h>

#include "finevox/render/frame_capture.hpp"

#include <finevk/core/instance.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/image.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/physical_device.hpp>
#include <finevk/device/sampler.hpp>
#include <finevk/device/staging_pool.hpp>
#include <finevk/rendering/sync.hpp>
#include <finevk/rendering/offscreen_surface.hpp>

#include <filesystem>
#include <vector>

using namespace finevox::render;

namespace {

struct HeadlessCtx {
    finevk::InstancePtr instance;
    finevk::PhysicalDevice physicalDevice;
    finevk::LogicalDevicePtr device;
    finevk::StagingPoolPtr stagingPool;
    std::unique_ptr<finevk::OffscreenSurface> surface;
};

HeadlessCtx makeHeadlessContext(uint32_t width, uint32_t height) {
    HeadlessCtx c;
    c.instance = finevk::Instance::create()
        .applicationName("finevox_frame_capture_test")
        .headless()
        .build();
    c.physicalDevice = c.instance->selectPhysicalDevice();
    c.device = c.physicalDevice.createLogicalDevice().build();
    c.stagingPool = finevk::StagingPool::create(c.device.get())
        .initialSize(1 * 1024 * 1024)
        .build();
    c.surface = finevk::OffscreenSurface::create(c.device.get())
        .extent(width, height)
        .colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
        .enableDepth()
        .build();
    return c;
}

// Render a single clear-only frame so the color attachment has a known
// layout/content, then return.
void clearFrame(finevk::OffscreenSurface& surface,
                 float r, float g, float b, float a)
{
    surface.beginFrame();
    surface.beginRenderPass({r, g, b, a}, 1.0f);
    surface.endRenderPass();
    surface.endFrame();
}

} // anonymous namespace

TEST(FrameCaptureImageAPI, FullReadbackViaImageOverload) {
    HeadlessCtx ctx = makeHeadlessContext(32, 32);
    clearFrame(*ctx.surface, 0.25f, 0.5f, 0.75f, 1.0f);

    finevk::Image* img = ctx.surface->colorImage();
    ASSERT_NE(img, nullptr);

    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    const bool ok = FrameCapture::readbackRGBA(
        *img, *ctx.stagingPool, CaptureRect{}, rgba, w, h,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ASSERT_TRUE(ok);
    EXPECT_EQ(w, 32u);
    EXPECT_EQ(h, 32u);
    ASSERT_EQ(rgba.size(), 32u * 32u * 4u);

    // Clear colour 0.25 / 0.5 / 0.75 in UNORM → ~64 / 128 / 191.
    // Sample a centre pixel to confirm the readback landed sensible bytes.
    const size_t centre = ((16u * 32u) + 16u) * 4u;
    EXPECT_NEAR(rgba[centre + 0], 64,  2);
    EXPECT_NEAR(rgba[centre + 1], 128, 2);
    EXPECT_NEAR(rgba[centre + 2], 191, 2);
    EXPECT_EQ(rgba[centre + 3], 255);
}

TEST(FrameCaptureImageAPI, RegionReadbackViaImageOverload) {
    HeadlessCtx ctx = makeHeadlessContext(64, 64);
    clearFrame(*ctx.surface, 1.0f, 0.0f, 0.0f, 1.0f);

    finevk::Image* img = ctx.surface->colorImage();
    ASSERT_NE(img, nullptr);

    CaptureRect roi{8, 8, 16, 16};
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    const bool ok = FrameCapture::readbackRGBA(
        *img, *ctx.stagingPool, roi, rgba, w, h,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ASSERT_TRUE(ok);
    EXPECT_EQ(w, 16u);
    EXPECT_EQ(h, 16u);
    EXPECT_EQ(rgba.size(), 16u * 16u * 4u);

    // Every pixel should be solid red.
    for (size_t i = 0; i < rgba.size(); i += 4) {
        EXPECT_EQ(rgba[i + 0], 255);
        EXPECT_EQ(rgba[i + 1], 0);
        EXPECT_EQ(rgba[i + 2], 0);
        EXPECT_EQ(rgba[i + 3], 255);
    }
}

TEST(FrameCaptureImageAPI, CaptureImageWritesPNG) {
    HeadlessCtx ctx = makeHeadlessContext(16, 16);
    clearFrame(*ctx.surface, 0.0f, 1.0f, 0.0f, 1.0f);

    finevk::Image* img = ctx.surface->colorImage();
    ASSERT_NE(img, nullptr);

    namespace fs = std::filesystem;
    const fs::path out = fs::temp_directory_path() / "finevox_frame_capture_test.png";
    fs::remove(out);  // ensure we observe a fresh write

    const bool ok = FrameCapture::captureImage(
        *img, *ctx.stagingPool, out.string(),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    ASSERT_TRUE(ok);
    EXPECT_TRUE(fs::exists(out));
    EXPECT_GT(fs::file_size(out), 0u);

    fs::remove(out);
}
