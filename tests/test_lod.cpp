#include <gtest/gtest.h>
#include "finevox/core/lod.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/mesh.hpp"

using namespace finevox;

// ============================================================================
// LODLevel and utility function tests
// ============================================================================

TEST(LODLevelTest, BlockGrouping) {
    EXPECT_EQ(lodBlockGrouping(LODLevel::LOD0), 1);
    EXPECT_EQ(lodBlockGrouping(LODLevel::LOD1), 2);
    EXPECT_EQ(lodBlockGrouping(LODLevel::LOD2), 4);
    EXPECT_EQ(lodBlockGrouping(LODLevel::LOD3), 8);
    EXPECT_EQ(lodBlockGrouping(LODLevel::LOD4), 16);
}

TEST(LODLevelTest, Resolution) {
    EXPECT_EQ(lodResolution(LODLevel::LOD0), 16);
    EXPECT_EQ(lodResolution(LODLevel::LOD1), 8);
    EXPECT_EQ(lodResolution(LODLevel::LOD2), 4);
    EXPECT_EQ(lodResolution(LODLevel::LOD3), 2);
    EXPECT_EQ(lodResolution(LODLevel::LOD4), 1);
}

TEST(LODLevelTest, GroupingTimesResolutionIs16) {
    // Block grouping * resolution should always equal 16 (subchunk size)
    for (int i = 0; i < static_cast<int>(LOD_LEVEL_COUNT); ++i) {
        LODLevel level = static_cast<LODLevel>(i);
        EXPECT_EQ(lodBlockGrouping(level) * lodResolution(level), 16);
    }
}

// ============================================================================
// LODRequest tests - hysteresis encoding using 2x values
// ============================================================================

TEST(LODRequestTest, ExactRequestEncoding) {
    // Exact requests use even values (2 * LODLevel)
    auto r0 = LODRequest::exact(LODLevel::LOD0);
    auto r1 = LODRequest::exact(LODLevel::LOD1);
    auto r2 = LODRequest::exact(LODLevel::LOD2);
    auto r3 = LODRequest::exact(LODLevel::LOD3);
    auto r4 = LODRequest::exact(LODLevel::LOD4);

    EXPECT_EQ(r0.value, 0);
    EXPECT_EQ(r1.value, 2);
    EXPECT_EQ(r2.value, 4);
    EXPECT_EQ(r3.value, 6);
    EXPECT_EQ(r4.value, 8);

    EXPECT_TRUE(r0.isExact());
    EXPECT_TRUE(r2.isExact());
    EXPECT_FALSE(r0.isFlexible());
}

TEST(LODRequestTest, FlexibleRequestEncoding) {
    // Flexible requests use odd values (2 * LODLevel + 1)
    auto f0 = LODRequest::flexible(LODLevel::LOD0);
    auto f1 = LODRequest::flexible(LODLevel::LOD1);
    auto f2 = LODRequest::flexible(LODLevel::LOD2);
    auto f3 = LODRequest::flexible(LODLevel::LOD3);

    EXPECT_EQ(f0.value, 1);
    EXPECT_EQ(f1.value, 3);
    EXPECT_EQ(f2.value, 5);
    EXPECT_EQ(f3.value, 7);

    EXPECT_TRUE(f0.isFlexible());
    EXPECT_TRUE(f2.isFlexible());
    EXPECT_FALSE(f0.isExact());
}

TEST(LODRequestTest, BaseLevelExtraction) {
    // baseLevel returns the LOD level regardless of exact/flexible
    EXPECT_EQ(LODRequest::exact(LODLevel::LOD0).baseLevel(), LODLevel::LOD0);
    EXPECT_EQ(LODRequest::exact(LODLevel::LOD2).baseLevel(), LODLevel::LOD2);
    EXPECT_EQ(LODRequest::flexible(LODLevel::LOD1).baseLevel(), LODLevel::LOD1);
    EXPECT_EQ(LODRequest::flexible(LODLevel::LOD3).baseLevel(), LODLevel::LOD3);
}

TEST(LODRequestTest, ExactAcceptsOnlySameLevel) {
    auto r1 = LODRequest::exact(LODLevel::LOD1);

    EXPECT_FALSE(r1.accepts(LODLevel::LOD0));  // Too fine
    EXPECT_TRUE(r1.accepts(LODLevel::LOD1));   // Exact match
    EXPECT_FALSE(r1.accepts(LODLevel::LOD2));  // Too coarse
}

