#include "finevox/render/frame_capture.hpp"

// stb_image_write: single-header PNG/BMP/TGA/JPEG encoder.  The
// IMPLEMENTATION macro must be defined in exactly one translation unit; we
// pick this one so the symbols land in libfinevox_render and nobody else has
// to think about it.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

// Full type includes BEFORE offscreen_surface.hpp because OffscreenSurface
// stores these as unique_ptr members with only forward declarations in its
// own header — the destructor site needs the full types.
#include <finevk/device/command.hpp>
#include <finevk/device/image.hpp>
#include <finevk/device/sampler.hpp>
#include <finevk/device/staging_pool.hpp>
#include <finevk/rendering/sync.hpp>
#include <finevk/rendering/offscreen_surface.hpp>

#include <chrono>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace finevox::render {

namespace {

bool isBGRA(VkFormat fmt) {
    return fmt == VK_FORMAT_B8G8R8A8_UNORM || fmt == VK_FORMAT_B8G8R8A8_SRGB;
}

void swizzleBGRAtoRGBA(std::vector<uint8_t>& pixels) {
    for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
        std::swap(pixels[i + 0], pixels[i + 2]);
    }
}

bool ensureParentDir(const std::string& path) {
    namespace fs = std::filesystem;
    try {
        fs::path p(path);
        if (p.has_parent_path()) {
            fs::create_directories(p.parent_path());
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FrameCapture] could not create parent dir for "
                  << path << ": " << e.what() << "\n";
        return false;
    }
}

} // anonymous namespace

bool FrameCapture::savePNG(const std::string& path,
                            uint32_t width, uint32_t height,
                            const std::vector<uint8_t>& rgba)
{
    if (rgba.size() != static_cast<size_t>(width) * height * 4) {
        std::cerr << "[FrameCapture] savePNG: buffer size " << rgba.size()
                  << " != expected " << (width * height * 4u) << "\n";
        return false;
    }
    return savePNG(path, width, height, rgba.data());
}

bool FrameCapture::savePNG(const std::string& path,
                            uint32_t width, uint32_t height,
                            const uint8_t* rgba)
{
    if (!rgba || width == 0 || height == 0) return false;
    if (!ensureParentDir(path)) return false;

    const int stride = static_cast<int>(width) * 4;
    const int ok = stbi_write_png(path.c_str(),
                                   static_cast<int>(width),
                                   static_cast<int>(height),
                                   4,                // RGBA
                                   rgba,
                                   stride);
    if (!ok) {
        std::cerr << "[FrameCapture] stbi_write_png failed for " << path << "\n";
        return false;
    }
    return true;
}

std::string FrameCapture::timestampedName(std::string_view prefix,
                                           std::string_view ext)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%04d%02d%02d-%02d%02d%02d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);

    std::string out;
    out.reserve(prefix.size() + 15 + ext.size());
    out.append(prefix);
    out.append(stamp);
    out.append(ext);
    return out;
}

// =============================================================================
// Image& primary API
// =============================================================================

bool FrameCapture::readbackRGBA(finevk::Image& image,
                                 finevk::StagingPool& pool,
                                 const CaptureRect& rect,
                                 std::vector<uint8_t>& out,
                                 uint32_t& outWidth, uint32_t& outHeight,
                                 VkImageLayout layout)
{
    try {
        if (rect.empty()) {
            image.readbackToCPU(out, &pool, layout);
            outWidth = image.width();
            outHeight = image.height();
        } else {
            image.readbackRegionToCPU(out, &pool,
                                       rect.x, rect.y, rect.width, rect.height,
                                       layout);
            outWidth = rect.width;
            outHeight = rect.height;
        }
    } catch (const std::exception& e) {
        std::cerr << "[FrameCapture] readback failed: " << e.what() << "\n";
        return false;
    }

    if (isBGRA(image.format())) {
        swizzleBGRAtoRGBA(out);
    }
    return true;
}

bool FrameCapture::captureImage(finevk::Image& image,
                                 finevk::StagingPool& pool,
                                 const std::string& path,
                                 VkImageLayout layout)
{
    return captureImageRegion(image, pool, CaptureRect{}, path, layout);
}

bool FrameCapture::captureImageRegion(finevk::Image& image,
                                       finevk::StagingPool& pool,
                                       const CaptureRect& rect,
                                       const std::string& path,
                                       VkImageLayout layout)
{
    std::vector<uint8_t> rgba;
    uint32_t w = 0, h = 0;
    if (!readbackRGBA(image, pool, rect, rgba, w, h, layout)) return false;
    return savePNG(path, w, h, rgba);
}

// =============================================================================
// OffscreenSurface& convenience overloads (forward to Image& primary API)
// =============================================================================

bool FrameCapture::readbackRGBA(finevk::OffscreenSurface& surface,
                                 finevk::StagingPool& pool,
                                 const CaptureRect& rect,
                                 std::vector<uint8_t>& out,
                                 uint32_t& outWidth, uint32_t& outHeight,
                                 VkImageLayout layout)
{
    auto* image = surface.colorImage();
    if (!image) return false;
    return readbackRGBA(*image, pool, rect, out, outWidth, outHeight, layout);
}

bool FrameCapture::captureSurface(finevk::OffscreenSurface& surface,
                                   finevk::StagingPool& pool,
                                   const std::string& path,
                                   VkImageLayout layout)
{
    auto* image = surface.colorImage();
    if (!image) return false;
    return captureImage(*image, pool, path, layout);
}

bool FrameCapture::captureSurfaceRegion(finevk::OffscreenSurface& surface,
                                         finevk::StagingPool& pool,
                                         const CaptureRect& rect,
                                         const std::string& path,
                                         VkImageLayout layout)
{
    auto* image = surface.colorImage();
    if (!image) return false;
    return captureImageRegion(*image, pool, rect, path, layout);
}

} // namespace finevox::render
