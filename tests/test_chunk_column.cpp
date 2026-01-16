#include <gtest/gtest.h>
#include "finevox/chunk_column.hpp"

using namespace finevox;

// ============================================================================
// Basic construction tests
// ============================================================================

TEST(ChunkColumnTest, Construction) {
    ChunkColumn column(ColumnPos(5, 10));
    EXPECT_EQ(column.position(), ColumnPos(5, 10));
    EXPECT_TRUE(column.isEmpty());
    EXPECT_EQ(column.subChunkCount(), 0);
}

TEST(ChunkColumnTest, EmptyColumnReturnsAir) {
    ChunkColumn column(ColumnPos(0, 0));
    EXPECT_EQ(column.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(column.getBlock(100, 500, 200), AIR_BLOCK_TYPE);
    EXPECT_EQ(column.getBlock(-1, -100, -1), AIR_BLOCK_TYPE);
}

// ============================================================================
// Block get/set tests
// ============================================================================

TEST(ChunkColumnTest, SetAndGetBlock) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:stone");

    column.setBlock(5, 64, 10, stone);

    EXPECT_EQ(column.getBlock(5, 64, 10), stone);
    EXPECT_FALSE(column.isEmpty());
    EXPECT_EQ(column.subChunkCount(), 1);
}

TEST(ChunkColumnTest, SetBlockWithBlockPos) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:stone2");
    BlockPos pos(3, 20, 7);

    column.setBlock(pos, stone);

    EXPECT_EQ(column.getBlock(pos), stone);
}

TEST(ChunkColumnTest, SetBlockCreatesSubChunk) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:stone3");

    EXPECT_FALSE(column.hasSubChunk(4));  // Y=64-79 is chunk Y=4

    column.setBlock(0, 64, 0, stone);

    EXPECT_TRUE(column.hasSubChunk(4));
}

TEST(ChunkColumnTest, SetAirDoesNotCreateSubChunk) {
    ChunkColumn column(ColumnPos(0, 0));

    column.setBlock(0, 64, 0, AIR_BLOCK_TYPE);

    EXPECT_FALSE(column.hasSubChunk(4));
    EXPECT_TRUE(column.isEmpty());
}

TEST(ChunkColumnTest, SetAirRemovesSubChunkWhenEmpty) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:stone4");

    column.setBlock(0, 64, 0, stone);
    EXPECT_TRUE(column.hasSubChunk(4));

    column.setBlock(0, 64, 0, AIR_BLOCK_TYPE);
    EXPECT_FALSE(column.hasSubChunk(4));
    EXPECT_TRUE(column.isEmpty());
}

// ============================================================================
// Negative Y coordinate tests
// ============================================================================

TEST(ChunkColumnTest, NegativeYCoordinates) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:negY");

    // Y = -1 should be in chunk Y = -1
    column.setBlock(5, -1, 5, stone);
    EXPECT_EQ(column.getBlock(5, -1, 5), stone);
    EXPECT_TRUE(column.hasSubChunk(-1));

    // Y = -16 should be in chunk Y = -1
    column.setBlock(5, -16, 5, stone);
    EXPECT_EQ(column.getBlock(5, -16, 5), stone);

    // Y = -17 should be in chunk Y = -2
    column.setBlock(5, -17, 5, stone);
    EXPECT_EQ(column.getBlock(5, -17, 5), stone);
    EXPECT_TRUE(column.hasSubChunk(-2));
}

TEST(ChunkColumnTest, NegativeYLocalCoordinates) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:negYlocal");

    // Y = -1 should map to local Y = 15
    // Y = -16 should map to local Y = 0
    // Y = -17 should map to local Y = 15 (in chunk -2)

    column.setBlock(0, -1, 0, stone);
    column.setBlock(0, -16, 0, stone);

    // Both should be in chunk Y = -1
    EXPECT_EQ(column.subChunkCount(), 1);
    EXPECT_TRUE(column.hasSubChunk(-1));
}

// ============================================================================
// SubChunk access tests
// ============================================================================

TEST(ChunkColumnTest, GetSubChunk) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:subchunk");

    EXPECT_EQ(column.getSubChunk(0), nullptr);

    column.setBlock(0, 0, 0, stone);

    EXPECT_NE(column.getSubChunk(0), nullptr);
    EXPECT_EQ(column.getSubChunk(0)->nonAirCount(), 1);
}

TEST(ChunkColumnTest, GetOrCreateSubChunk) {
    ChunkColumn column(ColumnPos(0, 0));

    SubChunk& chunk = column.getOrCreateSubChunk(5);
    EXPECT_TRUE(chunk.isEmpty());
    EXPECT_EQ(column.subChunkCount(), 1);

    // Getting again should return same chunk
    SubChunk& chunk2 = column.getOrCreateSubChunk(5);
    EXPECT_EQ(&chunk, &chunk2);
}

// ============================================================================
// Multiple subchunk tests
// ============================================================================

