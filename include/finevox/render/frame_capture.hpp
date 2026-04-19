#pragma once

/**
 * @file frame_capture.hpp
 * @brief GPU frame → PNG capture service.
 *
 * Reads rendered pixels back from an `OffscreenSurface` via finevk's
 * `Image::readbackToCPU` / `readbackRegionToCPU` and writes them to disk as
 * RGBA PNG via vendored `stb_image_write`.
 *
 * This is the reusable primitive underneath both the F12 screenshot hotkey
 * (shipped in a later phase, once a swapchain-TRANSFER_SRC path lands in
 * finevk) and the golden-image visual test harness.  It intentionally knows
 * nothing about the scene being rendered — callers hand it a surface that's
 * already had a frame submitted and it produces the PNG.
 *
 * Supports the four finevk readback-compatible formats:
 * `R8G8B8A8_{UNORM,SRGB}` and `B8G8R8A8_{UNORM,SRGB}`.  BGRA byte orders are
 * swizzled to RGBA in-place before writing so the resulting PNG always opens
 * as standard 8-bit RGBA in any image viewer.
 */

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan.h>

namespace finevk {
class OffscreenSurface;
class StagingPool;
} // namespace finevk

namespace finevox::render {

struct CaptureRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool empty() const { return width == 0 || height == 0; }
};

class FrameCapture {
public:
    /// Write raw RGBA8 (row-major, tightly packed) bytes to a PNG file.
    /// `rgba.size()` must equal `width * height * 4`.  Creates missing
    /// parent directories.  Returns true on success.
    static bool savePNG(const std::string& path,
                         uint32_t width, uint32_t height,
                         const std::vector<uint8_t>& rgba);

    /// Raw-pointer variant for callers that already own a buffer.
    static bool savePNG(const std::string& path,
                         uint32_t width, uint32_t height,
                         const uint8_t* rgba);

    /// Build a "<prefix>YYYYMMDD-HHMMSS<ext>" filename from the system clock.
    /// Example: `timestampedName("screenshots/", ".png")` →
    /// `"screenshots/20260419-143012.png"`.
    static std::string timestampedName(std::string_view prefix,
                                        std::string_view ext);

    /// Read back the full color image from `surface` and save as PNG.
    /// `layout` must match the image's current layout (defaults to
    /// SHADER_READ_ONLY_OPTIMAL, which is what OffscreenSurface leaves its
    /// color attachment in after `endFrame()`).  BGRA formats are swizzled
    /// in-place before writing.  Returns true on success.
    static bool captureSurface(finevk::OffscreenSurface& surface,
                                finevk::StagingPool& pool,
                                const std::string& path,
                                VkImageLayout layout =
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Sub-rectangle variant.  If `rect` is empty the full image is captured.
    static bool captureSurfaceRegion(finevk::OffscreenSurface& surface,
                                      finevk::StagingPool& pool,
                                      const CaptureRect& rect,
                                      const std::string& path,
                                      VkImageLayout layout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Read back pixels into `out` without writing to disk.  Handles BGRA →
    /// RGBA swizzle so callers always receive RGBA8.  Returns false on failure
    /// (empty rect that isn't full, readback throw propagated as false).
    /// `outWidth`/`outHeight` receive the final dimensions.
    static bool readbackRGBA(finevk::OffscreenSurface& surface,
                              finevk::StagingPool& pool,
                              const CaptureRect& rect,
                              std::vector<uint8_t>& out,
                              uint32_t& outWidth, uint32_t& outHeight,
                              VkImageLayout layout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};

} // namespace finevox::render
