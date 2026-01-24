#include <gtest/gtest.h>
#include "finevox/light_data.hpp"
#include "finevox/light_engine.hpp"
#include "finevox/block_type.hpp"
#include "finevox/world.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/subchunk.hpp"

using namespace finevox;

// ============================================================================
// LightData Tests (standalone class - may be deprecated)
// ============================================================================

TEST(LightDataTest, InitiallyDark) {
    LightData data;
    EXPECT_TRUE(data.isDark());
    EXPECT_EQ(data.getSkyLight(0, 0, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, 0, 0), 0);
}

TEST(LightDataTest, SetGetSkyLight) {
    LightData data;

    data.setSkyLight(5, 5, 5, 15);
    EXPECT_EQ(data.getSkyLight(5, 5, 5), 15);
    EXPECT_EQ(data.getBlockLight(5, 5, 5), 0);  // Block light unchanged

    data.setSkyLight(5, 5, 5, 8);
    EXPECT_EQ(data.getSkyLight(5, 5, 5), 8);
}

TEST(LightDataTest, SetGetBlockLight) {
    LightData data;

    data.setBlockLight(3, 7, 11, 12);
    EXPECT_EQ(data.getBlockLight(3, 7, 11), 12);
    EXPECT_EQ(data.getSkyLight(3, 7, 11), 0);  // Sky light unchanged
}

TEST(LightDataTest, CombinedLight) {
    LightData data;

    data.setSkyLight(0, 0, 0, 10);
    data.setBlockLight(0, 0, 0, 5);
    EXPECT_EQ(data.getCombinedLight(0, 0, 0), 10);  // Max of sky and block

    data.setBlockLight(0, 0, 0, 15);
    EXPECT_EQ(data.getCombinedLight(0, 0, 0), 15);  // Now block is higher
}

