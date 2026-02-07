/**
 * @file test_noise.cpp
 * @brief Unit tests for noise library
 *
 * Verifies determinism, output range, frequency scaling,
 * octave stacking, and composable operations.
 */

#include "finevox/noise.hpp"
#include "finevox/noise_ops.hpp"
#include "finevox/noise_voronoi.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <unordered_set>
#include <vector>

using namespace finevox;

// ============================================================================
// NoiseHash tests
// ============================================================================

TEST(NoiseHashTest, Hash2DDeterministic) {
    uint64_t seed = 12345;
    uint32_t h1 = NoiseHash::hash2D(10, 20, seed);
    uint32_t h2 = NoiseHash::hash2D(10, 20, seed);
    EXPECT_EQ(h1, h2);
}

TEST(NoiseHashTest, Hash2DDifferentInputs) {
    uint64_t seed = 12345;
    uint32_t h1 = NoiseHash::hash2D(10, 20, seed);
    uint32_t h2 = NoiseHash::hash2D(11, 20, seed);
    uint32_t h3 = NoiseHash::hash2D(10, 21, seed);
    EXPECT_NE(h1, h2);
    EXPECT_NE(h1, h3);
}

TEST(NoiseHashTest, Hash3DDeterministic) {
    uint64_t seed = 42;
    uint32_t h1 = NoiseHash::hash3D(1, 2, 3, seed);
    uint32_t h2 = NoiseHash::hash3D(1, 2, 3, seed);
    EXPECT_EQ(h1, h2);
}

TEST(NoiseHashTest, DeriveSeedDeterministic) {
    uint64_t s1 = NoiseHash::deriveSeed(100, 1);
    uint64_t s2 = NoiseHash::deriveSeed(100, 1);
    EXPECT_EQ(s1, s2);
}

TEST(NoiseHashTest, DeriveSeedDifferentSalts) {
    uint64_t s1 = NoiseHash::deriveSeed(100, 1);
    uint64_t s2 = NoiseHash::deriveSeed(100, 2);
    EXPECT_NE(s1, s2);
}

// ============================================================================
// Perlin 2D tests
// ============================================================================

TEST(PerlinNoise2DTest, Deterministic) {
    PerlinNoise2D noise(42);
    float v1 = noise.evaluate(1.5f, 2.7f);
    float v2 = noise.evaluate(1.5f, 2.7f);
    EXPECT_FLOAT_EQ(v1, v2);
}

TEST(PerlinNoise2DTest, DifferentSeedsDifferentOutput) {
    PerlinNoise2D noise1(42);
    PerlinNoise2D noise2(99);
    // Very unlikely to be exactly equal at a non-integer point
    EXPECT_NE(noise1.evaluate(1.5f, 2.7f), noise2.evaluate(1.5f, 2.7f));
}

TEST(PerlinNoise2DTest, OutputRange) {
    PerlinNoise2D noise(12345);
    float minVal = 1.0f, maxVal = -1.0f;

    for (float x = -50.0f; x <= 50.0f; x += 0.37f) {
        for (float z = -50.0f; z <= 50.0f; z += 0.37f) {
            float v = noise.evaluate(x, z);
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
        }
    }

    // Perlin noise should stay within roughly [-1, 1]
    EXPECT_GT(minVal, -1.5f);
    EXPECT_LT(maxVal, 1.5f);
    // Should actually reach some reasonable range
    EXPECT_LT(minVal, -0.1f);
    EXPECT_GT(maxVal, 0.1f);
}

TEST(PerlinNoise2DTest, IntegerCoordinatesNearZero) {
    PerlinNoise2D noise(42);
    // At integer coordinates, gradient noise should be close to 0
    // (dot product of gradient with zero offset)
    float v = noise.evaluate(0.0f, 0.0f);
    EXPECT_NEAR(v, 0.0f, 0.01f);

    v = noise.evaluate(5.0f, 3.0f);
    EXPECT_NEAR(v, 0.0f, 0.01f);
}

TEST(PerlinNoise2DTest, Continuity) {
    PerlinNoise2D noise(42);
    float v1 = noise.evaluate(1.5f, 2.5f);
    float v2 = noise.evaluate(1.501f, 2.5f);
    // Small step should produce small change
    EXPECT_NEAR(v1, v2, 0.1f);
}