TEST(LODRequestTest, FlexibleAcceptsNeighboringLevels) {
    // Flexible LOD1-2 (value=3) should accept LOD1 or LOD2
    auto f1 = LODRequest::flexible(LODLevel::LOD1);

    EXPECT_FALSE(f1.accepts(LODLevel::LOD0));  // Too fine
    EXPECT_TRUE(f1.accepts(LODLevel::LOD1));   // Lower neighbor
    EXPECT_TRUE(f1.accepts(LODLevel::LOD2));   // Upper neighbor
    EXPECT_FALSE(f1.accepts(LODLevel::LOD3));  // Too coarse
}

TEST(LODRequestTest, FlexibleAtLOD0AcceptsLOD0AndLOD1) {
    auto f0 = LODRequest::flexible(LODLevel::LOD0);

    EXPECT_TRUE(f0.accepts(LODLevel::LOD0));   // Base level
    EXPECT_TRUE(f0.accepts(LODLevel::LOD1));   // Upper neighbor
    EXPECT_FALSE(f0.accepts(LODLevel::LOD2));  // Too coarse
}

TEST(LODRequestTest, BuildLevelReturnsBaseLevel) {
    // buildLevel() should always return the finer (base) level for building
    EXPECT_EQ(LODRequest::exact(LODLevel::LOD2).buildLevel(), LODLevel::LOD2);
    EXPECT_EQ(LODRequest::flexible(LODLevel::LOD1).buildLevel(), LODLevel::LOD1);
}

TEST(LODRequestTest, LodMatchesHelper) {
    auto exact2 = LODRequest::exact(LODLevel::LOD2);
    auto flex1 = LODRequest::flexible(LODLevel::LOD1);

    EXPECT_TRUE(lodMatches(exact2, LODLevel::LOD2));
    EXPECT_FALSE(lodMatches(exact2, LODLevel::LOD1));
    EXPECT_TRUE(lodMatches(flex1, LODLevel::LOD1));
    EXPECT_TRUE(lodMatches(flex1, LODLevel::LOD2));
    EXPECT_FALSE(lodMatches(flex1, LODLevel::LOD0));
}

// ============================================================================
// LODConfig tests
// ============================================================================

TEST(LODConfigTest, DefaultDistances) {
    LODConfig config;

    EXPECT_EQ(config.distances[0], 32.0f);
    EXPECT_EQ(config.distances[1], 64.0f);
    EXPECT_EQ(config.distances[2], 128.0f);
    EXPECT_EQ(config.distances[3], 256.0f);
    EXPECT_EQ(config.distances[4], 512.0f);
}

TEST(LODConfigTest, GetLevelForDistanceSimple) {
    LODConfig config;
    // Default hysteresis is 4.0f, thresholds are 32, 64, 128, 256
    // Test values well outside hysteresis zones

    // Within LOD0 range (clearly below 32-4=28)
    EXPECT_EQ(config.getLevelForDistanceSimple(0.0f), LODLevel::LOD0);
    EXPECT_EQ(config.getLevelForDistanceSimple(16.0f), LODLevel::LOD0);
    EXPECT_EQ(config.getLevelForDistanceSimple(27.0f), LODLevel::LOD0);

    // At LOD1 range (clearly between 32+4=36 and 64-4=60)
    EXPECT_EQ(config.getLevelForDistanceSimple(40.0f), LODLevel::LOD1);
    EXPECT_EQ(config.getLevelForDistanceSimple(50.0f), LODLevel::LOD1);
    EXPECT_EQ(config.getLevelForDistanceSimple(59.0f), LODLevel::LOD1);

    // At LOD2 range (clearly between 64+4=68 and 128-4=124)
    EXPECT_EQ(config.getLevelForDistanceSimple(70.0f), LODLevel::LOD2);
    EXPECT_EQ(config.getLevelForDistanceSimple(100.0f), LODLevel::LOD2);

    // At LOD3 range (clearly between 128+4=132 and 256-4=252)
    EXPECT_EQ(config.getLevelForDistanceSimple(140.0f), LODLevel::LOD3);
    EXPECT_EQ(config.getLevelForDistanceSimple(200.0f), LODLevel::LOD3);

    // At LOD4 range (clearly beyond 256+4=260)
    EXPECT_EQ(config.getLevelForDistanceSimple(270.0f), LODLevel::LOD4);
    EXPECT_EQ(config.getLevelForDistanceSimple(1000.0f), LODLevel::LOD4);
}

