#include <gtest/gtest.h>
#include "finevox/batch_builder.hpp"
#include "finevox/world.hpp"

using namespace finevox;

// ============================================================================
// Basic BatchBuilder tests
// ============================================================================

TEST(BatchBuilderTest, EmptyBatch) {
    BatchBuilder batch;
    EXPECT_TRUE(batch.empty());
    EXPECT_EQ(batch.size(), 0);
}

TEST(BatchBuilderTest, SetBlock) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:stone");

    batch.setBlock(BlockPos(0, 0, 0), stone);

    EXPECT_FALSE(batch.empty());
    EXPECT_EQ(batch.size(), 1);
    EXPECT_TRUE(batch.hasChange(BlockPos(0, 0, 0)));
}

TEST(BatchBuilderTest, SetBlockCoordinates) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:stone2");

    batch.setBlock(5, 10, 15, stone);

    EXPECT_TRUE(batch.hasChange(BlockPos(5, 10, 15)));
}

TEST(BatchBuilderTest, GetChange) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:stone3");

    batch.setBlock(BlockPos(0, 0, 0), stone);

    auto change = batch.getChange(BlockPos(0, 0, 0));
    ASSERT_TRUE(change.has_value());
    EXPECT_EQ(*change, stone);
}

TEST(BatchBuilderTest, GetChangeNonexistent) {
    BatchBuilder batch;
    auto change = batch.getChange(BlockPos(999, 999, 999));
    EXPECT_FALSE(change.has_value());
}

// ============================================================================
// Coalescing tests
// ============================================================================

TEST(BatchBuilderTest, CoalescesChanges) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:coalesce_stone");
    auto dirt = BlockTypeId::fromName("batch:coalesce_dirt");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(0, 0, 0), dirt);  // Overwrites previous

    EXPECT_EQ(batch.size(), 1);

    auto change = batch.getChange(BlockPos(0, 0, 0));
    EXPECT_EQ(*change, dirt);
}

TEST(BatchBuilderTest, Cancel) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:cancel");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(1, 0, 0), stone);

    batch.cancel(BlockPos(0, 0, 0));

    EXPECT_EQ(batch.size(), 1);
    EXPECT_FALSE(batch.hasChange(BlockPos(0, 0, 0)));
    EXPECT_TRUE(batch.hasChange(BlockPos(1, 0, 0)));
}

TEST(BatchBuilderTest, Clear) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:clear");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(1, 0, 0), stone);

    batch.clear();

    EXPECT_TRUE(batch.empty());
    EXPECT_EQ(batch.size(), 0);
}

// ============================================================================
// Bounds tests
// ============================================================================

TEST(BatchBuilderTest, GetBoundsEmpty) {
    BatchBuilder batch;
    EXPECT_FALSE(batch.getBounds().has_value());
}

TEST(BatchBuilderTest, GetBoundsSingle) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:bounds1");

    batch.setBlock(BlockPos(5, 10, 15), stone);

    auto bounds = batch.getBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->min, BlockPos(5, 10, 15));
    EXPECT_EQ(bounds->max, BlockPos(5, 10, 15));
}

TEST(BatchBuilderTest, GetBoundsMultiple) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:bounds2");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(10, 20, 30), stone);
    batch.setBlock(BlockPos(-5, -10, -15), stone);

    auto bounds = batch.getBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_EQ(bounds->min, BlockPos(-5, -10, -15));
    EXPECT_EQ(bounds->max, BlockPos(10, 20, 30));
}

// ============================================================================
// Affected columns tests
// ============================================================================

TEST(BatchBuilderTest, GetAffectedColumns) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:affected");

    batch.setBlock(BlockPos(0, 0, 0), stone);     // Column (0, 0)
    batch.setBlock(BlockPos(15, 0, 15), stone);   // Column (0, 0)
    batch.setBlock(BlockPos(16, 0, 0), stone);    // Column (1, 0)
    batch.setBlock(BlockPos(0, 0, 16), stone);    // Column (0, 1)

    auto columns = batch.getAffectedColumns();

    EXPECT_EQ(columns.size(), 3);
}

// ============================================================================
// Commit tests
// ============================================================================