TEST(ChunkColumnTest, MultipleSubChunks) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:multi");

    // Place blocks in different subchunks
    column.setBlock(0, 0, 0, stone);     // Chunk Y = 0
    column.setBlock(0, 64, 0, stone);    // Chunk Y = 4
    column.setBlock(0, 128, 0, stone);   // Chunk Y = 8
    column.setBlock(0, -32, 0, stone);   // Chunk Y = -2

    EXPECT_EQ(column.subChunkCount(), 4);
    EXPECT_TRUE(column.hasSubChunk(0));
    EXPECT_TRUE(column.hasSubChunk(4));
    EXPECT_TRUE(column.hasSubChunk(8));
    EXPECT_TRUE(column.hasSubChunk(-2));
}

TEST(ChunkColumnTest, NonAirCount) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:count");

    EXPECT_EQ(column.nonAirCount(), 0);

    column.setBlock(0, 0, 0, stone);
    column.setBlock(1, 0, 0, stone);
    column.setBlock(0, 64, 0, stone);

    EXPECT_EQ(column.nonAirCount(), 3);
}

// ============================================================================
// Pruning and compaction tests
// ============================================================================

TEST(ChunkColumnTest, PruneEmptySubChunks) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:prune");

    column.setBlock(0, 0, 0, stone);
    column.setBlock(0, 64, 0, stone);
    EXPECT_EQ(column.subChunkCount(), 2);

    // Clear one subchunk via direct access
    SubChunk* chunk = column.getSubChunk(0);
    chunk->clear();

    // Subchunk still exists but is empty
    EXPECT_EQ(column.subChunkCount(), 2);

    column.pruneEmptySubChunks();

    EXPECT_EQ(column.subChunkCount(), 1);
    EXPECT_FALSE(column.hasSubChunk(0));
    EXPECT_TRUE(column.hasSubChunk(4));
}

TEST(ChunkColumnTest, CompactAll) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:compact1");
    auto dirt = BlockTypeId::fromName("column:compact2");

    column.setBlock(0, 0, 0, stone);
    column.setBlock(1, 0, 0, dirt);

    // Remove dirt - should need compaction
    column.setBlock(1, 0, 0, AIR_BLOCK_TYPE);

    SubChunk* chunk = column.getSubChunk(0);
    EXPECT_TRUE(chunk->needsCompaction());

    column.compactAll();

    EXPECT_FALSE(chunk->needsCompaction());
}

// ============================================================================
// Y bounds tests
// ============================================================================

TEST(ChunkColumnTest, GetYBoundsEmpty) {
    ChunkColumn column(ColumnPos(0, 0));
    EXPECT_FALSE(column.getYBounds().has_value());
}

TEST(ChunkColumnTest, GetYBoundsSingleChunk) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:bounds1");

    column.setBlock(0, 64, 0, stone);

    auto bounds = column.getYBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->first, 4);   // Chunk Y = 4
    EXPECT_EQ(bounds->second, 4);
}

TEST(ChunkColumnTest, GetYBoundsMultipleChunks) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:bounds2");

    column.setBlock(0, -32, 0, stone);  // Chunk Y = -2
    column.setBlock(0, 0, 0, stone);    // Chunk Y = 0
    column.setBlock(0, 128, 0, stone);  // Chunk Y = 8

    auto bounds = column.getYBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->first, -2);
    EXPECT_EQ(bounds->second, 8);
}

// ============================================================================
// ForEach tests
// ============================================================================

TEST(ChunkColumnTest, ForEachSubChunk) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:foreach");

    column.setBlock(0, 0, 0, stone);
    column.setBlock(0, 64, 0, stone);
    column.setBlock(0, 128, 0, stone);

    int count = 0;
    column.forEachSubChunk([&count](int32_t y, SubChunk& chunk) {
        (void)y;
        (void)chunk;
        ++count;
    });

    EXPECT_EQ(count, 3);
}

TEST(ChunkColumnTest, ForEachSubChunkConst) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:foreachconst");

    column.setBlock(0, 0, 0, stone);
    column.setBlock(0, 64, 0, stone);

    const ChunkColumn& constColumn = column;

    int totalNonAir = 0;
    constColumn.forEachSubChunk([&totalNonAir](int32_t y, const SubChunk& chunk) {
        (void)y;
        totalNonAir += chunk.nonAirCount();
    });

    EXPECT_EQ(totalNonAir, 2);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ChunkColumnTest, BlockAtSubChunkBoundary) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:boundary");

    // Test at Y = 15 (top of chunk 0) and Y = 16 (bottom of chunk 1)
    column.setBlock(0, 15, 0, stone);
    column.setBlock(0, 16, 0, stone);

    EXPECT_EQ(column.getBlock(0, 15, 0), stone);
    EXPECT_EQ(column.getBlock(0, 16, 0), stone);
    EXPECT_TRUE(column.hasSubChunk(0));
    EXPECT_TRUE(column.hasSubChunk(1));
}

TEST(ChunkColumnTest, LargeYValues) {
    ChunkColumn column(ColumnPos(0, 0));
    auto stone = BlockTypeId::fromName("column:largeY");

    // Test near Y limits (±2048)
    column.setBlock(0, 2000, 0, stone);
    column.setBlock(0, -2000, 0, stone);

    EXPECT_EQ(column.getBlock(0, 2000, 0), stone);
    EXPECT_EQ(column.getBlock(0, -2000, 0), stone);
}