TEST(LODConfigTest, ForceLOD) {
    LODConfig config;
    config.forceLOD = 2;

    // Force LOD should override distance calculation
    EXPECT_EQ(config.getLevelForDistanceSimple(0.0f), LODLevel::LOD2);
    EXPECT_EQ(config.getLevelForDistanceSimple(1000.0f), LODLevel::LOD2);
    EXPECT_EQ(config.getLevelForDistance(50.0f), LODLevel::LOD2);
}

TEST(LODConfigTest, LODBiasPositive) {
    LODConfig config;
    config.lodBias = 1;  // Everything appears 2x farther
    // With bias=1, effective distances are doubled:
    // - Threshold 32 with hysteresis 4 -> 28 to 36
    // - Real distance 14 -> effective 28, real distance 18 -> effective 36
    // So LOD1 zone is real distance 18+ to 30- (effective 36 to 60)

    // Distance 20 -> effective 40, clearly in LOD1 zone (36-60)
    EXPECT_EQ(config.getLevelForDistanceSimple(20.0f), LODLevel::LOD1);

    // Distance 35 -> effective 70, clearly in LOD2 zone (68-124)
    EXPECT_EQ(config.getLevelForDistanceSimple(35.0f), LODLevel::LOD2);
}

TEST(LODConfigTest, LODBiasNegative) {
    LODConfig config;
    config.lodBias = -1;  // Everything appears 2x closer

    // Distance 64 should now behave like distance 32 (stays LOD0)
    EXPECT_EQ(config.getLevelForDistanceSimple(63.0f), LODLevel::LOD0);

    // Distance 128 should now behave like distance 64 (LOD1)
    EXPECT_EQ(config.getLevelForDistanceSimple(127.0f), LODLevel::LOD1);
}

TEST(LODConfigTest, Hysteresis) {
    LODConfig config;
    config.hysteresis = 4.0f;

    // Hysteresis creates a "dead zone" around threshold (32):
    // - Below 28 (threshold - hysteresis): LOD0
    // - 28-36 (threshold ± hysteresis): stay at current level
    // - Above 36 (threshold + hysteresis): LOD1

    // With current level LOD0, need to exceed threshold + hysteresis to switch to LOD1
    EXPECT_EQ(config.getLevelForDistance(35.0f, LODLevel::LOD0), LODLevel::LOD0);  // In dead zone, stay LOD0
    EXPECT_EQ(config.getLevelForDistance(37.0f, LODLevel::LOD0), LODLevel::LOD1);  // Above 36, switch to LOD1

    // With current level LOD1, need to be below threshold - hysteresis to switch to LOD0
    EXPECT_EQ(config.getLevelForDistance(27.0f, LODLevel::LOD1), LODLevel::LOD0);  // Below 28, switch to LOD0
    EXPECT_EQ(config.getLevelForDistance(29.0f, LODLevel::LOD1), LODLevel::LOD1);  // In dead zone, stay LOD1
    EXPECT_EQ(config.getLevelForDistance(35.0f, LODLevel::LOD1), LODLevel::LOD1);  // In dead zone, stay LOD1
}

TEST(LODConfigTest, GetRequestForDistance) {
    LODConfig config;
    // Default hysteresis is 4.0f, thresholds are 32, 64, 128, 256

    // Clearly in LOD0 zone (below 28) -> exact LOD0
    auto req0 = config.getRequestForDistance(20.0f);
    EXPECT_TRUE(req0.isExact());
    EXPECT_EQ(req0.baseLevel(), LODLevel::LOD0);

    // In hysteresis zone between LOD0 and LOD1 (28-36) -> flexible
    auto reqFlex01 = config.getRequestForDistance(32.0f);
    EXPECT_TRUE(reqFlex01.isFlexible());
    EXPECT_EQ(reqFlex01.baseLevel(), LODLevel::LOD0);
    EXPECT_TRUE(reqFlex01.accepts(LODLevel::LOD0));
    EXPECT_TRUE(reqFlex01.accepts(LODLevel::LOD1));

    // Clearly in LOD1 zone (36-60) -> exact LOD1
    auto req1 = config.getRequestForDistance(50.0f);
    EXPECT_TRUE(req1.isExact());
    EXPECT_EQ(req1.baseLevel(), LODLevel::LOD1);

    // In hysteresis zone between LOD1 and LOD2 (60-68) -> flexible
    auto reqFlex12 = config.getRequestForDistance(64.0f);
    EXPECT_TRUE(reqFlex12.isFlexible());
    EXPECT_EQ(reqFlex12.baseLevel(), LODLevel::LOD1);
    EXPECT_TRUE(reqFlex12.accepts(LODLevel::LOD1));
    EXPECT_TRUE(reqFlex12.accepts(LODLevel::LOD2));

    // Beyond all thresholds -> exact LOD4
    auto req4 = config.getRequestForDistance(600.0f);
    EXPECT_TRUE(req4.isExact());
    EXPECT_EQ(req4.baseLevel(), LODLevel::LOD4);
}

