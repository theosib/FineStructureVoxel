/**
 * @file noise_perlin.cpp
 * @brief Perlin gradient noise implementation (2D and 3D)
 *
 * Based on Ken Perlin's improved noise (2002).
 * Uses permutation table shuffled from seed for determinism.
 */

#include "finevox/worldgen/noise.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace finevox::worldgen {

// ============================================================================
// NoiseHash
// ============================================================================

uint32_t NoiseHash::hash2D(int32_t x, int32_t z, uint64_t seed) {
    // FNV-1a inspired hash
    uint64_t h = seed ^ 0x517cc1b727220a95ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(x));
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(z));
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= h >> 32;
    return static_cast<uint32_t>(h);
}

uint32_t NoiseHash::hash3D(int32_t x, int32_t y, int32_t z, uint64_t seed) {
    uint64_t h = seed ^ 0x517cc1b727220a95ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(x));
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(y));
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(z));
    h *= 0x9e3779b97f4a7c15ULL;
    h ^= h >> 32;
    return static_cast<uint32_t>(h);
}

uint64_t NoiseHash::deriveSeed(uint64_t baseSeed, uint64_t salt) {
    uint64_t h = baseSeed;
    h ^= salt * 0x9e3779b97f4a7c15ULL;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 31;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
    return h;
}

// ============================================================================
// Perlin helper functions
// ============================================================================

namespace {

/// Improved Perlin fade curve: 6t^5 - 15t^4 + 10t^3
inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

/// Linear interpolation
inline float lerp(float t, float a, float b) {
    return a + t * (b - a);
}

/// Shuffle permutation table using a seed (Fisher-Yates)
void shufflePermutation(std::array<uint8_t, 512>& perm, uint64_t seed) {
    // Initialize first 256 entries as 0..255
    for (int i = 0; i < 256; ++i) {
        perm[static_cast<size_t>(i)] = static_cast<uint8_t>(i);
    }

    // Fisher-Yates shuffle using deterministic PRNG
    uint64_t state = seed;
    for (int i = 255; i > 0; --i) {
        // Simple xorshift-based PRNG
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        int j = static_cast<int>(state % static_cast<uint64_t>(i + 1));
        std::swap(perm[static_cast<size_t>(i)], perm[static_cast<size_t>(j)]);
    }

    // Duplicate to avoid overflow
    for (int i = 0; i < 256; ++i) {
        perm[static_cast<size_t>(i + 256)] = perm[static_cast<size_t>(i)];
    }
}

}  // namespace

// ============================================================================
// PerlinNoise2D
// ============================================================================

PerlinNoise2D::PerlinNoise2D(uint64_t seed) {
    buildPermutation(seed);
}

void PerlinNoise2D::buildPermutation(uint64_t seed) {
    shufflePermutation(perm_, seed);
}

float PerlinNoise2D::grad(int hash, float x, float z) const {
    // Use low 2 bits to select gradient direction
    int h = hash & 3;
    float u = (h & 2) == 0 ? x : -x;
    float v = (h & 1) == 0 ? z : -z;
    return u + v;
}

float PerlinNoise2D::evaluate(float x, float z) const {
    // Find unit grid cell
    int xi = static_cast<int>(std::floor(x));
    int zi = static_cast<int>(std::floor(z));

    // Relative position within cell
    float xf = x - static_cast<float>(xi);
    float zf = z - static_cast<float>(zi);

    // Wrap to 0..255
    xi &= 255;
    zi &= 255;

    // Fade curves
    float u = fade(xf);
    float v = fade(zf);

    // Hash corners
    int aa = perm_[static_cast<size_t>(perm_[static_cast<size_t>(xi)] + zi)];
    int ab = perm_[static_cast<size_t>(perm_[static_cast<size_t>(xi)] + zi + 1)];
    int ba = perm_[static_cast<size_t>(perm_[static_cast<size_t>(xi + 1)] + zi)];
    int bb = perm_[static_cast<size_t>(perm_[static_cast<size_t>(xi + 1)] + zi + 1)];

    // Gradient dot products and interpolation
    float x1 = lerp(u, grad(aa, xf, zf), grad(ba, xf - 1.0f, zf));
    float x2 = lerp(u, grad(ab, xf, zf - 1.0f), grad(bb, xf - 1.0f, zf - 1.0f));

    return lerp(v, x1, x2);
}

// ============================================================================
// PerlinNoise3D
// ============================================================================

PerlinNoise3D::PerlinNoise3D(uint64_t seed) {
    buildPermutation(seed);
}

void PerlinNoise3D::buildPermutation(uint64_t seed) {
    shufflePermutation(perm_, seed);
}

float PerlinNoise3D::grad(int hash, float x, float y, float z) const {
    // Use low 4 bits to select one of 12 gradient directions
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

float PerlinNoise3D::evaluate(float x, float y, float z) const {
    // Find unit grid cell
    int xi = static_cast<int>(std::floor(x));
    int yi = static_cast<int>(std::floor(y));
    int zi = static_cast<int>(std::floor(z));

    // Relative position within cell
    float xf = x - static_cast<float>(xi);
    float yf = y - static_cast<float>(yi);
    float zf = z - static_cast<float>(zi);

    // Wrap to 0..255
    xi &= 255;
    yi &= 255;
    zi &= 255;

    // Fade curves
    float u = fade(xf);
    float v = fade(yf);
    float w = fade(zf);

    // Hash corners
    int a  = perm_[static_cast<size_t>(xi)] + yi;
    int aa = perm_[static_cast<size_t>(a)] + zi;
    int ab = perm_[static_cast<size_t>(a + 1)] + zi;
    int b  = perm_[static_cast<size_t>(xi + 1)] + yi;
    int ba = perm_[static_cast<size_t>(b)] + zi;
    int bb = perm_[static_cast<size_t>(b + 1)] + zi;

    // Gradient dot products and trilinear interpolation
    float x1 = lerp(u,
        grad(perm_[static_cast<size_t>(aa)], xf, yf, zf),
        grad(perm_[static_cast<size_t>(ba)], xf - 1.0f, yf, zf));
    float x2 = lerp(u,
        grad(perm_[static_cast<size_t>(ab)], xf, yf - 1.0f, zf),
        grad(perm_[static_cast<size_t>(bb)], xf - 1.0f, yf - 1.0f, zf));
    float y1 = lerp(v, x1, x2);

    float x3 = lerp(u,
        grad(perm_[static_cast<size_t>(aa + 1)], xf, yf, zf - 1.0f),
        grad(perm_[static_cast<size_t>(ba + 1)], xf - 1.0f, yf, zf - 1.0f));
    float x4 = lerp(u,
        grad(perm_[static_cast<size_t>(ab + 1)], xf, yf - 1.0f, zf - 1.0f),
        grad(perm_[static_cast<size_t>(bb + 1)], xf - 1.0f, yf - 1.0f, zf - 1.0f));
    float y2 = lerp(v, x3, x4);

    return lerp(w, y1, y2);
}

}  // namespace finevox::worldgen