// ============================================================================
// Perlin 3D tests
// ============================================================================

TEST(PerlinNoise3DTest, Deterministic) {
    PerlinNoise3D noise(42);
    float v1 = noise.evaluate(1.5f, 2.7f, 3.3f);
    float v2 = noise.evaluate(1.5f, 2.7f, 3.3f);
    EXPECT_FLOAT_EQ(v1, v2);
}

TEST(PerlinNoise3DTest, OutputRange) {
    PerlinNoise3D noise(777);
    float minVal = 1.0f, maxVal = -1.0f;

    for (float x = -20.0f; x <= 20.0f; x += 1.37f) {
        for (float y = -20.0f; y <= 20.0f; y += 1.37f) {
            for (float z = -20.0f; z <= 20.0f; z += 1.37f) {
                float v = noise.evaluate(x, y, z);
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }
        }
    }

    EXPECT_GT(minVal, -1.5f);
    EXPECT_LT(maxVal, 1.5f);
    EXPECT_LT(minVal, -0.1f);
    EXPECT_GT(maxVal, 0.1f);
}

// ============================================================================
// OpenSimplex 2D tests
// ============================================================================

TEST(OpenSimplex2DTest, Deterministic) {
    OpenSimplex2D noise(42);
    float v1 = noise.evaluate(1.5f, 2.7f);
    float v2 = noise.evaluate(1.5f, 2.7f);
    EXPECT_FLOAT_EQ(v1, v2);
}

TEST(OpenSimplex2DTest, DifferentSeedsDifferentOutput) {
    OpenSimplex2D noise1(42);
    OpenSimplex2D noise2(99);
    EXPECT_NE(noise1.evaluate(1.5f, 2.7f), noise2.evaluate(1.5f, 2.7f));
}

TEST(OpenSimplex2DTest, OutputRange) {
    OpenSimplex2D noise(54321);
    float minVal = 1.0f, maxVal = -1.0f;

    for (float x = -50.0f; x <= 50.0f; x += 0.37f) {
        for (float z = -50.0f; z <= 50.0f; z += 0.37f) {
            float v = noise.evaluate(x, z);
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
        }
    }

    // Should stay within roughly [-1.5, 1.5] (OpenSimplex can slightly exceed [-1,1])
    EXPECT_GT(minVal, -2.0f);
    EXPECT_LT(maxVal, 2.0f);
    // Should have reasonable variation
    EXPECT_LT(minVal, -0.1f);
    EXPECT_GT(maxVal, 0.1f);
}

TEST(OpenSimplex2DTest, Continuity) {
    OpenSimplex2D noise(42);
    float v1 = noise.evaluate(1.5f, 2.5f);
    float v2 = noise.evaluate(1.501f, 2.5f);
    EXPECT_NEAR(v1, v2, 0.1f);
}

// ============================================================================
// OpenSimplex 3D tests
// ============================================================================

TEST(OpenSimplex3DTest, Deterministic) {
    OpenSimplex3D noise(42);
    float v1 = noise.evaluate(1.5f, 2.7f, 3.3f);
    float v2 = noise.evaluate(1.5f, 2.7f, 3.3f);
    EXPECT_FLOAT_EQ(v1, v2);
}

TEST(OpenSimplex3DTest, OutputRange) {
    OpenSimplex3D noise(99999);
    float minVal = 1.0f, maxVal = -1.0f;

    for (float x = -15.0f; x <= 15.0f; x += 1.37f) {
        for (float y = -15.0f; y <= 15.0f; y += 1.37f) {
            for (float z = -15.0f; z <= 15.0f; z += 1.37f) {
                float v = noise.evaluate(x, y, z);
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }
        }
    }

    EXPECT_GT(minVal, -2.0f);
    EXPECT_LT(maxVal, 2.0f);
    EXPECT_LT(minVal, -0.1f);
    EXPECT_GT(maxVal, 0.1f);
}

// ============================================================================
// FBM tests
// ============================================================================