TEST(LODConfigTest, DistanceToChunk) {
    glm::dvec3 cameraPos(0.0, 0.0, 0.0);
    ChunkPos chunk(0, 0, 0);

    // Chunk center is at (8, 8, 8)
    float dist = LODConfig::distanceToChunk(cameraPos, chunk);
    float expected = std::sqrt(8.0f * 8.0f * 3.0f);  // sqrt(192) ≈ 13.86
    EXPECT_NEAR(dist, expected, 0.01f);
}

TEST(LODConfigTest, DistanceToChunkFarAway) {
    glm::dvec3 cameraPos(0.0, 0.0, 0.0);
    ChunkPos chunk(10, 0, 0);  // 10 chunks away in X

    // Chunk center is at (160 + 8, 8, 8) = (168, 8, 8)
    float dist = LODConfig::distanceToChunk(cameraPos, chunk);
    float expected = std::sqrt(168.0f * 168.0f + 8.0f * 8.0f + 8.0f * 8.0f);
    EXPECT_NEAR(dist, expected, 0.01f);
}

// ============================================================================
// LODSubChunk tests
// ============================================================================

TEST(LODSubChunkTest, Construction) {
    LODSubChunk lod1(LODLevel::LOD1);
    EXPECT_EQ(lod1.level(), LODLevel::LOD1);
    EXPECT_EQ(lod1.resolution(), 8);
    EXPECT_EQ(lod1.grouping(), 2);
    EXPECT_EQ(lod1.volume(), 512);  // 8^3

    LODSubChunk lod2(LODLevel::LOD2);
    EXPECT_EQ(lod2.resolution(), 4);
    EXPECT_EQ(lod2.grouping(), 4);
    EXPECT_EQ(lod2.volume(), 64);  // 4^3

    LODSubChunk lod3(LODLevel::LOD3);
    EXPECT_EQ(lod3.resolution(), 2);
    EXPECT_EQ(lod3.grouping(), 8);
    EXPECT_EQ(lod3.volume(), 8);  // 2^3

    LODSubChunk lod4(LODLevel::LOD4);
    EXPECT_EQ(lod4.resolution(), 1);
    EXPECT_EQ(lod4.grouping(), 16);
    EXPECT_EQ(lod4.volume(), 1);  // 1^3
}

TEST(LODSubChunkTest, LOD0BecomesLOD1) {
    // LOD0 should use regular SubChunk, so LODSubChunk upgrades to LOD1
    LODSubChunk lod0(LODLevel::LOD0);
    EXPECT_EQ(lod0.level(), LODLevel::LOD1);
}

TEST(LODSubChunkTest, InitiallyEmpty) {
    LODSubChunk lod(LODLevel::LOD1);
    EXPECT_TRUE(lod.isEmpty());
    EXPECT_EQ(lod.nonAirCount(), 0);
}

TEST(LODSubChunkTest, GetSetBlock) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);
    EXPECT_EQ(lod.getBlock(0, 0, 0), stone);
    EXPECT_EQ(lod.nonAirCount(), 1);

    lod.setBlock(7, 7, 7, stone);  // Max coords for LOD1 (8x8x8)
    EXPECT_EQ(lod.getBlock(7, 7, 7), stone);
    EXPECT_EQ(lod.nonAirCount(), 2);
}

