#include <gtest/gtest.h>
#include "finevox/world.hpp"

using namespace finevox;

// ============================================================================
// Basic World tests
// ============================================================================

TEST(WorldTest, EmptyWorld) {
    World world;
    EXPECT_EQ(world.columnCount(), 0);
    EXPECT_EQ(world.totalNonAirBlocks(), 0);
}

TEST(WorldTest, GetBlockFromEmptyWorld) {
    World world;
    EXPECT_EQ(world.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
    EXPECT_EQ(world.getBlock(100, 50, -100), AIR_BLOCK_TYPE);
}

TEST(WorldTest, SetAndGetBlock) {
    World world;
    auto stone = BlockTypeId::fromName("world:stone");

    world.setBlock(10, 64, 20, stone);

    EXPECT_EQ(world.getBlock(10, 64, 20), stone);
    EXPECT_EQ(world.columnCount(), 1);
}

TEST(WorldTest, SetBlockWithBlockPos) {
    World world;
    auto dirt = BlockTypeId::fromName("world:dirt");
    BlockPos pos(5, 32, 15);

    world.setBlock(pos, dirt);

    EXPECT_EQ(world.getBlock(pos), dirt);
}

TEST(WorldTest, SetBlockCreatesColumn) {
    World world;
    auto stone = BlockTypeId::fromName("world:stone2");

    EXPECT_FALSE(world.hasColumn(ColumnPos(0, 0)));

    world.setBlock(5, 64, 10, stone);

    EXPECT_TRUE(world.hasColumn(ColumnPos(0, 0)));
}

TEST(WorldTest, MultipleColumnsCreated) {
    World world;
    auto stone = BlockTypeId::fromName("world:stone3");

    // Set blocks in different columns
    world.setBlock(0, 0, 0, stone);      // Column (0, 0)
    world.setBlock(16, 0, 0, stone);     // Column (1, 0)
    world.setBlock(0, 0, 16, stone);     // Column (0, 1)
    world.setBlock(32, 0, 32, stone);    // Column (2, 2)

    EXPECT_EQ(world.columnCount(), 4);
}

TEST(WorldTest, NegativeCoordinates) {
    World world;
    auto stone = BlockTypeId::fromName("world:negcoord");

    world.setBlock(-1, -10, -1, stone);

    EXPECT_EQ(world.getBlock(-1, -10, -1), stone);
    EXPECT_TRUE(world.hasColumn(ColumnPos(-1, -1)));
}

TEST(WorldTest, LargeCoordinates) {
    World world;
    auto stone = BlockTypeId::fromName("world:largecoord");

    world.setBlock(100000, 500, -200000, stone);

    EXPECT_EQ(world.getBlock(100000, 500, -200000), stone);
}

// ============================================================================
// Column access tests
// ============================================================================

TEST(WorldTest, GetColumn) {
    World world;
    auto stone = BlockTypeId::fromName("world:getcol");

    world.setBlock(5, 64, 10, stone);

    ChunkColumn* col = world.getColumn(ColumnPos(0, 0));
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->position(), ColumnPos(0, 0));
}

TEST(WorldTest, GetNonexistentColumn) {
    World world;
    EXPECT_EQ(world.getColumn(ColumnPos(99, 99)), nullptr);
}

TEST(WorldTest, GetOrCreateColumn) {
    World world;

    EXPECT_FALSE(world.hasColumn(ColumnPos(5, 10)));

    ChunkColumn& col = world.getOrCreateColumn(ColumnPos(5, 10));

    EXPECT_TRUE(world.hasColumn(ColumnPos(5, 10)));
    EXPECT_EQ(col.position(), ColumnPos(5, 10));
}

TEST(WorldTest, RemoveColumn) {
    World world;
    auto stone = BlockTypeId::fromName("world:removecol");

    world.setBlock(5, 64, 10, stone);
    EXPECT_TRUE(world.hasColumn(ColumnPos(0, 0)));

    bool removed = world.removeColumn(ColumnPos(0, 0));
    EXPECT_TRUE(removed);
    EXPECT_FALSE(world.hasColumn(ColumnPos(0, 0)));
}

TEST(WorldTest, RemoveNonexistentColumn) {
    World world;
    EXPECT_FALSE(world.removeColumn(ColumnPos(99, 99)));
}

// ============================================================================
// SubChunk access tests
// ============================================================================

TEST(WorldTest, GetSubChunk) {
    World world;
    auto stone = BlockTypeId::fromName("world:getsub");

    world.setBlock(5, 64, 10, stone);  // Y=64 is subchunk Y=4

    SubChunk* sub = world.getSubChunk(ChunkPos(0, 4, 0));
    ASSERT_NE(sub, nullptr);
    EXPECT_EQ(sub->nonAirCount(), 1);
}

TEST(WorldTest, GetNonexistentSubChunk) {
    World world;
    EXPECT_EQ(world.getSubChunk(ChunkPos(0, 0, 0)), nullptr);
}

// ============================================================================
// Statistics tests
// ============================================================================