TEST(FBMNoise2DTest, Deterministic) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    FBMNoise2D fbm(std::move(base), 6);

    auto base2 = std::make_unique<PerlinNoise2D>(42);
    FBMNoise2D fbm2(std::move(base2), 6);

    EXPECT_FLOAT_EQ(fbm.evaluate(1.5f, 2.7f), fbm2.evaluate(1.5f, 2.7f));
}

TEST(FBMNoise2DTest, MoreOctavesMoreDetail) {
    auto base1 = std::make_unique<PerlinNoise2D>(42);
    FBMNoise2D fbm1(std::move(base1), 1);

    auto base6 = std::make_unique<PerlinNoise2D>(42);
    FBMNoise2D fbm6(std::move(base6), 6);

    // With more octaves, output at non-octave-aligned positions should differ
    // (the additional octaves add high-frequency detail)
    float v1 = fbm1.evaluate(1.37f, 2.91f);
    float v6 = fbm6.evaluate(1.37f, 2.91f);
    // They share the first octave but 6-octave version adds more
    // Just verify they're not identical
    EXPECT_NE(v1, v6);
}

TEST(FBMNoise2DTest, NormalizedRange) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    FBMNoise2D fbm(std::move(base), 6);

    float minVal = 1.0f, maxVal = -1.0f;
    for (float x = -30.0f; x <= 30.0f; x += 0.5f) {
        for (float z = -30.0f; z <= 30.0f; z += 0.5f) {
            float v = fbm.evaluate(x, z);
            minVal = std::min(minVal, v);
            maxVal = std::max(maxVal, v);
        }
    }

    // FBM normalizes by maxAmplitude, should stay in [-1, 1]
    EXPECT_GT(minVal, -1.5f);
    EXPECT_LT(maxVal, 1.5f);
}

TEST(FBMNoise3DTest, Deterministic) {
    auto base = std::make_unique<PerlinNoise3D>(42);
    FBMNoise3D fbm(std::move(base), 4);

    auto base2 = std::make_unique<PerlinNoise3D>(42);
    FBMNoise3D fbm2(std::move(base2), 4);

    EXPECT_FLOAT_EQ(fbm.evaluate(1.5f, 2.7f, 3.3f), fbm2.evaluate(1.5f, 2.7f, 3.3f));
}

// ============================================================================
// Ridged noise tests
// ============================================================================

TEST(RidgedNoise2DTest, Deterministic) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    RidgedNoise2D ridged(std::move(base));

    auto base2 = std::make_unique<PerlinNoise2D>(42);
    RidgedNoise2D ridged2(std::move(base2));

    EXPECT_FLOAT_EQ(ridged.evaluate(1.5f, 2.7f), ridged2.evaluate(1.5f, 2.7f));
}

TEST(RidgedNoise2DTest, ProducesPositiveBias) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    RidgedNoise2D ridged(std::move(base));

    // Ridged noise tends toward higher values (inverted abs creates peaks)
    int positiveCount = 0;
    int totalCount = 0;
    for (float x = -20.0f; x <= 20.0f; x += 0.5f) {
        for (float z = -20.0f; z <= 20.0f; z += 0.5f) {
            float v = ridged.evaluate(x, z);
            if (v > 0.0f) ++positiveCount;
            ++totalCount;
        }
    }
    // Should have a mix of positive and negative values
    EXPECT_GT(positiveCount, 0);
    EXPECT_LT(positiveCount, totalCount);
}

// ============================================================================
// Billow noise tests
// ============================================================================

TEST(BillowNoise2DTest, Deterministic) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    BillowNoise2D billow(std::move(base));

    auto base2 = std::make_unique<PerlinNoise2D>(42);
    BillowNoise2D billow2(std::move(base2));

    EXPECT_FLOAT_EQ(billow.evaluate(1.5f, 2.7f), billow2.evaluate(1.5f, 2.7f));
}

// ============================================================================
// ScaledNoise tests
// ============================================================================

TEST(ScaledNoise2DTest, FrequencyScaling) {
    auto base = std::make_unique<PerlinNoise2D>(42);

    // Value at (10, 20) with frequency 0.1 should equal
    // raw noise at (1, 2)
    PerlinNoise2D raw(42);
    float rawValue = raw.evaluate(1.0f, 2.0f);

    ScaledNoise2D scaled(std::move(base), 0.1f, 0.1f);
    float scaledValue = scaled.evaluate(10.0f, 20.0f);

    EXPECT_FLOAT_EQ(rawValue, scaledValue);
}

