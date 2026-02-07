/**
 * @file noise.hpp
 * @brief Deterministic noise functions for procedural generation
 *
 * Design: [27-world-generation.md] Section 27.2
 *
 * Provides Perlin, OpenSimplex2, and base interfaces for composable noise.
 * All noise is deterministic: same seed + coordinates = same output.
 * Output range is approximately [-1, 1] for all implementations.
 */

#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace finevox::worldgen {

// ============================================================================
// Seed utilities
// ============================================================================

/// Deterministic hash and seed derivation for noise functions
class NoiseHash {
public:
    /// Hash a 2D integer position with a seed
    [[nodiscard]] static uint32_t hash2D(int32_t x, int32_t z, uint64_t seed);

    /// Hash a 3D integer position with a seed
    [[nodiscard]] static uint32_t hash3D(int32_t x, int32_t y, int32_t z, uint64_t seed);

    /// Derive an independent sub-seed from a base seed and salt
    [[nodiscard]] static uint64_t deriveSeed(uint64_t baseSeed, uint64_t salt);
};

// ============================================================================
// Base interfaces
// ============================================================================

/// Abstract 2D noise evaluator
class Noise2D {
public:
    virtual ~Noise2D() = default;

    /// Evaluate noise at (x, z). Returns approximately [-1, 1].
    [[nodiscard]] virtual float evaluate(float x, float z) const = 0;
};

/// Abstract 3D noise evaluator
class Noise3D {
public:
    virtual ~Noise3D() = default;

    /// Evaluate noise at (x, y, z). Returns approximately [-1, 1].
    [[nodiscard]] virtual float evaluate(float x, float y, float z) const = 0;
};

// ============================================================================
// Perlin noise
// ============================================================================

/// Classic Perlin gradient noise (2D)
class PerlinNoise2D : public Noise2D {
public:
    explicit PerlinNoise2D(uint64_t seed);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::array<uint8_t, 512> perm_;

    void buildPermutation(uint64_t seed);
    [[nodiscard]] float grad(int hash, float x, float z) const;
};

/// Classic Perlin gradient noise (3D)
class PerlinNoise3D : public Noise3D {
public:
    explicit PerlinNoise3D(uint64_t seed);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::array<uint8_t, 512> perm_;

    void buildPermutation(uint64_t seed);
    [[nodiscard]] float grad(int hash, float x, float y, float z) const;
};

// ============================================================================
// OpenSimplex2 noise (patent-free simplex alternative)
// ============================================================================

/// OpenSimplex2S noise (2D, smooth variant)
class OpenSimplex2D : public Noise2D {
public:
    explicit OpenSimplex2D(uint64_t seed);

    [[nodiscard]] float evaluate(float x, float z) const override;

private:
    std::array<int16_t, 2048> perm_;
    std::array<int16_t, 2048> permGrad2_;

    void buildPermutation(uint64_t seed);
};

/// OpenSimplex2S noise (3D, smooth variant)
class OpenSimplex3D : public Noise3D {
public:
    explicit OpenSimplex3D(uint64_t seed);

    [[nodiscard]] float evaluate(float x, float y, float z) const override;

private:
    std::array<int16_t, 2048> perm_;
    std::array<int16_t, 2048> permGrad3_;

    void buildPermutation(uint64_t seed);
};

}  // namespace finevox::worldgen