TEST(WorldTest, TotalNonAirBlocks) {
    World world;
    auto stone = BlockTypeId::fromName("world:stats");

    EXPECT_EQ(world.totalNonAirBlocks(), 0);

    world.setBlock(0, 0, 0, stone);
    world.setBlock(1, 0, 0, stone);
    world.setBlock(16, 0, 0, stone);  // Different column

    EXPECT_EQ(world.totalNonAirBlocks(), 3);
}

// ============================================================================
// ForEach tests
// ============================================================================

TEST(WorldTest, ForEachColumn) {
    World world;
    auto stone = BlockTypeId::fromName("world:foreach");

    world.setBlock(0, 0, 0, stone);
    world.setBlock(16, 0, 0, stone);
    world.setBlock(32, 0, 0, stone);

    int count = 0;
    world.forEachColumn([&count](ColumnPos pos, ChunkColumn& col) {
        (void)pos;
        (void)col;
        ++count;
    });

    EXPECT_EQ(count, 3);
}

// ============================================================================
// Generator callback tests
// ============================================================================

TEST(WorldTest, ColumnGenerator) {
    World world;
    auto bedrock = BlockTypeId::fromName("world:bedrock");

    // Set a generator that places bedrock at Y=0
    world.setColumnGenerator([bedrock](ChunkColumn& col) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(col.position().x * 16 + x, 0, col.position().z * 16 + z, bedrock);
            }
        }
    });

    // Set a block to trigger column creation
    auto stone = BlockTypeId::fromName("world:stone_gen");
    world.setBlock(5, 64, 10, stone);

    // Check that generator was called
    EXPECT_EQ(world.getBlock(0, 0, 0), bedrock);
    EXPECT_EQ(world.getBlock(15, 0, 15), bedrock);
}

// ============================================================================
// Clear tests
// ============================================================================

TEST(WorldTest, Clear) {
    World world;
    auto stone = BlockTypeId::fromName("world:clear");

    world.setBlock(0, 0, 0, stone);
    world.setBlock(16, 0, 0, stone);
    world.setBlock(32, 0, 32, stone);

    EXPECT_EQ(world.columnCount(), 3);

    world.clear();

    EXPECT_EQ(world.columnCount(), 0);
    EXPECT_EQ(world.getBlock(0, 0, 0), AIR_BLOCK_TYPE);
}

// ============================================================================
// Mesh Dirty Notification tests
// ============================================================================

TEST(WorldTest, GetAffectedSubChunks_InteriorBlock) {
    World world;

    // Block at (5, 5, 5) is interior to subchunk (0, 0, 0)
    auto affected = world.getAffectedSubChunks(BlockPos(5, 5, 5));

    EXPECT_EQ(affected.size(), 1);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
}

TEST(WorldTest, GetAffectedSubChunks_XBoundary) {
    World world;

    // Block at x=0 affects neighboring subchunk at x-1
    auto affected = world.getAffectedSubChunks(BlockPos(0, 5, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(-1, 0, 0));

    // Block at x=15 affects neighboring subchunk at x+1
    affected = world.getAffectedSubChunks(BlockPos(15, 5, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(1, 0, 0));
}

TEST(WorldTest, GetAffectedSubChunks_YBoundary) {
    World world;

    // Block at y=0 in subchunk (0, 0, 0) affects subchunk (0, -1, 0)
    auto affected = world.getAffectedSubChunks(BlockPos(5, 0, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(0, -1, 0));

    // Block at y=15 affects subchunk (0, 1, 0)
    affected = world.getAffectedSubChunks(BlockPos(5, 15, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(0, 1, 0));
}

TEST(WorldTest, GetAffectedSubChunks_ZBoundary) {
    World world;

    // Block at z=0 affects neighboring subchunk at z-1
    auto affected = world.getAffectedSubChunks(BlockPos(5, 5, 0));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(0, 0, -1));

    // Block at z=15 affects neighboring subchunk at z+1
    affected = world.getAffectedSubChunks(BlockPos(5, 5, 15));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(0, 0, 1));
}

TEST(WorldTest, GetAffectedSubChunks_Corner) {
    World world;

    // Block at corner (0, 0, 0) affects 3 neighboring subchunks
    auto affected = world.getAffectedSubChunks(BlockPos(0, 0, 0));

    EXPECT_EQ(affected.size(), 4);
    EXPECT_EQ(affected[0], ChunkPos(0, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(-1, 0, 0));  // -X neighbor
    EXPECT_EQ(affected[2], ChunkPos(0, -1, 0));  // -Y neighbor
    EXPECT_EQ(affected[3], ChunkPos(0, 0, -1));  // -Z neighbor
}

TEST(WorldTest, GetAffectedSubChunks_NegativeCoordinates) {
    World world;

    // Block at (-1, 5, 5) is at x=15 in subchunk (-1, 0, 0)
    auto affected = world.getAffectedSubChunks(BlockPos(-1, 5, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(-1, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(0, 0, 0));  // Affects +X neighbor

    // Block at (-16, 5, 5) is at x=0 in subchunk (-1, 0, 0)
    affected = world.getAffectedSubChunks(BlockPos(-16, 5, 5));

    EXPECT_EQ(affected.size(), 2);
    EXPECT_EQ(affected[0], ChunkPos(-1, 0, 0));
    EXPECT_EQ(affected[1], ChunkPos(-2, 0, 0));  // Affects -X neighbor
}

