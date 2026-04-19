#pragma once

/**
 * @file visual_test_harness.hpp
 * @brief Golden-image compare helpers for headless rendering tests.
 *
 * A visual test renders a deterministic scene into an `OffscreenSurface`,
 * reads the ROI back (via `FrameCapture::readbackRGBA`), and compares the
 * result against a committed PNG on disk.  This header owns only the
 * load-and-compare half; scene construction is the test author's job.
 *
 * Tolerance is **per-channel absolute byte difference**.  No SSIM / perceptual
 * metrics — failures cite both the worst per-pixel diff and the first
 * divergent pixel so drift shows up in the test log.
 *
 * Missing-golden policy: loud failure by default.  Running with the
 * `FINEVOX_UPDATE_GOLDENS=1` env var writes the actual pixels out as the new
 * golden and passes — use this only when intentionally reseeding the oracle.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace finevox::render {

struct GoldenCompareResult {
    bool passed = false;

    uint32_t width = 0;
    uint32_t height = 0;

    // Worst-case absolute byte diff across all channels and pixels, 0..255.
    uint8_t maxAbsDiff = 0;

    // Coordinates of the first pixel whose per-channel diff exceeded
    // tolerance.  Valid only when `passed` is false and the sizes match.
    uint32_t firstFailX = 0;
    uint32_t firstFailY = 0;

    // Human-readable failure reason: "size-mismatch", "missing-golden",
    // "load-error", "tolerance-exceeded", or empty when `passed`.
    std::string failureReason;

    // Count of pixels whose any-channel diff exceeded tolerance.
    uint32_t failingPixelCount = 0;
};

class VisualTestHarness {
public:
    /// Load `path` as RGBA8 via stb_image.  Returns true on success.  Does
    /// not perform any gamma correction or format conversion — whatever the
    /// PNG stores is what comes back (RGBA8 forced).
    static bool loadPNG(const std::string& path,
                         std::vector<uint8_t>& rgba,
                         uint32_t& width, uint32_t& height);

    /// Compare `actualRGBA` (width*height*4 bytes, RGBA8) against the PNG at
    /// `goldenPath`.  `maxAbsDiffPerChannel` is the tolerance ceiling — any
    /// pixel whose worst-channel absolute byte delta exceeds this fails.
    ///
    /// If the golden is missing and the `FINEVOX_UPDATE_GOLDENS` env var is
    /// set to "1", writes `actualRGBA` out as the new golden and returns
    /// `passed = true` with `failureReason = "golden-written"`.  In all other
    /// "golden missing" cases returns `passed = false` with a clear reason.
    static GoldenCompareResult compareAgainstGolden(
        const std::vector<uint8_t>& actualRGBA,
        uint32_t actualWidth, uint32_t actualHeight,
        const std::string& goldenPath,
        uint8_t maxAbsDiffPerChannel);

    /// Returns true when `FINEVOX_UPDATE_GOLDENS=1` is set in the environment.
    static bool goldenUpdateRequested();
};

} // namespace finevox::render