TEST(LODSubChunkTest, OutOfBoundsReturnsAir) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Out of bounds should return air
    EXPECT_EQ(lod.getBlock(-1, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(lod.getBlock(8, 0, 0), AIR_BLOCK_TYPE);  // 8 is out of bounds for LOD1
    EXPECT_EQ(lod.getBlock(0, 100, 0), AIR_BLOCK_TYPE);
}

TEST(LODSubChunkTest, Clear) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);
    lod.setBlock(1, 1, 1, stone);
    EXPECT_EQ(lod.nonAirCount(), 2);

    lod.clear();
    EXPECT_TRUE(lod.isEmpty());
    EXPECT_EQ(lod.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
}

TEST(LODSubChunkTest, VersionIncrement) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    uint64_t v0 = lod.version();
    lod.setBlock(0, 0, 0, stone);
    uint64_t v1 = lod.version();
    EXPECT_GT(v1, v0);

    // Setting to same value shouldn't increment
    lod.setBlock(0, 0, 0, stone);
    EXPECT_EQ(lod.version(), v1);

    // Setting to different value should increment
    lod.setBlock(0, 0, 0, AIR_BLOCK_TYPE);
    EXPECT_GT(lod.version(), v1);
}

// ============================================================================
// Downsampling tests
// ============================================================================

TEST(LODSubChunkTest, DownsampleSolidChunk) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Fill entire subchunk with stone
    source.fill(stone);

    // Downsample to LOD1 (8x8x8)
    LODSubChunk lod1(LODLevel::LOD1);
    lod1.downsampleFrom(source);

    // All cells should be stone
    EXPECT_EQ(lod1.nonAirCount(), 512);  // 8^3
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 8; ++z) {
            for (int x = 0; x < 8; ++x) {
                EXPECT_EQ(lod1.getBlock(x, y, z), stone)
                    << "at (" << x << ", " << y << ", " << z << ")";
            }
        }
    }
}

TEST(LODSubChunkTest, DownsampleEmptyChunk) {
    SubChunk source;  // All air

    LODSubChunk lod1(LODLevel::LOD1);
    lod1.downsampleFrom(source);

    EXPECT_TRUE(lod1.isEmpty());
}

TEST(LODSubChunkTest, DownsampleHalfFilled) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Fill bottom half (y < 8) with stone
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                source.setBlock(x, y, z, stone);
            }
        }
    }

    LODSubChunk lod1(LODLevel::LOD1);
    lod1.downsampleFrom(source);

    // Bottom 4 layers (y = 0..3) should be stone, top 4 (y = 4..7) should be air
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 8; ++z) {
            for (int x = 0; x < 8; ++x) {
                if (y < 4) {
                    EXPECT_EQ(lod1.getBlock(x, y, z), stone)
                        << "at (" << x << ", " << y << ", " << z << ")";
                } else {
                    EXPECT_EQ(lod1.getBlock(x, y, z), AIR_BLOCK_TYPE)
                        << "at (" << x << ", " << y << ", " << z << ")";
                }
            }
        }
    }
}

TEST(LODSubChunkTest, DownsampleModeSelection) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");
    BlockTypeId dirt = BlockTypeId::fromName("blockgame:dirt");

    // In the first 2x2x2 group (LOD coords 0,0,0):
    // Put 5 stone blocks and 3 dirt blocks - stone should win
    source.setBlock(0, 0, 0, stone);
    source.setBlock(1, 0, 0, stone);
    source.setBlock(0, 1, 0, stone);
    source.setBlock(1, 1, 0, stone);
    source.setBlock(0, 0, 1, stone);
    source.setBlock(1, 0, 1, dirt);
    source.setBlock(0, 1, 1, dirt);
    source.setBlock(1, 1, 1, dirt);

    LODSubChunk lod1(LODLevel::LOD1);
    lod1.downsampleFrom(source);

    // Stone should be selected as the representative (5 > 3)
    EXPECT_EQ(lod1.getBlock(0, 0, 0), stone);
}

TEST(LODSubChunkTest, DownsampleSparseGroup) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Put only 3 blocks in a 2x2x2 group (less than half = 4)
    // Now preserves any solid block (no 50% threshold) to avoid losing small features
    source.setBlock(0, 0, 0, stone);
    source.setBlock(1, 0, 0, stone);
    source.setBlock(0, 1, 0, stone);

    LODSubChunk lod1(LODLevel::LOD1);
    lod1.downsampleFrom(source);

    // Group has solid blocks, should be stone (not air)
    // The topmost block is at y=1, which is >= g/2 (1 >= 1), so surface preservation applies
    EXPECT_EQ(lod1.getBlock(0, 0, 0), stone);
}

