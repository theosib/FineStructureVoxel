#include <gtest/gtest.h>
#include "finevox/core/world.hpp"

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

// ============================================================================
// Force-Loading tests
// ============================================================================

TEST(WorldForceLoadTest, InitiallyEmpty) {
    World world;
    EXPECT_TRUE(world.forceLoaders().empty());
}

TEST(WorldForceLoadTest, RegisterForceLoader) {
    World world;
    BlockPos pos(100, 64, 200);

    world.registerForceLoader(pos, 0);

    EXPECT_TRUE(world.isForceLoader(pos));
    EXPECT_EQ(world.forceLoaders().size(), 1);
}

TEST(WorldForceLoadTest, UnregisterForceLoader) {
    World world;
    BlockPos pos(100, 64, 200);

    world.registerForceLoader(pos, 0);
    EXPECT_TRUE(world.isForceLoader(pos));

    world.unregisterForceLoader(pos);
    EXPECT_FALSE(world.isForceLoader(pos));
    EXPECT_TRUE(world.forceLoaders().empty());
}

TEST(WorldForceLoadTest, UnregisterNonexistentIsNoOp) {
    World world;
    BlockPos pos(100, 64, 200);

    // Should not throw or cause issues
    world.unregisterForceLoader(pos);
    EXPECT_FALSE(world.isForceLoader(pos));
}

TEST(WorldForceLoadTest, CanUnloadChunk_NoForceLoaders) {
    World world;

    // With no force-loaders, any chunk can be unloaded
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(0, 0, 0)));
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(100, 5, -50)));
}

TEST(WorldForceLoadTest, CanUnloadChunk_SameChunk) {
    World world;
    BlockPos pos(100, 64, 200);  // Chunk (6, 4, 12)

    world.registerForceLoader(pos, 0);

    // The chunk containing the force-loader cannot be unloaded
    ChunkPos loaderChunk = ChunkPos::fromBlock(pos);
    EXPECT_FALSE(world.canUnloadChunk(loaderChunk));

    // Other chunks can be unloaded
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(0, 0, 0)));
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(7, 4, 12)));  // Adjacent chunk
}

TEST(WorldForceLoadTest, CanUnloadChunk_WithRadius) {
    World world;
    BlockPos pos(32, 32, 32);  // Chunk (2, 2, 2)

    world.registerForceLoader(pos, 1);  // Keep 3x3 area loaded

    ChunkPos loaderChunk = ChunkPos::fromBlock(pos);
    EXPECT_EQ(loaderChunk, ChunkPos(2, 2, 2));

    // Center chunk cannot be unloaded
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 2, 2)));

    // Adjacent chunks (distance 1) cannot be unloaded
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(1, 2, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(3, 2, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 1, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 3, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 2, 1)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 2, 3)));

    // Corner chunks (still distance 1 in Chebyshev) cannot be unloaded
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(1, 1, 1)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(3, 3, 3)));

    // Chunks at distance 2 can be unloaded
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(0, 2, 2)));
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(4, 2, 2)));
}

TEST(WorldForceLoadTest, MultipleForceLoaders) {
    World world;
    BlockPos pos1(32, 32, 32);   // Chunk (2, 2, 2)
    BlockPos pos2(160, 32, 32);  // Chunk (10, 2, 2)

    world.registerForceLoader(pos1, 0);
    world.registerForceLoader(pos2, 0);

    // Both chunks cannot be unloaded
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 2, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(10, 2, 2)));

    // Chunks in between can be unloaded
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(5, 2, 2)));

    // Unregister one
    world.unregisterForceLoader(pos1);
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(2, 2, 2)));
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(10, 2, 2)));
}

TEST(WorldForceLoadTest, OverlappingRadii) {
    World world;
    BlockPos pos1(32, 32, 32);  // Chunk (2, 2, 2)
    BlockPos pos2(64, 32, 32);  // Chunk (4, 2, 2)

    world.registerForceLoader(pos1, 1);  // Covers chunks 1-3
    world.registerForceLoader(pos2, 1);  // Covers chunks 3-5

    // Chunk 3 is covered by both
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(3, 2, 2)));

    // Remove first loader - chunk 3 still covered by second
    world.unregisterForceLoader(pos1);
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(3, 2, 2)));
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(1, 2, 2)));  // No longer covered
}

TEST(WorldForceLoadTest, SetForceLoaders) {
    World world;

    std::unordered_map<BlockPos, int32_t> loaders;
    loaders[BlockPos(0, 0, 0)] = 0;
    loaders[BlockPos(100, 64, 100)] = 2;

    world.setForceLoaders(std::move(loaders));

    EXPECT_EQ(world.forceLoaders().size(), 2);
    EXPECT_TRUE(world.isForceLoader(BlockPos(0, 0, 0)));
    EXPECT_TRUE(world.isForceLoader(BlockPos(100, 64, 100)));
}

TEST(WorldForceLoadTest, UpdateRadius) {
    World world;
    BlockPos pos(32, 32, 32);  // Chunk (2, 2, 2)

    world.registerForceLoader(pos, 0);

    // Only center chunk protected
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(2, 2, 2)));
    EXPECT_TRUE(world.canUnloadChunk(ChunkPos(3, 2, 2)));

    // Update to larger radius
    world.registerForceLoader(pos, 1);

    // Now adjacent chunks are also protected
    EXPECT_FALSE(world.canUnloadChunk(ChunkPos(3, 2, 2)));
}

TEST(WorldForceLoadTest, CanUnloadColumn) {
    World world;
    BlockPos pos(32, 64, 32);  // Chunk (2, 4, 2), Column (2, 2)

    world.registerForceLoader(pos, 0);

    // Same column should not be unloadable
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(2, 2)));

    // Adjacent columns should be unloadable (radius 0)
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(3, 2)));
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(2, 3)));
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(1, 2)));
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(2, 1)));

    // Now test with radius
    world.registerForceLoader(pos, 1);

    // Adjacent columns should now be protected
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(3, 2)));
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(2, 3)));
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(1, 2)));
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(2, 1)));
    EXPECT_FALSE(world.canUnloadColumn(ColumnPos(3, 3)));  // Diagonal

    // Columns outside radius should be unloadable
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(4, 2)));
    EXPECT_TRUE(world.canUnloadColumn(ColumnPos(2, 4)));
}

