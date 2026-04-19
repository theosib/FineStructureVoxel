#include "finevox/render/visual_test_harness.hpp"
#include "finevox/render/frame_capture.hpp"

// stb_image — declarations only.  The IMPLEMENTATION symbols are already
// provided by finevk's texture loader; we reuse the same header from the
// finevk external/ tree (include path is set in the CMake target).
#include <stb_image.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace finevox::render {

bool VisualTestHarness::loadPNG(const std::string& path,
                                 std::vector<uint8_t>& rgba,
                                 uint32_t& width, uint32_t& height)
{
    int w = 0, h = 0, ch = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &ch, /*desired=*/4);
    if (!data) {
        return false;
    }
    rgba.assign(data, data + (w * h * 4));
    width = static_cast<uint32_t>(w);
    height = static_cast<uint32_t>(h);
    stbi_image_free(data);
    return true;
}

bool VisualTestHarness::goldenUpdateRequested() {
    const char* env = std::getenv("FINEVOX_UPDATE_GOLDENS");
    return env && std::strcmp(env, "1") == 0;
}

GoldenCompareResult VisualTestHarness::compareAgainstGolden(
    const std::vector<uint8_t>& actual,
    uint32_t actualWidth, uint32_t actualHeight,
    const std::string& goldenPath,
    uint8_t maxAbsDiffPerChannel)
{
    GoldenCompareResult r;
    r.width = actualWidth;
    r.height = actualHeight;

    const size_t expectedBytes =
        static_cast<size_t>(actualWidth) * actualHeight * 4;
    if (actual.size() != expectedBytes) {
        r.failureReason = "size-mismatch (actual buffer does not match w*h*4)";
        return r;
    }

    namespace fs = std::filesystem;
    const bool goldenExists = fs::exists(goldenPath);

    if (!goldenExists) {
        if (goldenUpdateRequested()) {
            if (FrameCapture::savePNG(goldenPath, actualWidth, actualHeight,
                                      actual)) {
                r.passed = true;
                r.failureReason = "golden-written";
                std::cerr << "[VisualTestHarness] wrote new golden to "
                          << goldenPath << "\n";
                return r;
            }
            r.failureReason = "golden-write-failed";
            return r;
        }
        r.failureReason =
            "missing-golden (set FINEVOX_UPDATE_GOLDENS=1 to create it)";
        return r;
    }

    std::vector<uint8_t> golden;
    uint32_t gw = 0, gh = 0;
    if (!loadPNG(goldenPath, golden, gw, gh)) {
        r.failureReason = "load-error (could not decode golden PNG)";
        return r;
    }

    if (gw != actualWidth || gh != actualHeight) {
        r.failureReason = "size-mismatch (golden " + std::to_string(gw) + "x" +
                          std::to_string(gh) + " vs actual " +
                          std::to_string(actualWidth) + "x" +
                          std::to_string(actualHeight) + ")";
        if (goldenUpdateRequested()) {
            FrameCapture::savePNG(goldenPath, actualWidth, actualHeight, actual);
            r.passed = true;
            r.failureReason = "golden-written (size changed)";
        }
        return r;
    }

    // Byte-wise tolerance sweep.
    uint32_t failingPixels = 0;
    bool firstFailRecorded = false;
    for (uint32_t y = 0; y < actualHeight; ++y) {
        for (uint32_t x = 0; x < actualWidth; ++x) {
            const size_t base = (static_cast<size_t>(y) * actualWidth + x) * 4;
            uint8_t maxChan = 0;
            for (int c = 0; c < 4; ++c) {
                const int da = static_cast<int>(actual[base + c]);
                const int dg = static_cast<int>(golden[base + c]);
                const int d = da > dg ? da - dg : dg - da;
                if (d > maxChan) maxChan = static_cast<uint8_t>(d);
            }
            if (maxChan > r.maxAbsDiff) r.maxAbsDiff = maxChan;
            if (maxChan > maxAbsDiffPerChannel) {
                ++failingPixels;
                if (!firstFailRecorded) {
                    r.firstFailX = x;
                    r.firstFailY = y;
                    firstFailRecorded = true;
                }
            }
        }
    }

    r.failingPixelCount = failingPixels;
    if (failingPixels == 0) {
        r.passed = true;
        return r;
    }

    r.failureReason = "tolerance-exceeded (" + std::to_string(failingPixels) +
                       " pixels > " +
                       std::to_string(static_cast<int>(maxAbsDiffPerChannel)) +
                       "/channel)";
    if (goldenUpdateRequested()) {
        FrameCapture::savePNG(goldenPath, actualWidth, actualHeight, actual);
        r.passed = true;
        r.failureReason += " — golden overwritten (FINEVOX_UPDATE_GOLDENS=1)";
    }
    return r;
}

} // namespace finevox::render