TEST(LODSubChunkTest, DownsampleToLOD2) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Fill a 4x4x4 region (one LOD2 cell)
    for (int y = 0; y < 4; ++y) {
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                source.setBlock(x, y, z, stone);
            }
        }
    }

    LODSubChunk lod2(LODLevel::LOD2);
    lod2.downsampleFrom(source);

    // First cell should be stone, rest should be air
    EXPECT_EQ(lod2.getBlock(0, 0, 0), stone);
    EXPECT_EQ(lod2.getBlock(1, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(lod2.nonAirCount(), 1);
}

TEST(LODSubChunkTest, DownsampleToLOD4) {
    SubChunk source;
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Fill entire subchunk
    source.fill(stone);

    LODSubChunk lod4(LODLevel::LOD4);
    lod4.downsampleFrom(source);

    // Only one cell at LOD4
    EXPECT_EQ(lod4.volume(), 1);
    EXPECT_EQ(lod4.getBlock(0, 0, 0), stone);
    EXPECT_EQ(lod4.nonAirCount(), 1);
}

// ============================================================================
// Debug utilities tests
// ============================================================================

TEST(LODDebugTest, DebugColors) {
    // Just verify colors are distinct and valid
    glm::vec3 c0 = lodDebugColor(LODLevel::LOD0);
    glm::vec3 c1 = lodDebugColor(LODLevel::LOD1);
    glm::vec3 c2 = lodDebugColor(LODLevel::LOD2);
    glm::vec3 c3 = lodDebugColor(LODLevel::LOD3);
    glm::vec3 c4 = lodDebugColor(LODLevel::LOD4);

    // Colors should be different
    EXPECT_NE(c0, c1);
    EXPECT_NE(c1, c2);
    EXPECT_NE(c2, c3);
    EXPECT_NE(c3, c4);

    // All components should be valid (0-1 range)
    auto checkValid = [](const glm::vec3& c) {
        EXPECT_GE(c.r, 0.0f);
        EXPECT_LE(c.r, 1.0f);
        EXPECT_GE(c.g, 0.0f);
        EXPECT_LE(c.g, 1.0f);
        EXPECT_GE(c.b, 0.0f);
        EXPECT_LE(c.b, 1.0f);
    };
    checkValid(c0);
    checkValid(c1);
    checkValid(c2);
    checkValid(c3);
    checkValid(c4);
}

TEST(LODDebugTest, LevelNames) {
    EXPECT_STREQ(lodLevelName(LODLevel::LOD0), "LOD0 (16x16x16)");
    EXPECT_STREQ(lodLevelName(LODLevel::LOD1), "LOD1 (8x8x8)");
    EXPECT_STREQ(lodLevelName(LODLevel::LOD2), "LOD2 (4x4x4)");
    EXPECT_STREQ(lodLevelName(LODLevel::LOD3), "LOD3 (2x2x2)");
    EXPECT_STREQ(lodLevelName(LODLevel::LOD4), "LOD4 (1x1x1)");
}

// ============================================================================
// LOD Mesh Generation tests
// ============================================================================

class LODMeshTest : public ::testing::Test {
protected:
    MeshBuilder builder;

    // Simple texture provider that returns unit UVs
    BlockTextureProvider simpleTextureProvider = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };
};

TEST_F(LODMeshTest, EmptyLODSubChunkProducesEmptyMesh) {
    LODSubChunk lod(LODLevel::LOD1);  // Empty

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    EXPECT_TRUE(mesh.isEmpty());
}

TEST_F(LODMeshTest, SingleLOD1BlockProducesMesh) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    // Single exposed block should have 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24);
    // 6 faces * 6 indices = 36 indices
    EXPECT_EQ(mesh.indexCount(), 36);
}

