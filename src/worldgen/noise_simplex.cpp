/**
 * @file noise_simplex.cpp
 * @brief OpenSimplex2S noise implementation (2D and 3D)
 *
 * Based on OpenSimplex2 by KdotJPG (public domain).
 * Smooth variant (S) with better isotropy than classic Simplex.
 * Patent-free alternative to Simplex noise.
 */

#include "finevox/worldgen/noise.hpp"

#include <cmath>

namespace finevox::worldgen {

// ============================================================================
// OpenSimplex2 constants and gradients
// ============================================================================

namespace {

// 2D gradients (24 directions for better isotropy)
constexpr float GRAD2[] = {
     0.130526192220052f,  0.99144486137381f,
     0.38268343236509f,   0.923879532511287f,
     0.608761429008721f,  0.793353340291235f,
     0.793353340291235f,  0.608761429008721f,
     0.923879532511287f,  0.38268343236509f,
     0.99144486137381f,   0.130526192220052f,
     0.99144486137381f,  -0.130526192220052f,
     0.923879532511287f, -0.38268343236509f,
     0.793353340291235f, -0.608761429008721f,
     0.608761429008721f, -0.793353340291235f,
     0.38268343236509f,  -0.923879532511287f,
     0.130526192220052f, -0.99144486137381f,
    -0.130526192220052f, -0.99144486137381f,
    -0.38268343236509f,  -0.923879532511287f,
    -0.608761429008721f, -0.793353340291235f,
    -0.793353340291235f, -0.608761429008721f,
    -0.923879532511287f, -0.38268343236509f,
    -0.99144486137381f,  -0.130526192220052f,
    -0.99144486137381f,   0.130526192220052f,
    -0.923879532511287f,  0.38268343236509f,
    -0.793353340291235f,  0.608761429008721f,
    -0.608761429008721f,  0.793353340291235f,
    -0.38268343236509f,   0.923879532511287f,
    -0.130526192220052f,  0.99144486137381f,
};
constexpr int GRAD2_COUNT = 24;

// 3D gradients (normalized midpoint-edge vectors)
constexpr float GRAD3[] = {
    -2.22474487139f,      -2.22474487139f,      -1.0f,
    -2.22474487139f,      -2.22474487139f,       1.0f,
    -3.0862664687972017f, -1.1721513422464978f,  0.0f,
    -1.1721513422464978f, -3.0862664687972017f,  0.0f,
    -2.22474487139f,      -1.0f,                -2.22474487139f,
    -2.22474487139f,       1.0f,                -2.22474487139f,
    -1.1721513422464978f,  0.0f,                -3.0862664687972017f,
    -3.0862664687972017f,  0.0f,                -1.1721513422464978f,
    -2.22474487139f,      -1.0f,                 2.22474487139f,
    -2.22474487139f,       1.0f,                 2.22474487139f,
    -1.1721513422464978f,  0.0f,                 3.0862664687972017f,
    -3.0862664687972017f,  0.0f,                 1.1721513422464978f,
    -1.0f,                -2.22474487139f,       -2.22474487139f,
     1.0f,                -2.22474487139f,       -2.22474487139f,
     0.0f,                -3.0862664687972017f,  -1.1721513422464978f,
     0.0f,                -1.1721513422464978f,  -3.0862664687972017f,
    -1.0f,                -2.22474487139f,        2.22474487139f,
     1.0f,                -2.22474487139f,        2.22474487139f,
     0.0f,                -3.0862664687972017f,   1.1721513422464978f,
     0.0f,                -1.1721513422464978f,   3.0862664687972017f,
    -1.0f,                 2.22474487139f,       -2.22474487139f,
     1.0f,                 2.22474487139f,       -2.22474487139f,
     0.0f,                 3.0862664687972017f,  -1.1721513422464978f,
     0.0f,                 1.1721513422464978f,  -3.0862664687972017f,
    -1.0f,                 2.22474487139f,        2.22474487139f,
     1.0f,                 2.22474487139f,        2.22474487139f,
     0.0f,                 3.0862664687972017f,   1.1721513422464978f,
     0.0f,                 1.1721513422464978f,   3.0862664687972017f,
     2.22474487139f,      -2.22474487139f,       -1.0f,
     2.22474487139f,      -2.22474487139f,        1.0f,
     1.1721513422464978f, -3.0862664687972017f,   0.0f,
     3.0862664687972017f, -1.1721513422464978f,   0.0f,
     2.22474487139f,      -1.0f,                 -2.22474487139f,
     2.22474487139f,       1.0f,                 -2.22474487139f,
     3.0862664687972017f,  0.0f,                 -1.1721513422464978f,
     1.1721513422464978f,  0.0f,                 -3.0862664687972017f,
     2.22474487139f,      -1.0f,                  2.22474487139f,
     2.22474487139f,       1.0f,                  2.22474487139f,
     3.0862664687972017f,  0.0f,                  1.1721513422464978f,
     1.1721513422464978f,  0.0f,                  3.0862664687972017f,
     2.22474487139f,       2.22474487139f,       -1.0f,
     2.22474487139f,       2.22474487139f,        1.0f,
     3.0862664687972017f,  1.1721513422464978f,   0.0f,
     1.1721513422464978f,  3.0862664687972017f,   0.0f,
     2.22474487139f,       2.22474487139f,       -1.0f,
     2.22474487139f,       2.22474487139f,        1.0f,
     3.0862664687972017f,  1.1721513422464978f,   0.0f,
     1.1721513422464978f,  3.0862664687972017f,   0.0f,
};
constexpr int GRAD3_COUNT = 48;

// Normalization factor for 3D gradients
constexpr float GRAD3_NORM = 0.2781926117527186f;

/// Build permutation table for OpenSimplex2
void buildOpenSimplexPerm(std::array<int16_t, 2048>& perm,
                          std::array<int16_t, 2048>& permGrad,
                          int gradCount, uint64_t seed) {
    // Initialize source array
    std::array<int16_t, 2048> source{};
    for (int i = 0; i < 2048; ++i) {
        source[static_cast<size_t>(i)] = static_cast<int16_t>(i);
    }

    // Deterministic shuffle
    uint64_t state = seed;
    for (int i = 2047; i >= 0; --i) {
        // SplitMix64-style PRNG
        state += 0x9e3779b97f4a7c15ULL;
        uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;

        int r = static_cast<int>((z + 31) % static_cast<uint64_t>(i + 1));
        if (r < 0) r += i + 1;

        perm[static_cast<size_t>(i)] = source[static_cast<size_t>(r)];
        permGrad[static_cast<size_t>(i)] = static_cast<int16_t>(
            perm[static_cast<size_t>(i)] % gradCount);
        source[static_cast<size_t>(r)] = source[static_cast<size_t>(i)];
    }
}

// Skew constant for 2D: (sqrt(3) - 1) / 2
constexpr float SKEW_2D = 0.366025403784439f;
// Unskew constant for 2D: (3 - sqrt(3)) / 6
constexpr float UNSKEW_2D = 0.211324865405187f;

// For OpenSimplex2S 2D we use the SuperSimplex lattice
// Contribution radius squared
constexpr float RSQUARED_2D = 2.0f / 3.0f;

// Skew/unskew for 3D
constexpr float R3 = 2.0f / 3.0f;
constexpr float RSQUARED_3D = 3.0f / 4.0f;

inline int fastFloor(float x) {
    int xi = static_cast<int>(x);
    return x < static_cast<float>(xi) ? xi - 1 : xi;
}

}  // namespace

// ============================================================================
// OpenSimplex2D
// ============================================================================

OpenSimplex2D::OpenSimplex2D(uint64_t seed) {
    buildPermutation(seed);
}

void OpenSimplex2D::buildPermutation(uint64_t seed) {
    buildOpenSimplexPerm(perm_, permGrad2_, GRAD2_COUNT, seed);
}

float OpenSimplex2D::evaluate(float x, float z) const {
    // OpenSimplex2S 2D evaluation
    // Skew input to simplex space
    float s = SKEW_2D * (x + z);
    float xs = x + s;
    float zs = z + s;

    // Base vertex in skewed coords
    int xsb = fastFloor(xs);
    int zsb = fastFloor(zs);

    // Fractional part in skewed space
    float xsi = xs - static_cast<float>(xsb);
    float zsi = zs - static_cast<float>(zsb);

    // Unskew to real coords relative to base
    float t = (xsi + zsi) * UNSKEW_2D;
    float dx0 = xsi - t;
    float dz0 = zsi - t;

    // Determine which simplex we're in based on skewed coords
    float value = 0.0f;

    // Vertex contributions
    // In OpenSimplex2S, we evaluate the 3 vertices of the simplex triangle
    // plus additional vertices from neighboring triangles for smoothness

    auto contribute = [&](int xsv, int zsv, float dx, float dz) {
        float attn = RSQUARED_2D - dx * dx - dz * dz;
        if (attn > 0) {
            int pxm = xsv & 2047;
            int pzm = zsv & 2047;
            int gi = permGrad2_[static_cast<size_t>(
                perm_[static_cast<size_t>(pxm)] ^ pzm)];
            int gradIdx = gi * 2;
            float extrapolation = GRAD2[static_cast<size_t>(gradIdx)] * dx +
                                  GRAD2[static_cast<size_t>(gradIdx + 1)] * dz;
            attn *= attn;
            value += attn * attn * extrapolation;
        }
    };

    // Base vertex (0,0)
    contribute(xsb, zsb, dx0, dz0);

    // (1,0) vertex
    contribute(xsb + 1, zsb, dx0 - 1.0f + UNSKEW_2D, dz0 + UNSKEW_2D);

    // (0,1) vertex
    contribute(xsb, zsb + 1, dx0 + UNSKEW_2D, dz0 - 1.0f + UNSKEW_2D);

    // (1,1) vertex
    contribute(xsb + 1, zsb + 1,
              dx0 - 1.0f + 2.0f * UNSKEW_2D,
              dz0 - 1.0f + 2.0f * UNSKEW_2D);

    // Additional vertex depending on which triangle
    if (xsi + zsi > 1.0f) {
        // Upper triangle: add (2,1) and (1,2) contributions
        contribute(xsb + 2, zsb + 1,
                  dx0 - 2.0f + 3.0f * UNSKEW_2D,
                  dz0 - 1.0f + 3.0f * UNSKEW_2D);
        contribute(xsb + 1, zsb + 2,
                  dx0 - 1.0f + 3.0f * UNSKEW_2D,
                  dz0 - 2.0f + 3.0f * UNSKEW_2D);
    } else {
        // Lower triangle: add (-1,0) and (0,-1) contributions
        contribute(xsb - 1, zsb,
                  dx0 + 1.0f - UNSKEW_2D,
                  dz0 - UNSKEW_2D);
        contribute(xsb, zsb - 1,
                  dx0 - UNSKEW_2D,
                  dz0 + 1.0f - UNSKEW_2D);
    }

    // Scale to approximately [-1, 1]
    return value * 18.24196194486065f;
}

// ============================================================================
// OpenSimplex3D
// ============================================================================

OpenSimplex3D::OpenSimplex3D(uint64_t seed) {
    buildPermutation(seed);
}

void OpenSimplex3D::buildPermutation(uint64_t seed) {
    buildOpenSimplexPerm(perm_, permGrad3_, GRAD3_COUNT, seed);
}

float OpenSimplex3D::evaluate(float x, float y, float z) const {
    // OpenSimplex2S 3D evaluation using BCC lattice orientation

    // Re-orient to BCC lattice (rotation to improve axis alignment)
    float r = R3 * (x + y + z);
    float xr = r - x;
    float yr = r - y;
    float zr = r - z;

    // Find base and offsets
    int xrb = fastFloor(xr);
    int yrb = fastFloor(yr);
    int zrb = fastFloor(zr);

    float xri = xr - static_cast<float>(xrb);
    float yri = yr - static_cast<float>(yrb);
    float zri = zr - static_cast<float>(zrb);

    // Determine which lattice region
    int xNSign = static_cast<int>(-1.0f - xri) | 1;
    int yNSign = static_cast<int>(-1.0f - yri) | 1;
    int zNSign = static_cast<int>(-1.0f - zri) | 1;

    float ax0 = xNSign * -xri;
    float ay0 = yNSign * -yri;
    float az0 = zNSign * -zri;

    float value = 0.0f;

    auto contribute3D = [&](int xrv, int yrv, int zrv, float dx, float dy, float dz) {
        float attn = RSQUARED_3D - dx * dx - dy * dy - dz * dz;
        if (attn > 0) {
            int pxm = xrv & 2047;
            int pym = yrv & 2047;
            int pzm = zrv & 2047;
            int gi = permGrad3_[static_cast<size_t>(
                perm_[static_cast<size_t>(
                    perm_[static_cast<size_t>(pxm)] ^ pym)] ^ pzm)];
            int gradIdx = gi * 3;
            float extrapolation = GRAD3[static_cast<size_t>(gradIdx)] * dx +
                                  GRAD3[static_cast<size_t>(gradIdx + 1)] * dy +
                                  GRAD3[static_cast<size_t>(gradIdx + 2)] * dz;
            attn *= attn;
            value += attn * attn * extrapolation;
        }
    };

    // First vertex: closest to origin in lattice
    contribute3D(xrb, yrb, zrb, xri, yri, zri);

    // Second vertex: depends on which half of the cube
    if (ax0 >= ay0 && ax0 >= az0) {
        contribute3D(xrb + xNSign, yrb, zrb,
                    xri - static_cast<float>(xNSign), yri, zri);
    } else if (ay0 >= ax0 && ay0 >= az0) {
        contribute3D(xrb, yrb + yNSign, zrb,
                    xri, yri - static_cast<float>(yNSign), zri);
    } else {
        contribute3D(xrb, yrb, zrb + zNSign,
                    xri, yri, zri - static_cast<float>(zNSign));
    }

    // Third vertex: second-closest axis
    if (ax0 >= ay0 && ax0 >= az0) {
        if (ay0 >= az0) {
            contribute3D(xrb + xNSign, yrb + yNSign, zrb,
                        xri - static_cast<float>(xNSign),
                        yri - static_cast<float>(yNSign), zri);
        } else {
            contribute3D(xrb + xNSign, yrb, zrb + zNSign,
                        xri - static_cast<float>(xNSign),
                        yri, zri - static_cast<float>(zNSign));
        }
    } else if (ay0 >= ax0 && ay0 >= az0) {
        if (ax0 >= az0) {
            contribute3D(xrb + xNSign, yrb + yNSign, zrb,
                        xri - static_cast<float>(xNSign),
                        yri - static_cast<float>(yNSign), zri);
        } else {
            contribute3D(xrb, yrb + yNSign, zrb + zNSign,
                        xri, yri - static_cast<float>(yNSign),
                        zri - static_cast<float>(zNSign));
        }
    } else {
        if (ax0 >= ay0) {
            contribute3D(xrb + xNSign, yrb, zrb + zNSign,
                        xri - static_cast<float>(xNSign),
                        yri, zri - static_cast<float>(zNSign));
        } else {
            contribute3D(xrb, yrb + yNSign, zrb + zNSign,
                        xri, yri - static_cast<float>(yNSign),
                        zri - static_cast<float>(zNSign));
        }
    }

    // Fourth vertex: opposite corner
    contribute3D(xrb + xNSign, yrb + yNSign, zrb + zNSign,
                xri - static_cast<float>(xNSign),
                yri - static_cast<float>(yNSign),
                zri - static_cast<float>(zNSign));

    // Additional contribution: one step further on smallest axis
    float aScore = xri + yri + zri;
    if (aScore < 1.5f) {
        // We're in the closer half — add vertex from even closer
        contribute3D(xrb - 1, yrb - 1, zrb - 1,
                    xri + 1.0f, yri + 1.0f, zri + 1.0f);
    } else {
        // We're in the farther half
        contribute3D(xrb + 1, yrb + 1, zrb + 1,
                    xri - 1.0f, yri - 1.0f, zri - 1.0f);
    }

    // Normalize to approximately [-1, 1]
    return value * GRAD3_NORM * 32.0f;
}

}  // namespace finevox::worldgen