TEST(ScaledNoise2DTest, AmplitudeAndOffset) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    PerlinNoise2D raw(42);

    float rawVal = raw.evaluate(1.5f, 2.5f);
    ScaledNoise2D scaled(std::move(base), 1.0f, 1.0f, 2.0f, 10.0f);
    float expected = rawVal * 2.0f + 10.0f;

    EXPECT_FLOAT_EQ(scaled.evaluate(1.5f, 2.5f), expected);
}

// ============================================================================
// ClampedNoise tests
// ============================================================================

TEST(ClampedNoise2DTest, ClampsOutput) {
    auto base = std::make_unique<PerlinNoise2D>(42);
    ClampedNoise2D clamped(std::move(base), -0.5f, 0.5f);

    for (float x = -20.0f; x <= 20.0f; x += 0.5f) {
        for (float z = -20.0f; z <= 20.0f; z += 0.5f) {
            float v = clamped.evaluate(x, z);
            EXPECT_GE(v, -0.5f);
            EXPECT_LE(v, 0.5f);
        }
    }
}

// ============================================================================
// CombinedNoise tests
// ============================================================================

TEST(CombinedNoise2DTest, AddOperation) {
    PerlinNoise2D rawA(42);
    PerlinNoise2D rawB(99);

    auto a = std::make_unique<PerlinNoise2D>(42);
    auto b = std::make_unique<PerlinNoise2D>(99);
    CombinedNoise2D combined(std::move(a), std::move(b), CombineOp::Add);

    float expected = rawA.evaluate(1.5f, 2.5f) + rawB.evaluate(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(combined.evaluate(1.5f, 2.5f), expected);
}

TEST(CombinedNoise2DTest, MultiplyOperation) {
    PerlinNoise2D rawA(42);
    PerlinNoise2D rawB(99);

    auto a = std::make_unique<PerlinNoise2D>(42);
    auto b = std::make_unique<PerlinNoise2D>(99);
    CombinedNoise2D combined(std::move(a), std::move(b), CombineOp::Multiply);

    float expected = rawA.evaluate(1.5f, 2.5f) * rawB.evaluate(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(combined.evaluate(1.5f, 2.5f), expected);
}

TEST(CombinedNoise2DTest, LerpOperation) {
    PerlinNoise2D rawA(42);
    PerlinNoise2D rawB(99);

    auto a = std::make_unique<PerlinNoise2D>(42);
    auto b = std::make_unique<PerlinNoise2D>(99);
    CombinedNoise2D combined(std::move(a), std::move(b), CombineOp::Lerp, 0.3f);

    float va = rawA.evaluate(1.5f, 2.5f);
    float vb = rawB.evaluate(1.5f, 2.5f);
    float expected = va + 0.3f * (vb - va);
    EXPECT_FLOAT_EQ(combined.evaluate(1.5f, 2.5f), expected);
}

// ============================================================================
// MappedNoise tests
// ============================================================================

TEST(MappedNoise2DTest, CustomFunction) {
    PerlinNoise2D raw(42);
    auto base = std::make_unique<PerlinNoise2D>(42);
    MappedNoise2D mapped(std::move(base), [](float v) { return v * v; });

    float rawVal = raw.evaluate(1.5f, 2.5f);
    EXPECT_FLOAT_EQ(mapped.evaluate(1.5f, 2.5f), rawVal * rawVal);
}

// ============================================================================
// DomainWarp tests
// ============================================================================

TEST(DomainWarp2DTest, Deterministic) {
    auto source1 = std::make_unique<PerlinNoise2D>(42);
    auto warpX1 = std::make_unique<PerlinNoise2D>(100);
    auto warpZ1 = std::make_unique<PerlinNoise2D>(200);
    DomainWarp2D warp1(std::move(source1), std::move(warpX1), std::move(warpZ1), 1.0f);

    auto source2 = std::make_unique<PerlinNoise2D>(42);
    auto warpX2 = std::make_unique<PerlinNoise2D>(100);
    auto warpZ2 = std::make_unique<PerlinNoise2D>(200);
    DomainWarp2D warp2(std::move(source2), std::move(warpX2), std::move(warpZ2), 1.0f);

    EXPECT_FLOAT_EQ(warp1.evaluate(5.5f, 3.3f), warp2.evaluate(5.5f, 3.3f));
}

TEST(DomainWarp2DTest, ZeroStrengthEqualsSource) {
    PerlinNoise2D raw(42);
    auto source = std::make_unique<PerlinNoise2D>(42);
    auto warpX = std::make_unique<PerlinNoise2D>(100);
    auto warpZ = std::make_unique<PerlinNoise2D>(200);
    DomainWarp2D warp(std::move(source), std::move(warpX), std::move(warpZ), 0.0f);

    EXPECT_FLOAT_EQ(warp.evaluate(5.5f, 3.3f), raw.evaluate(5.5f, 3.3f));
}

// ============================================================================
// Voronoi noise tests
// ============================================================================

TEST(VoronoiNoise2DTest, Deterministic) {
    VoronoiNoise2D voronoi(42, 100.0f);
    auto r1 = voronoi.evaluate(150.0f, 200.0f);
    auto r2 = voronoi.evaluate(150.0f, 200.0f);

    EXPECT_FLOAT_EQ(r1.distance1, r2.distance1);
    EXPECT_FLOAT_EQ(r1.distance2, r2.distance2);
    EXPECT_EQ(r1.cellId, r2.cellId);
    EXPECT_FLOAT_EQ(r1.cellCenter.x, r2.cellCenter.x);
    EXPECT_FLOAT_EQ(r1.cellCenter.y, r2.cellCenter.y);
}

TEST(VoronoiNoise2DTest, F1LessThanF2) {
    VoronoiNoise2D voronoi(42, 100.0f);

    for (float x = -500.0f; x <= 500.0f; x += 47.0f) {
        for (float z = -500.0f; z <= 500.0f; z += 47.0f) {
            auto r = voronoi.evaluate(x, z);
            EXPECT_LE(r.distance1, r.distance2);
        }
    }
}

TEST(VoronoiNoise2DTest, F1NonNegative) {
    VoronoiNoise2D voronoi(42, 100.0f);

    for (float x = -200.0f; x <= 200.0f; x += 13.0f) {
        for (float z = -200.0f; z <= 200.0f; z += 13.0f) {
            EXPECT_GE(voronoi.evaluateF1(x, z), 0.0f);
        }
    }
}

TEST(VoronoiNoise2DTest, F2MinusF1NonNegative) {
    VoronoiNoise2D voronoi(42, 100.0f);

    for (float x = -200.0f; x <= 200.0f; x += 13.0f) {
        for (float z = -200.0f; z <= 200.0f; z += 13.0f) {
            EXPECT_GE(voronoi.evaluateF2MinusF1(x, z), 0.0f);
        }
    }
}

TEST(VoronoiNoise2DTest, CellsFormRegions) {
    VoronoiNoise2D voronoi(42, 100.0f);

    // Points close together should often be in the same cell
    auto r1 = voronoi.evaluate(150.0f, 200.0f);
    auto r2 = voronoi.evaluate(151.0f, 200.0f);
    EXPECT_EQ(r1.cellId, r2.cellId);
}

TEST(VoronoiNoise2DTest, DifferentCellsExist) {
    VoronoiNoise2D voronoi(42, 100.0f);
    std::unordered_set<uint32_t> cells;

    for (float x = 0.0f; x <= 1000.0f; x += 100.0f) {
        for (float z = 0.0f; z <= 1000.0f; z += 100.0f) {
            auto r = voronoi.evaluate(x, z);
            cells.insert(r.cellId);
        }
    }

    // With 100x100 cell size over 1000x1000, should have multiple cells
    EXPECT_GT(cells.size(), 5u);
}

// ============================================================================
// NoiseFactory tests
// ============================================================================

TEST(NoiseFactoryTest, PerlinFBMDeterministic) {
    auto noise1 = NoiseFactory::perlinFBM(42);
    auto noise2 = NoiseFactory::perlinFBM(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(100.0f, 200.0f),
                     noise2->evaluate(100.0f, 200.0f));
}

TEST(NoiseFactoryTest, SimplexFBMDeterministic) {
    auto noise1 = NoiseFactory::simplexFBM(42);
    auto noise2 = NoiseFactory::simplexFBM(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(100.0f, 200.0f),
                     noise2->evaluate(100.0f, 200.0f));
}

TEST(NoiseFactoryTest, RidgedMountainsDeterministic) {
    auto noise1 = NoiseFactory::ridgedMountains(42);
    auto noise2 = NoiseFactory::ridgedMountains(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(100.0f, 200.0f),
                     noise2->evaluate(100.0f, 200.0f));
}

TEST(NoiseFactoryTest, WarpedTerrainDeterministic) {
    auto noise1 = NoiseFactory::warpedTerrain(42);
    auto noise2 = NoiseFactory::warpedTerrain(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(100.0f, 200.0f),
                     noise2->evaluate(100.0f, 200.0f));
}

TEST(NoiseFactoryTest, PerlinFBM3DDeterministic) {
    auto noise1 = NoiseFactory::perlinFBM3D(42);
    auto noise2 = NoiseFactory::perlinFBM3D(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(10.0f, 20.0f, 30.0f),
                     noise2->evaluate(10.0f, 20.0f, 30.0f));
}

TEST(NoiseFactoryTest, SimplexFBM3DDeterministic) {
    auto noise1 = NoiseFactory::simplexFBM3D(42);
    auto noise2 = NoiseFactory::simplexFBM3D(42);

    EXPECT_FLOAT_EQ(noise1->evaluate(10.0f, 20.0f, 30.0f),
                     noise2->evaluate(10.0f, 20.0f, 30.0f));
}

TEST(NoiseFactoryTest, DifferentSeedsDifferentOutput) {
    auto noise1 = NoiseFactory::perlinFBM(42);
    auto noise2 = NoiseFactory::perlinFBM(99);

    // Avoid coordinates that become integers after frequency scaling (0.01)
    EXPECT_NE(noise1->evaluate(105.3f, 207.7f),
              noise2->evaluate(105.3f, 207.7f));
}

// ============================================================================
// Composition test (deep nesting)
// ============================================================================

TEST(NoiseCompositionTest, DeepNesting) {
    // Build: Clamped(FBM(Perlin)) combined with ScaledNoise
    auto perlin = std::make_unique<PerlinNoise2D>(42);
    auto fbm = std::make_unique<FBMNoise2D>(std::move(perlin), 4);
    auto clamped = std::make_unique<ClampedNoise2D>(std::move(fbm), -0.8f, 0.8f);

    auto simplex = std::make_unique<OpenSimplex2D>(99);
    auto scaled = std::make_unique<ScaledNoise2D>(std::move(simplex), 0.1f, 0.1f, 0.5f);

    CombinedNoise2D combined(std::move(clamped), std::move(scaled), CombineOp::Add);

    // Just verify it evaluates without crashing and produces reasonable output
    float v = combined.evaluate(10.0f, 20.0f);
    EXPECT_GT(v, -5.0f);
    EXPECT_LT(v, 5.0f);
}

TEST(NoiseCompositionTest, PolymorphicInterface) {
    // Verify that all noise types work through the Noise2D interface
    std::vector<std::unique_ptr<Noise2D>> noises;
    noises.push_back(std::make_unique<PerlinNoise2D>(1));
    noises.push_back(std::make_unique<OpenSimplex2D>(2));

    auto fbmBase = std::make_unique<PerlinNoise2D>(3);
    noises.push_back(std::make_unique<FBMNoise2D>(std::move(fbmBase)));

    auto ridgedBase = std::make_unique<PerlinNoise2D>(4);
    noises.push_back(std::make_unique<RidgedNoise2D>(std::move(ridgedBase)));

    auto billowBase = std::make_unique<PerlinNoise2D>(5);
    noises.push_back(std::make_unique<BillowNoise2D>(std::move(billowBase)));

    auto scaledBase = std::make_unique<PerlinNoise2D>(6);
    noises.push_back(std::make_unique<ScaledNoise2D>(std::move(scaledBase), 0.5f, 0.5f));

    for (auto& noise : noises) {
        float v = noise->evaluate(5.0f, 10.0f);
        // Just verify it returns a finite value
        EXPECT_TRUE(std::isfinite(v));
    }
}
