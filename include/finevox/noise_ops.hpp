/**
 * @file noise_ops.hpp
 * @brief Composable noise operations: fractal, warp, scale, combine
 *
 * Design: [27-world-generation.md] Section 27.2.4, 27.2.5
 *
 * All operations wrap Noise2D/Noise3D via unique_ptr, enabling
 * arbitrary composition. Example:
 *
 *   auto terrain = std::make_unique<FBMNoise2D>(
 *       std::make_unique<PerlinNoise2D>(seed), 6);
 */

#pragma once

#include "finevox/noise.hpp"
#include <cstdint>
#include <functional>
#include <memory>

namespace finevox {

// ============================================================================
// Fractal noise (octave stacking)
// ============================================================================

/// Fractal Brownian Motion — stacks octaves of noise for natural detail
class FBMNoise2D : public Noise2D {
public:
    /// @param base Base noise source (takes ownership)
    /// @param octaves Number of octaves to stack (default 6)
    /// @param lacunarity Frequency multiplier per octave (default 2.0)
    /// @param persistence Amplitude multiplier per octave (default 0.5)
    FBMNoise2D(std::unique_ptr<Noise2D> base, int octaves = 6,
               float lacunarity = 2.0f, float persistence = 0.5f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> base_;
    int octaves_;
    float lacunarity_;
    float persistence_;
};

/// FBM for 3D noise
class FBMNoise3D : public Noise3D {
public:
    FBMNoise3D(std::unique_ptr<Noise3D> base, int octaves = 6,
               float lacunarity = 2.0f, float persistence = 0.5f);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::unique_ptr<Noise3D> base_;
    int octaves_;
    float lacunarity_;
    float persistence_;
};

/// Ridged multi-fractal noise — sharp ridges for mountains
class RidgedNoise2D : public Noise2D {
public:
    RidgedNoise2D(std::unique_ptr<Noise2D> base, int octaves = 6,
                  float lacunarity = 2.0f, float gain = 0.5f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> base_;
    int octaves_;
    float lacunarity_;
    float gain_;
    float maxValue_;  ///< Precomputed max for normalization
};

/// Ridged multi-fractal 3D
class RidgedNoise3D : public Noise3D {
public:
    RidgedNoise3D(std::unique_ptr<Noise3D> base, int octaves = 6,
                  float lacunarity = 2.0f, float gain = 0.5f);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::unique_ptr<Noise3D> base_;
    int octaves_;
    float lacunarity_;
    float gain_;
    float maxValue_;  ///< Precomputed max for normalization
};

/// Billow noise — absolute value of each octave, puffy appearance
class BillowNoise2D : public Noise2D {
public:
    BillowNoise2D(std::unique_ptr<Noise2D> base, int octaves = 6,
                  float lacunarity = 2.0f, float persistence = 0.5f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> base_;
    int octaves_;
    float lacunarity_;
    float persistence_;
};

/// Billow noise 3D
class BillowNoise3D : public Noise3D {
public:
    BillowNoise3D(std::unique_ptr<Noise3D> base, int octaves = 6,
                  float lacunarity = 2.0f, float persistence = 0.5f);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::unique_ptr<Noise3D> base_;
    int octaves_;
    float lacunarity_;
    float persistence_;
};

// ============================================================================
// Domain warping
// ============================================================================

/// Domain warping: evaluates source at coordinates distorted by warp noise
class DomainWarp2D : public Noise2D {
public:
    /// @param source Noise to sample
    /// @param warpX Noise that distorts the X coordinate
    /// @param warpZ Noise that distorts the Z coordinate
    /// @param warpStrength How much to distort (in coordinate units)
    DomainWarp2D(std::unique_ptr<Noise2D> source,
                 std::unique_ptr<Noise2D> warpX,
                 std::unique_ptr<Noise2D> warpZ,
                 float warpStrength = 1.0f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> source_;
    std::unique_ptr<Noise2D> warpX_;
    std::unique_ptr<Noise2D> warpZ_;
    float warpStrength_;
};

/// Domain warping 3D
class DomainWarp3D : public Noise3D {
public:
    DomainWarp3D(std::unique_ptr<Noise3D> source,
                 std::unique_ptr<Noise3D> warpX,
                 std::unique_ptr<Noise3D> warpY,
                 std::unique_ptr<Noise3D> warpZ,
                 float warpStrength = 1.0f);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::unique_ptr<Noise3D> source_;
    std::unique_ptr<Noise3D> warpX_;
    std::unique_ptr<Noise3D> warpY_;
    std::unique_ptr<Noise3D> warpZ_;
    float warpStrength_;
};

// ============================================================================
// Utility adapters
// ============================================================================

/// Scale frequency and amplitude of a noise source
class ScaledNoise2D : public Noise2D {
public:
    /// @param source Base noise
    /// @param frequencyX X frequency multiplier
    /// @param frequencyZ Z frequency multiplier
    /// @param amplitude Output multiplier (default 1.0)
    /// @param offset Added to output after amplitude scaling (default 0.0)
    ScaledNoise2D(std::unique_ptr<Noise2D> source,
                  float frequencyX, float frequencyZ,
                  float amplitude = 1.0f, float offset = 0.0f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> source_;
    float freqX_, freqZ_;
    float amplitude_, offset_;
};

/// Scale frequency and amplitude of a 3D noise source
class ScaledNoise3D : public Noise3D {
public:
    ScaledNoise3D(std::unique_ptr<Noise3D> source,
                  float frequencyX, float frequencyY, float frequencyZ,
                  float amplitude = 1.0f, float offset = 0.0f);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::unique_ptr<Noise3D> source_;
    float freqX_, freqY_, freqZ_;
    float amplitude_, offset_;
};

/// Clamp noise output to [minVal, maxVal]
class ClampedNoise2D : public Noise2D {
public:
    ClampedNoise2D(std::unique_ptr<Noise2D> source,
                   float minVal = -1.0f, float maxVal = 1.0f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> source_;
    float minVal_, maxVal_;
};

/// Combine two noise sources with an operation
enum class CombineOp {
    Add,       ///< a + b
    Multiply,  ///< a * b
    Min,       ///< min(a, b)
    Max,       ///< max(a, b)
    Lerp,      ///< lerp(a, b, blendFactor)
};

class CombinedNoise2D : public Noise2D {
public:
    /// @param a First noise source
    /// @param b Second noise source
    /// @param op Combine operation
    /// @param blendFactor Used for Lerp (0.0 = all a, 1.0 = all b)
    CombinedNoise2D(std::unique_ptr<Noise2D> a,
                    std::unique_ptr<Noise2D> b,
                    CombineOp op, float blendFactor = 0.5f);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> a_, b_;
    CombineOp op_;
    float blendFactor_;
};

/// Evaluate a custom function on a noise source's output
class MappedNoise2D : public Noise2D {
public:
    /// @param source Base noise
    /// @param mapFunc Function applied to noise output
    MappedNoise2D(std::unique_ptr<Noise2D> source,
                  std::function<float(float)> mapFunc);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::unique_ptr<Noise2D> source_;
    std::function<float(float)> mapFunc_;
};

// ============================================================================
// Convenience factories
// ============================================================================

namespace NoiseFactory {

/// Perlin noise with FBM octave stacking
[[nodiscard]] std::unique_ptr<Noise2D> perlinFBM(
    uint64_t seed, int octaves = 6, float frequency = 0.01f);

/// OpenSimplex2 noise with FBM octave stacking
[[nodiscard]] std::unique_ptr<Noise2D> simplexFBM(
    uint64_t seed, int octaves = 6, float frequency = 0.01f);

/// Ridged multi-fractal for mountain terrain
[[nodiscard]] std::unique_ptr<Noise2D> ridgedMountains(
    uint64_t seed, float frequency = 0.005f);

/// Domain-warped terrain for natural-looking landforms
[[nodiscard]] std::unique_ptr<Noise2D> warpedTerrain(
    uint64_t seed, float frequency = 0.008f);

/// Perlin 3D with FBM (for caves, 3D density)
[[nodiscard]] std::unique_ptr<Noise3D> perlinFBM3D(
    uint64_t seed, int octaves = 4, float frequency = 0.02f);

/// OpenSimplex2 3D with FBM
[[nodiscard]] std::unique_ptr<Noise3D> simplexFBM3D(
    uint64_t seed, int octaves = 4, float frequency = 0.02f);

}  // namespace NoiseFactory

}  // namespace finevox