TEST(LightDataTest, PackedLight) {
    LightData data;

    data.setLight(1, 2, 3, 12, 7);  // Sky=12, Block=7
    EXPECT_EQ(data.getSkyLight(1, 2, 3), 12);
    EXPECT_EQ(data.getBlockLight(1, 2, 3), 7);

    uint8_t packed = data.getPackedLight(1, 2, 3);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

TEST(LightDataTest, FillSkyLight) {
    LightData data;

    data.fillSkyLight(15);
    EXPECT_TRUE(data.isFullSkyLight());
    EXPECT_EQ(data.getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(data.getSkyLight(15, 15, 15), 15);
}

TEST(LightDataTest, Clear) {
    LightData data;

    data.setLight(5, 5, 5, 10, 10);
    EXPECT_FALSE(data.isDark());

    data.clear();
    EXPECT_TRUE(data.isDark());
}

TEST(LightDataTest, VersionIncrement) {
    LightData data;

    uint64_t v1 = data.version();
    data.setSkyLight(0, 0, 0, 5);
    uint64_t v2 = data.version();
    EXPECT_GT(v2, v1);

    // Setting to same value shouldn't increment
    data.setSkyLight(0, 0, 0, 5);
    uint64_t v3 = data.version();
    EXPECT_EQ(v3, v2);
}

TEST(LightDataTest, OutOfBoundsReturnsZero) {
    LightData data;

    EXPECT_EQ(data.getSkyLight(-1, 0, 0), 0);
    EXPECT_EQ(data.getSkyLight(16, 0, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, -1, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, 16, 0), 0);
}

// ============================================================================
// SubChunk Light Storage Tests
// ============================================================================

TEST(SubChunkLightTest, InitiallyDark) {
    SubChunk subChunk;
    EXPECT_TRUE(subChunk.isLightDark());
    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 0);
    EXPECT_EQ(subChunk.getBlockLight(0, 0, 0), 0);
}

TEST(SubChunkLightTest, SetGetSkyLight) {
    SubChunk subChunk;

    subChunk.setSkyLight(5, 5, 5, 15);
    EXPECT_EQ(subChunk.getSkyLight(5, 5, 5), 15);
    EXPECT_EQ(subChunk.getBlockLight(5, 5, 5), 0);  // Block light unchanged

    subChunk.setSkyLight(5, 5, 5, 8);
    EXPECT_EQ(subChunk.getSkyLight(5, 5, 5), 8);
}

TEST(SubChunkLightTest, SetGetBlockLight) {
    SubChunk subChunk;

    subChunk.setBlockLight(3, 7, 11, 12);
    EXPECT_EQ(subChunk.getBlockLight(3, 7, 11), 12);
    EXPECT_EQ(subChunk.getSkyLight(3, 7, 11), 0);  // Sky light unchanged
}

TEST(SubChunkLightTest, CombinedLight) {
    SubChunk subChunk;

    subChunk.setSkyLight(0, 0, 0, 10);
    subChunk.setBlockLight(0, 0, 0, 5);
    EXPECT_EQ(subChunk.getCombinedLight(0, 0, 0), 10);  // Max of sky and block

    subChunk.setBlockLight(0, 0, 0, 15);
    EXPECT_EQ(subChunk.getCombinedLight(0, 0, 0), 15);  // Now block is higher
}

TEST(SubChunkLightTest, PackedLight) {
    SubChunk subChunk;

    subChunk.setLight(1, 2, 3, 12, 7);  // Sky=12, Block=7
    EXPECT_EQ(subChunk.getSkyLight(1, 2, 3), 12);
    EXPECT_EQ(subChunk.getBlockLight(1, 2, 3), 7);

    uint8_t packed = subChunk.getPackedLight(1, 2, 3);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

TEST(SubChunkLightTest, FillSkyLight) {
    SubChunk subChunk;

    subChunk.fillSkyLight(15);
    EXPECT_TRUE(subChunk.isFullSkyLight());
    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(subChunk.getSkyLight(15, 15, 15), 15);
}

TEST(SubChunkLightTest, ClearLight) {
    SubChunk subChunk;

    subChunk.setLight(5, 5, 5, 10, 10);
    EXPECT_FALSE(subChunk.isLightDark());

    subChunk.clearLight();
    EXPECT_TRUE(subChunk.isLightDark());
}

TEST(SubChunkLightTest, LightVersionIncrement) {
    SubChunk subChunk;

    uint64_t v1 = subChunk.lightVersion();
    subChunk.setSkyLight(0, 0, 0, 5);
    uint64_t v2 = subChunk.lightVersion();
    EXPECT_GT(v2, v1);

    // Setting to same value shouldn't increment
    subChunk.setSkyLight(0, 0, 0, 5);
    uint64_t v3 = subChunk.lightVersion();
    EXPECT_EQ(v3, v2);
}

TEST(SubChunkLightTest, OutOfBoundsReturnsZero) {
    SubChunk subChunk;

    // Use index-based access for out-of-bounds testing
    EXPECT_EQ(subChunk.getSkyLight(-1), 0);
    EXPECT_EQ(subChunk.getSkyLight(4096), 0);
    EXPECT_EQ(subChunk.getBlockLight(-1), 0);
    EXPECT_EQ(subChunk.getBlockLight(4096), 0);
}

TEST(SubChunkLightTest, SetLightData) {
    SubChunk subChunk;

    std::array<uint8_t, 4096> data;
    data.fill(packLightValue(10, 5));

    subChunk.setLightData(data);

    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 10);
    EXPECT_EQ(subChunk.getBlockLight(0, 0, 0), 5);
    EXPECT_EQ(subChunk.getSkyLight(15, 15, 15), 10);
}

TEST(SubChunkLightTest, GetLightData) {
    SubChunk subChunk;

    subChunk.setSkyLight(0, 0, 0, 15);
    subChunk.setBlockLight(0, 0, 0, 7);

    const auto& data = subChunk.lightData();
    uint8_t packed = data[0];  // Index 0 = position (0,0,0)
    EXPECT_EQ(unpackSkyLightValue(packed), 15);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

// ============================================================================
// BlockType Lighting Properties Tests
// ============================================================================

TEST(BlockTypeLightTest, DefaultProperties) {
    BlockType type;

    EXPECT_EQ(type.lightEmission(), 0);
    EXPECT_EQ(type.lightAttenuation(), 15);  // Opaque by default
    EXPECT_TRUE(type.blocksSkyLight());
}

TEST(BlockTypeLightTest, SetLightEmission) {
    BlockType type;
    type.setLightEmission(14);

    EXPECT_EQ(type.lightEmission(), 14);

    // Clamp to max
    type.setLightEmission(20);
    EXPECT_EQ(type.lightEmission(), 15);
}

TEST(BlockTypeLightTest, SetLightAttenuation) {
    BlockType type;
    type.setLightAttenuation(1);

    EXPECT_EQ(type.lightAttenuation(), 1);

    // Clamp to valid range
    type.setLightAttenuation(0);  // Should become 1
    EXPECT_EQ(type.lightAttenuation(), 1);

    type.setLightAttenuation(20);  // Should become 15
    EXPECT_EQ(type.lightAttenuation(), 15);
}

TEST(BlockTypeLightTest, SetBlocksSkyLight) {
    BlockType type;
    EXPECT_TRUE(type.blocksSkyLight());

    type.setBlocksSkyLight(false);
    EXPECT_FALSE(type.blocksSkyLight());
}

TEST(BlockTypeLightTest, TransparentBlockProperties) {
    BlockType glass;
    glass.setOpaque(false)
         .setTransparent(true)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    EXPECT_FALSE(glass.isOpaque());
    EXPECT_TRUE(glass.isTransparent());
    EXPECT_EQ(glass.lightAttenuation(), 1);
    EXPECT_FALSE(glass.blocksSkyLight());
}

TEST(BlockTypeLightTest, TorchProperties) {
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    EXPECT_FALSE(torch.hasCollision());
    EXPECT_FALSE(torch.isOpaque());
    EXPECT_EQ(torch.lightEmission(), 14);
    EXPECT_EQ(torch.lightAttenuation(), 1);
    EXPECT_FALSE(torch.blocksSkyLight());
}

// ============================================================================
// ChunkColumn Heightmap Tests
// ============================================================================

TEST(HeightmapTest, InitiallyNoHeight) {
    ChunkColumn column(ColumnPos{0, 0});

    // No blocks placed, heightmap should indicate no opaque blocks
    EXPECT_EQ(column.getHeight(0, 0), std::numeric_limits<int32_t>::min());
    EXPECT_TRUE(column.heightmapDirty());
}

TEST(HeightmapTest, UpdateHeightOnBlockPlace) {
    ChunkColumn column(ColumnPos{0, 0});

    // Place a block at y=10
    column.updateHeight(5, 5, 10, true);
    EXPECT_EQ(column.getHeight(5, 5), 11);  // Height is top of block (y + 1)

    // Place a higher block
    column.updateHeight(5, 5, 20, true);
    EXPECT_EQ(column.getHeight(5, 5), 21);
}

TEST(HeightmapTest, SetHeightmapData) {
    ChunkColumn column(ColumnPos{0, 0});

    std::array<int32_t, 256> data;
    data.fill(100);
    data[0] = 50;

    column.setHeightmapData(data);

    EXPECT_EQ(column.getHeight(0, 0), 50);
    EXPECT_EQ(column.getHeight(1, 0), 100);
    EXPECT_FALSE(column.heightmapDirty());
}

// ============================================================================
// LightEngine Basic Tests
// ============================================================================

TEST(LightEngineTest, InitiallyDark) {
    World world;
    LightEngine engine(world);

    EXPECT_EQ(engine.getSkyLight(BlockPos{0, 0, 0}), 0);
    EXPECT_EQ(engine.getBlockLight(BlockPos{0, 0, 0}), 0);
}

TEST(LightEngineTest, RegisterBlockType) {
    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    BlockRegistry::global().registerType("test:torch", torch);

    const BlockType& retrieved = BlockRegistry::global().getType("test:torch");
    EXPECT_EQ(retrieved.lightEmission(), 14);
}

TEST(LightEngineTest, PropagateBlockLight) {
    World world;
    LightEngine engine(world);

    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("lighttest:torch", torch);

    // Place a torch
    BlockPos torchPos{8, 8, 8};
    BlockTypeId torchId = BlockTypeId::fromName("lighttest:torch");
    world.setBlock(torchPos, torchId);

    // Propagate light
    engine.propagateBlockLight(torchPos, 14);

    // Check light at torch position
    EXPECT_EQ(engine.getBlockLight(torchPos), 14);

    // Check light decreases with distance
    EXPECT_LT(engine.getBlockLight(BlockPos{9, 8, 8}), 14);
}

TEST(LightEngineTest, SubChunkCreatedOnDemand) {
    World world;
    LightEngine engine(world);

    ChunkPos chunkPos{0, 0, 0};

    // No subchunk initially
    EXPECT_EQ(world.getSubChunk(chunkPos), nullptr);

    // Propagate some light - this should create the subchunk
    engine.propagateBlockLight(BlockPos{8, 8, 8}, 10);

    // Now subchunk should exist with light data
    SubChunk* subChunk = world.getSubChunk(chunkPos);
    EXPECT_NE(subChunk, nullptr);
    EXPECT_EQ(subChunk->getBlockLight(8, 8, 8), 10);
}

TEST(LightEngineTest, LightStoredInSubChunk) {
    World world;
    LightEngine engine(world);

    // Place a block first to create the subchunk
    BlockPos pos{4, 4, 4};
    world.setBlock(pos, BlockTypeId::fromName("minecraft:stone"));

    // Propagate light
    engine.propagateBlockLight(pos, 12);

    // Verify light is stored in the subchunk
    SubChunk* subChunk = world.getSubChunk(ChunkPos{0, 0, 0});
    ASSERT_NE(subChunk, nullptr);
    EXPECT_EQ(subChunk->getBlockLight(4, 4, 4), 12);

    // LightEngine should return the same value
    EXPECT_EQ(engine.getBlockLight(pos), 12);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(LightUtilsTest, PackUnpackLight) {
    uint8_t packed = packLightValue(12, 7);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);

    // Edge cases
    packed = packLightValue(0, 0);
    EXPECT_EQ(unpackSkyLightValue(packed), 0);
    EXPECT_EQ(unpackBlockLightValue(packed), 0);

    packed = packLightValue(15, 15);
    EXPECT_EQ(unpackSkyLightValue(packed), 15);
    EXPECT_EQ(unpackBlockLightValue(packed), 15);
}

TEST(LightUtilsTest, CombinedLightValue) {
    uint8_t packed = packLightValue(10, 5);
    EXPECT_EQ(combinedLightValue(packed), 10);

    packed = packLightValue(5, 12);
    EXPECT_EQ(combinedLightValue(packed), 12);

    packed = packLightValue(8, 8);
    EXPECT_EQ(combinedLightValue(packed), 8);
}
