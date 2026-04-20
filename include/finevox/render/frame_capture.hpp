#pragma once

/**
 * @file frame_capture.hpp
 * @brief GPU frame → PNG capture service.
 *
 * Reads rendered pixels back from a finevk `Image` via
 * `Image::readbackToCPU` / `readbackRegionToCPU` and writes them to disk as
 * RGBA PNG via vendored `stb_image_write`.
 *
 * Two entry-point shapes share the same underlying readback:
 *
 *  - `finevk::Image&` primary API — for any image (swap-chain wrapper,
 *    offscreen colour attachment, ad-hoc render target).  Used by the F12
 *    screenshot hotkey path (swap-chain image in PRESENT_SRC_KHR) and
 *    directly by any future consumer that already holds an `Image&`.
 *
 *  - `finevk::OffscreenSurface&` convenience overloads — thin one-liners
 *    that forward to the `Image&` form using
 *    `OffscreenSurface::colorImage()`.  Used by the golden-image test
 *    harness and the first generation of headless capture tests.
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
class Image;
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

    // =========================================================================
    // Primary API — Image&
    //
    // `layout` must match the image's current Vulkan layout.  Common values:
    //   - VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL — OffscreenSurface colour
    //     attachment after `endFrame()`.
    //   - VK_IMAGE_LAYOUT_PRESENT_SRC_KHR — swap-chain image after the
    //     renderer's `endFrame()` has presented it.
    //
    // BGRA formats are swizzled to RGBA in-place before return/write.
    // =========================================================================

    /// Read back the full image and save as PNG.  Returns true on success.
    static bool captureImage(finevk::Image& image,
                              finevk::StagingPool& pool,
                              const std::string& path,
                              VkImageLayout layout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Sub-rectangle variant.  If `rect` is empty the full image is captured.
    static bool captureImageRegion(finevk::Image& image,
                                    finevk::StagingPool& pool,
                                    const CaptureRect& rect,
                                    const std::string& path,
                                    VkImageLayout layout =
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    /// Read back pixels into `out` without writing to disk.  Handles BGRA →
    /// RGBA swizzle so callers always receive RGBA8.  `outWidth`/`outHeight`
    /// receive the final dimensions.  Returns false on failure.
    static bool readbackRGBA(finevk::Image& image,
                              finevk::StagingPool& pool,
                              const CaptureRect& rect,
                              std::vector<uint8_t>& out,
                              uint32_t& outWidth, uint32_t& outHeight,
                              VkImageLayout layout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // =========================================================================
    // Convenience overloads — OffscreenSurface&
    //
    // Forward to the Image& primary API using `surface.colorImage()`.
    // =========================================================================

    static bool captureSurface(finevk::OffscreenSurface& surface,
                                finevk::StagingPool& pool,
                                const std::string& path,
                                VkImageLayout layout =
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    static bool captureSurfaceRegion(finevk::OffscreenSurface& surface,
                                      finevk::StagingPool& pool,
                                      const CaptureRect& rect,
                                      const std::string& path,
                                      VkImageLayout layout =
                                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    static bool readbackRGBA(finevk::OffscreenSurface& surface,
                              finevk::StagingPool& pool,
                              const CaptureRect& rect,
                              std::vector<uint8_t>& out,
                              uint32_t& outWidth, uint32_t& outHeight,
                              VkImageLayout layout =
                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
};

} // namespace finevox::render
