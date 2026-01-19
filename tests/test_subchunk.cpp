#include <gtest/gtest.h>
#include "finevox/subchunk.hpp"

using namespace finevox;

// ============================================================================
// Basic construction and access tests
// ============================================================================

TEST(SubChunkTest, DefaultConstructionIsAllAir) {
    SubChunk chunk;
    EXPECT_TRUE(chunk.isEmpty());
    EXPECT_EQ(chunk.nonAirCount(), 0);

    // Check a few random positions
    EXPECT_EQ(chunk.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.getBlock(8, 8, 8), AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.getBlock(15, 15, 15), AIR_BLOCK_TYPE);
}

TEST(SubChunkTest, SetAndGetBlock) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("subchunk:stone");

    chunk.setBlock(5, 10, 3, stone);

    EXPECT_EQ(chunk.getBlock(5, 10, 3), stone);
    EXPECT_EQ(chunk.nonAirCount(), 1);
    EXPECT_FALSE(chunk.isEmpty());
}

TEST(SubChunkTest, SetBlockByIndex) {
    SubChunk chunk;
    auto dirt = BlockTypeId::fromName("subchunk:dirt");

    // Index = y*256 + z*16 + x
    int32_t index = 5*256 + 3*16 + 2;  // (2, 5, 3)
    chunk.setBlock(index, dirt);

    EXPECT_EQ(chunk.getBlock(2, 5, 3), dirt);
    EXPECT_EQ(chunk.getBlock(index), dirt);
}

TEST(SubChunkTest, SetBlockToAirDecrementsCount) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("subchunk:stone2");

    chunk.setBlock(0, 0, 0, stone);
    EXPECT_EQ(chunk.nonAirCount(), 1);

    chunk.setBlock(0, 0, 0, AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.nonAirCount(), 0);
    EXPECT_TRUE(chunk.isEmpty());
}

TEST(SubChunkTest, SetSameBlockTwiceNoChange) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("subchunk:stone3");

    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(0, 0, 0, stone);  // Same block again

    EXPECT_EQ(chunk.nonAirCount(), 1);
    EXPECT_EQ(chunk.palette().activeCount(), 2);  // Air + stone
}

// ============================================================================
// Palette management tests
// ============================================================================

TEST(SubChunkTest, PaletteGrowsWithNewTypes) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("palettetest:stone");
    auto dirt = BlockTypeId::fromName("palettetest:dirt");
    auto grass = BlockTypeId::fromName("palettetest:grass");

    EXPECT_EQ(chunk.palette().activeCount(), 1);  // Just air

    chunk.setBlock(0, 0, 0, stone);
    EXPECT_EQ(chunk.palette().activeCount(), 2);

    chunk.setBlock(1, 0, 0, dirt);
    EXPECT_EQ(chunk.palette().activeCount(), 3);

    chunk.setBlock(2, 0, 0, grass);
    EXPECT_EQ(chunk.palette().activeCount(), 4);
}

TEST(SubChunkTest, PaletteShrinkWhenTypeRemoved) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("shrinktest:stone");

    chunk.setBlock(0, 0, 0, stone);
    EXPECT_EQ(chunk.palette().activeCount(), 2);

    // Replace with air - stone should be removed from palette
    chunk.setBlock(0, 0, 0, AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.palette().activeCount(), 1);  // Just air
    EXPECT_FALSE(chunk.palette().contains(stone));
}

TEST(SubChunkTest, PaletteKeepsTypeWithMultipleUsages) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("keeptest:stone");

    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(1, 0, 0, stone);
    EXPECT_EQ(chunk.palette().activeCount(), 2);

    // Remove one usage - stone should stay
    chunk.setBlock(0, 0, 0, AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.palette().activeCount(), 2);
    EXPECT_TRUE(chunk.palette().contains(stone));

    // Remove last usage - stone should be removed
    chunk.setBlock(1, 0, 0, AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.palette().activeCount(), 1);
    EXPECT_FALSE(chunk.palette().contains(stone));
}

// ============================================================================
// Clear and fill tests
// ============================================================================

TEST(SubChunkTest, Clear) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("cleartest:stone");

    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(5, 5, 5, stone);
    chunk.setBlock(15, 15, 15, stone);

    chunk.clear();

    EXPECT_TRUE(chunk.isEmpty());
    EXPECT_EQ(chunk.nonAirCount(), 0);
    EXPECT_EQ(chunk.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.palette().activeCount(), 1);
}