TEST_F(LODMeshTest, LOD1BlocksAreScaled2x) {
    LODSubChunk lod(LODLevel::LOD1);  // 2x scale
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    // Check that vertices span a 2x2x2 region
    float minX = 1000, maxX = -1000;
    float minY = 1000, maxY = -1000;
    float minZ = 1000, maxZ = -1000;

    for (const auto& v : mesh.vertices) {
        minX = std::min(minX, v.position.x);
        maxX = std::max(maxX, v.position.x);
        minY = std::min(minY, v.position.y);
        maxY = std::max(maxY, v.position.y);
        minZ = std::min(minZ, v.position.z);
        maxZ = std::max(maxZ, v.position.z);
    }

    EXPECT_FLOAT_EQ(minX, 0.0f);
    EXPECT_FLOAT_EQ(maxX, 2.0f);  // LOD1 = 2x scale
    EXPECT_FLOAT_EQ(minY, 0.0f);
    EXPECT_FLOAT_EQ(maxY, 2.0f);
    EXPECT_FLOAT_EQ(minZ, 0.0f);
    EXPECT_FLOAT_EQ(maxZ, 2.0f);
}

TEST_F(LODMeshTest, LOD2BlocksAreScaled4x) {
    LODSubChunk lod(LODLevel::LOD2);  // 4x scale
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    float maxX = -1000, maxY = -1000, maxZ = -1000;
    for (const auto& v : mesh.vertices) {
        maxX = std::max(maxX, v.position.x);
        maxY = std::max(maxY, v.position.y);
        maxZ = std::max(maxZ, v.position.z);
    }

    EXPECT_FLOAT_EQ(maxX, 4.0f);  // LOD2 = 4x scale
    EXPECT_FLOAT_EQ(maxY, 4.0f);
    EXPECT_FLOAT_EQ(maxZ, 4.0f);
}

TEST_F(LODMeshTest, LOD4BlocksAreScaled16x) {
    LODSubChunk lod(LODLevel::LOD4);  // 16x scale (entire subchunk is one block)
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    float maxX = -1000, maxY = -1000, maxZ = -1000;
    for (const auto& v : mesh.vertices) {
        maxX = std::max(maxX, v.position.x);
        maxY = std::max(maxY, v.position.y);
        maxZ = std::max(maxZ, v.position.z);
    }

    EXPECT_FLOAT_EQ(maxX, 16.0f);  // LOD4 = 16x scale
    EXPECT_FLOAT_EQ(maxY, 16.0f);
    EXPECT_FLOAT_EQ(maxZ, 16.0f);
}

TEST_F(LODMeshTest, AdjacentLODBlocksCullHiddenFaces) {
    LODSubChunk lod(LODLevel::LOD1);
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Two adjacent blocks in X direction
    lod.setBlock(0, 0, 0, stone);
    lod.setBlock(1, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    // With greedy meshing, two adjacent same-type blocks are merged into one:
    // 6 faces * 4 vertices = 24 vertices
    // The internal face is culled, and the coplanar external faces are merged
    EXPECT_EQ(mesh.vertexCount(), 24);
}

TEST_F(LODMeshTest, FullLOD1SubChunkCullsAllInternalFaces) {
    LODSubChunk lod(LODLevel::LOD1);  // 8x8x8 resolution
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    // Fill all cells
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 8; ++z) {
            for (int x = 0; x < 8; ++x) {
                lod.setBlock(x, y, z, stone);
            }
        }
    }

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    // With greedy meshing, all internal faces are culled and all external faces
    // on each side are merged into one large quad per face:
    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24);
}

TEST_F(LODMeshTest, TextureTilesAcrossScaledBlock) {
    LODSubChunk lod(LODLevel::LOD1);  // 2x scale
    BlockTypeId stone = BlockTypeId::fromName("blockgame:stone");

    lod.setBlock(0, 0, 0, stone);

    MeshData mesh = builder.buildLODMesh(lod, ChunkPos{0, 0, 0}, simpleTextureProvider);

    // Find a +Y face (top face)
    bool foundTiledUV = false;
    for (size_t i = 0; i + 3 < mesh.vertices.size(); i += 4) {
        if (mesh.vertices[i].normal == glm::vec3(0.0f, 1.0f, 0.0f)) {
            // Check UV range - should tile 2x for LOD1
            float maxU = 0, maxV = 0;
            for (size_t j = 0; j < 4; ++j) {
                maxU = std::max(maxU, mesh.vertices[i + j].texCoord.x);
                maxV = std::max(maxV, mesh.vertices[i + j].texCoord.y);
            }
            // UVs should tile 2x2 for a 2x2 block
            if (maxU >= 1.9f && maxV >= 1.9f) {
                foundTiledUV = true;
            }
            break;
        }
    }

    EXPECT_TRUE(foundTiledUV) << "Expected UV coordinates to tile across 2x2 LOD block";
}