TEST(BatchBuilderTest, CommitToWorld) {
    World world;
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:commit");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(1, 0, 0), stone);
    batch.setBlock(BlockPos(2, 0, 0), stone);

    size_t changed = batch.commit(world);

    EXPECT_EQ(changed, 3);
    EXPECT_TRUE(batch.empty());  // Batch cleared after commit

    EXPECT_EQ(world.getBlock(0, 0, 0), stone);
    EXPECT_EQ(world.getBlock(1, 0, 0), stone);
    EXPECT_EQ(world.getBlock(2, 0, 0), stone);
}

TEST(BatchBuilderTest, CommitSkipsNoOps) {
    World world;
    auto stone = BlockTypeId::fromName("batch:noop");

    // Pre-set a block
    world.setBlock(BlockPos(0, 0, 0), stone);

    BatchBuilder batch;
    batch.setBlock(BlockPos(0, 0, 0), stone);  // Same value - no-op
    batch.setBlock(BlockPos(1, 0, 0), stone);  // New block

    size_t changed = batch.commit(world);

    EXPECT_EQ(changed, 1);  // Only one actual change
}

TEST(BatchBuilderTest, CommitAndGetChanged) {
    World world;
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:getchanged");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(1, 0, 0), stone);
    batch.setBlock(BlockPos(2, 0, 0), AIR_BLOCK_TYPE);  // No-op (already air)

    auto changed = batch.commitAndGetChanged(world);

    EXPECT_EQ(changed.size(), 2);
}

// ============================================================================
// ForEach tests
// ============================================================================

TEST(BatchBuilderTest, ForEach) {
    BatchBuilder batch;
    auto stone = BlockTypeId::fromName("batch:foreach");

    batch.setBlock(BlockPos(0, 0, 0), stone);
    batch.setBlock(BlockPos(1, 0, 0), stone);
    batch.setBlock(BlockPos(2, 0, 0), stone);

    int count = 0;
    batch.forEach([&count](BlockPos, BlockTypeId) {
        ++count;
    });

    EXPECT_EQ(count, 3);
}

// ============================================================================
// Merge tests
// ============================================================================

TEST(BatchBuilderTest, Merge) {
    BatchBuilder batch1;
    BatchBuilder batch2;
    auto stone = BlockTypeId::fromName("batch:merge_stone");
    auto dirt = BlockTypeId::fromName("batch:merge_dirt");

    batch1.setBlock(BlockPos(0, 0, 0), stone);
    batch1.setBlock(BlockPos(1, 0, 0), stone);

    batch2.setBlock(BlockPos(1, 0, 0), dirt);  // Overrides batch1
    batch2.setBlock(BlockPos(2, 0, 0), dirt);

    batch1.merge(batch2);

    EXPECT_EQ(batch1.size(), 3);
    EXPECT_EQ(*batch1.getChange(BlockPos(0, 0, 0)), stone);
    EXPECT_EQ(*batch1.getChange(BlockPos(1, 0, 0)), dirt);  // Overridden
    EXPECT_EQ(*batch1.getChange(BlockPos(2, 0, 0)), dirt);
}

// ============================================================================
// CommitBatchWithHistory tests
// ============================================================================

TEST(BatchBuilderTest, CommitWithHistory) {
    World world;
    auto stone = BlockTypeId::fromName("batch:history_stone");
    auto dirt = BlockTypeId::fromName("batch:history_dirt");

    // Pre-set some blocks
    world.setBlock(BlockPos(0, 0, 0), stone);

    BatchBuilder batch;
    batch.setBlock(BlockPos(0, 0, 0), dirt);  // Change stone -> dirt
    batch.setBlock(BlockPos(1, 0, 0), stone); // Set new block

    auto result = commitBatchWithHistory(batch, world);

    EXPECT_EQ(result.blocksChanged, 2);
    EXPECT_EQ(result.changes.size(), 2);

    // Find the stone->dirt change
    bool foundChange = false;
    for (const auto& change : result.changes) {
        if (change.pos == BlockPos(0, 0, 0)) {
            EXPECT_EQ(change.oldType, stone);
            EXPECT_EQ(change.newType, dirt);
            foundChange = true;
        }
    }
    EXPECT_TRUE(foundChange);
}