TEST(SubChunkTest, FillWithBlockType) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("filltest:stone");

    chunk.fill(stone);

    EXPECT_FALSE(chunk.isEmpty());
    EXPECT_EQ(chunk.nonAirCount(), SubChunk::VOLUME);
    EXPECT_EQ(chunk.getBlock(0, 0, 0), stone);
    EXPECT_EQ(chunk.getBlock(8, 8, 8), stone);
    EXPECT_EQ(chunk.getBlock(15, 15, 15), stone);
    EXPECT_EQ(chunk.palette().activeCount(), 2);  // Air + stone
}

TEST(SubChunkTest, FillWithAir) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("fillairtest:stone");

    chunk.fill(stone);
    chunk.fill(AIR_BLOCK_TYPE);

    EXPECT_TRUE(chunk.isEmpty());
    EXPECT_EQ(chunk.nonAirCount(), 0);
}

// ============================================================================
// Compaction tests
// ============================================================================

TEST(SubChunkTest, CompactPaletteRemapsIndices) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("compacttest:stone");
    auto dirt = BlockTypeId::fromName("compacttest:dirt");
    auto grass = BlockTypeId::fromName("compacttest:grass");

    // Add three types
    chunk.setBlock(0, 0, 0, stone);  // Index 1
    chunk.setBlock(1, 0, 0, dirt);   // Index 2
    chunk.setBlock(2, 0, 0, grass);  // Index 3

    // Remove dirt (middle entry)
    chunk.setBlock(1, 0, 0, AIR_BLOCK_TYPE);

    // Palette now has gap at index 2
    EXPECT_TRUE(chunk.needsCompaction());

    // Compact
    auto mapping = chunk.compactPalette();

    // After compaction, indices should be contiguous
    EXPECT_FALSE(chunk.needsCompaction());

    // Blocks should still resolve to correct types
    EXPECT_EQ(chunk.getBlock(0, 0, 0), stone);
    EXPECT_EQ(chunk.getBlock(1, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(chunk.getBlock(2, 0, 0), grass);
}

TEST(SubChunkTest, CompactPaletteReducesBits) {
    SubChunk chunk;

    // Add many types to get high bit count
    for (int i = 0; i < 10; ++i) {
        auto type = BlockTypeId::fromName("bitreducetest:type" + std::to_string(i));
        chunk.setBlock(i, 0, 0, type);
    }

    int bitsBeforeCompact = chunk.palette().bitsForSerialization();

    // Remove most types
    for (int i = 2; i < 10; ++i) {
        chunk.setBlock(i, 0, 0, AIR_BLOCK_TYPE);
    }

    // Compact
    chunk.compactPalette();

    int bitsAfterCompact = chunk.palette().bitsForSerialization();
    EXPECT_LT(bitsAfterCompact, bitsBeforeCompact);
}

// ============================================================================
// Usage count tests
// ============================================================================

TEST(SubChunkTest, UsageCountsAccurate) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("usagetest:stone");

    // Initially all air
    auto counts = chunk.getUsageCounts();
    EXPECT_EQ(counts[0], SubChunk::VOLUME);

    // Add some stone
    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(1, 0, 0, stone);
    chunk.setBlock(2, 0, 0, stone);

    counts = chunk.getUsageCounts();
    EXPECT_EQ(counts[0], SubChunk::VOLUME - 3);  // Air count
    EXPECT_EQ(counts[1], 3);  // Stone count
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(SubChunkTest, AllCornersAccessible) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("cornertest:stone");

    // Set all 8 corners
    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(15, 0, 0, stone);
    chunk.setBlock(0, 15, 0, stone);
    chunk.setBlock(0, 0, 15, stone);
    chunk.setBlock(15, 15, 0, stone);
    chunk.setBlock(15, 0, 15, stone);
    chunk.setBlock(0, 15, 15, stone);
    chunk.setBlock(15, 15, 15, stone);

    EXPECT_EQ(chunk.nonAirCount(), 8);

    // Verify all corners
    EXPECT_EQ(chunk.getBlock(0, 0, 0), stone);
    EXPECT_EQ(chunk.getBlock(15, 0, 0), stone);
    EXPECT_EQ(chunk.getBlock(0, 15, 0), stone);
    EXPECT_EQ(chunk.getBlock(0, 0, 15), stone);
    EXPECT_EQ(chunk.getBlock(15, 15, 0), stone);
    EXPECT_EQ(chunk.getBlock(15, 0, 15), stone);
    EXPECT_EQ(chunk.getBlock(0, 15, 15), stone);
    EXPECT_EQ(chunk.getBlock(15, 15, 15), stone);
}

TEST(SubChunkTest, IndexLayoutMatchesBlockPos) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("layouttest:stone");

    // Test that our index calculation matches BlockPos::toLocalIndex
    for (int y = 0; y < 16; y += 5) {
        for (int z = 0; z < 16; z += 5) {
            for (int x = 0; x < 16; x += 5) {
                BlockPos pos(x, y, z);
                int32_t index = pos.toLocalIndex();

                chunk.setBlock(x, y, z, stone);
                EXPECT_EQ(chunk.getBlock(index), stone);
                chunk.setBlock(x, y, z, AIR_BLOCK_TYPE);
            }
        }
    }
}

TEST(SubChunkTest, ReplaceOneTypeWithAnother) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("replacetest:stone");
    auto dirt = BlockTypeId::fromName("replacetest:dirt");

    chunk.setBlock(5, 5, 5, stone);
    EXPECT_EQ(chunk.getBlock(5, 5, 5), stone);
    EXPECT_EQ(chunk.nonAirCount(), 1);

    chunk.setBlock(5, 5, 5, dirt);
    EXPECT_EQ(chunk.getBlock(5, 5, 5), dirt);
    EXPECT_EQ(chunk.nonAirCount(), 1);  // Still 1 non-air
    EXPECT_FALSE(chunk.palette().contains(stone));  // Stone removed
    EXPECT_TRUE(chunk.palette().contains(dirt));    // Dirt added
}

// ============================================================================
// Block Version Tracking tests
// ============================================================================

TEST(SubChunkTest, BlockVersionOnBlockChange) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("meshtest:stone");

    // Initial version is 1
    uint64_t initialVersion = chunk.blockVersion();
    EXPECT_EQ(initialVersion, 1u);

    // Setting a block increments version
    chunk.setBlock(0, 0, 0, stone);
    EXPECT_GT(chunk.blockVersion(), initialVersion);

    uint64_t afterFirstSet = chunk.blockVersion();

    // Setting same block type again doesn't increment version (no actual change)
    chunk.setBlock(0, 0, 0, stone);
    EXPECT_EQ(chunk.blockVersion(), afterFirstSet);

    // But changing to different type does increment
    auto dirt = BlockTypeId::fromName("meshtest:dirt");
    chunk.setBlock(0, 0, 0, dirt);
    EXPECT_GT(chunk.blockVersion(), afterFirstSet);
}

TEST(SubChunkTest, BlockVersionOnClear) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("meshtest2:stone");

    // Fill with stone
    chunk.fill(stone);
    uint64_t afterFill = chunk.blockVersion();

    // Clear increments version
    chunk.clear();
    EXPECT_GT(chunk.blockVersion(), afterFill);

    uint64_t afterClear = chunk.blockVersion();

    // Clearing an already empty chunk doesn't increment version
    chunk.clear();
    EXPECT_EQ(chunk.blockVersion(), afterClear);
}

TEST(SubChunkTest, BlockVersionOnFill) {
    SubChunk chunk;
    auto stone = BlockTypeId::fromName("meshtest3:stone");

    // Initial version
    uint64_t initialVersion = chunk.blockVersion();

    // Fill increments version
    chunk.fill(stone);
    EXPECT_GT(chunk.blockVersion(), initialVersion);
}

TEST(SubChunkTest, BlockChangeCallback) {
    SubChunk chunk;
    chunk.setPosition(ChunkPos{1, 2, 3});

    auto stone = BlockTypeId::fromName("callbacktest:stone");

    // Track callback invocations
    int callbackCount = 0;
    ChunkPos lastPos;
    int lastX = 0, lastY = 0, lastZ = 0;
    BlockTypeId lastOldType, lastNewType;

    chunk.setBlockChangeCallback([&](ChunkPos pos, int32_t x, int32_t y, int32_t z,
                                     BlockTypeId oldType, BlockTypeId newType) {
        ++callbackCount;
        lastPos = pos;
        lastX = x;
        lastY = y;
        lastZ = z;
        lastOldType = oldType;
        lastNewType = newType;
    });

    // Set a block
    chunk.setBlock(5, 7, 9, stone);

    // Callback should have been called
    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(lastPos.x, 1);
    EXPECT_EQ(lastPos.y, 2);
    EXPECT_EQ(lastPos.z, 3);
    EXPECT_EQ(lastX, 5);
    EXPECT_EQ(lastY, 7);
    EXPECT_EQ(lastZ, 9);
    EXPECT_TRUE(lastOldType.isAir());
    EXPECT_EQ(lastNewType, stone);

    // Setting same block doesn't trigger callback
    chunk.setBlock(5, 7, 9, stone);
    EXPECT_EQ(callbackCount, 1);

    // Clear callback
    chunk.clearBlockChangeCallback();
    auto dirt = BlockTypeId::fromName("callbacktest:dirt");
    chunk.setBlock(5, 7, 9, dirt);
    EXPECT_EQ(callbackCount, 1);  // Not incremented
}
