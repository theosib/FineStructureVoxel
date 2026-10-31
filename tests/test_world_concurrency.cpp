#include <gtest/gtest.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/column_index.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace finevox;

namespace {

constexpr int CYCLES = 300;
constexpr int LAYERS = 64;     ///< Enough subchunks that a column's table has to rehash

class WorldConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override { stone = BlockTypeId::fromName("concurrency_stone"); }

    /// The game thread: subchunks appear and, when their last block goes, disappear
    void churnBlocks(std::atomic<bool>& done) {
        for (int cycle = 0; cycle < CYCLES; ++cycle) {
            for (int layer = 0; layer < LAYERS; ++layer) world.setBlock(3, layer * 16, 5, stone);
            for (int layer = 0; layer < LAYERS; ++layer) world.setBlock(3, layer * 16, 5, AIR_BLOCK_TYPE);
        }
        done = true;
    }

    /// The lighting thread: light spreading into empty space gives it a subchunk to live in
    void lightEmptySpace(std::atomic<bool>& done) {
        int y = 0;
        while (!done) {
            ChunkColumn& column = world.getOrCreateColumn(ColumnPos(0, 0));
            (void)column.getOrCreateSubChunk(LAYERS + (y++ % LAYERS));
        }
    }

    World world;
    BlockTypeId stone;
};

}  // namespace

// The map of subchunks in a column is written by two threads and read by every other:
// a rehash under a reader used to crash the client within a minute of play.
TEST_F(WorldConcurrencyTest, ColumnsSurviveEveryThreadAtOnce) {
    world.setBlock(0, 0, 0, stone);

    std::atomic<bool> done{false};
    std::atomic<int> wrongBlocks{0};
    std::vector<std::thread> threads;
    threads.emplace_back([&] { churnBlocks(done); });
    threads.emplace_back([&] { lightEmptySpace(done); });

    // Through the world
    threads.emplace_back([&] {
        while (!done) {
            for (int layer = 0; layer < LAYERS; ++layer) {
                BlockTypeId block = world.getBlock(3, layer * 16, 5);
                if (block != stone && !block.isAir()) ++wrongBlocks;
            }
        }
    });

    // Directly on the column: replica sync, vision, and sky-light lookups do this
    threads.emplace_back([&] {
        while (!done) {
            const ChunkColumn* column = std::as_const(world).getColumn(ColumnPos(0, 0));
            ASSERT_NE(column, nullptr);
            (void)column->getYBounds();
            int64_t blocks = 0;
            column->forEachSubChunk([&](int32_t, const SubChunk& sub) { blocks += sub.nonAirCount(); });
            EXPECT_GE(blocks, 0);
        }
    });

    for (auto& thread : threads) thread.join();
    EXPECT_EQ(wrongBlocks.load(), 0);
}

// A mesh build reads one subchunk for milliseconds while the game ticks on
TEST_F(WorldConcurrencyTest, ASubchunkPointerOutlivesTheSubchunkLeavingTheWorld) {
    world.setBlock(3, 0, 5, stone);
    const SubChunk* held = std::as_const(world).getSubChunk(ChunkPos(0, 0, 0));
    ASSERT_NE(held, nullptr);

    world.setBlock(3, 0, 5, AIR_BLOCK_TYPE);
    EXPECT_EQ(std::as_const(world).getSubChunk(ChunkPos(0, 0, 0)), nullptr) << "gone from the world";
    EXPECT_TRUE(held->getBlock(3, 0, 5).isAir()) << "but still safe to read";

    // Coming back is indistinguishable from a new one, except that versions kept counting
    uint64_t version = held->blockVersion();
    world.setBlock(7, 0, 5, stone);
    const SubChunk* back = std::as_const(world).getSubChunk(ChunkPos(0, 0, 0));
    ASSERT_NE(back, nullptr);
    EXPECT_EQ(back->nonAirCount(), 1);
    EXPECT_EQ(back->getBlock(7, 0, 5), stone);
    EXPECT_EQ(back->getSkyLight(0, 0, 0), SubChunk::MAX_LIGHT) << "open sky, as a new one starts";
    EXPECT_GT(back->blockVersion(), version) << "a replica must not mistake it for what it had";
}

// Palette growth used to free the buffer under readers
TEST_F(WorldConcurrencyTest, PaletteGrowthUnderReaders) {
    constexpr int TYPES = 256;      // One per cell of a plane, and many times the inline palette
    std::vector<BlockTypeId> types;
    for (int i = 0; i < TYPES; ++i) types.push_back(BlockTypeId::fromName("concurrency_type_" + std::to_string(i)));

    for (int round = 0; round < 20; ++round) {
        World fresh;
        fresh.setBlock(0, 0, 0, types[0]);

        std::atomic<bool> done{false};
        std::atomic<int> wrong{0};
        std::thread reader([&] {
            while (!done) {
                for (int i = 0; i < TYPES; ++i) {
                    BlockTypeId block = fresh.getBlock(i & 15, i >> 4, 1);
                    if (!block.isAir() && block != types[i]) ++wrong;
                }
            }
        });
        for (int i = 0; i < TYPES; ++i) fresh.setBlock(i & 15, i >> 4, 1, types[i]);
        done = true;
        reader.join();
        EXPECT_EQ(wrong.load(), 0);
    }
}

TEST_F(WorldConcurrencyTest, ColumnsFoundWhileTheIndexGrows) {
    constexpr int COLUMNS = 3000;       // Past the table's starting size
    std::atomic<bool> done{false};
    std::atomic<int> lost{0};
    world.setBlock(0, 0, 0, stone);

    std::thread reader([&] {
        while (!done) {
            if (world.getBlock(0, 0, 0) != stone) ++lost;
            if (!world.hasColumn(ColumnPos(0, 0))) ++lost;
        }
    });
    for (int i = 1; i < COLUMNS; ++i) world.setBlock(i * 16, 0, 0, stone);
    done = true;
    reader.join();

    EXPECT_EQ(lost.load(), 0);
    for (int i = 0; i < COLUMNS; ++i) EXPECT_EQ(world.getBlock(i * 16, 0, 0), stone);
}

TEST_F(WorldConcurrencyTest, ARemovedColumnStaysReadable) {
    world.setBlock(0, 0, 0, stone);
    const ChunkColumn* held = std::as_const(world).getColumn(ColumnPos(0, 0));
    ASSERT_NE(held, nullptr);

    EXPECT_TRUE(world.removeColumn(ColumnPos(0, 0)));
    EXPECT_FALSE(world.hasColumn(ColumnPos(0, 0)));
    EXPECT_TRUE(world.getBlock(0, 0, 0).isAir());
    EXPECT_EQ(held->getBlock(0, 0, 0), stone);

    world.setBlock(0, 0, 0, stone);     // Same key again
    EXPECT_TRUE(world.hasColumn(ColumnPos(0, 0)));
    EXPECT_EQ(world.getBlock(0, 0, 0), stone);
}

TEST(ColumnIndexTest, FindsEveryColumnAroundTheOrigin) {
    // Both signs, and (-1, -1), whose key is all ones
    ColumnIndex index;
    std::vector<std::unique_ptr<ChunkColumn>> columns;
    for (int x = -60; x < 60; ++x) {
        for (int z = -60; z < 60; ++z) {
            columns.push_back(std::make_unique<ChunkColumn>(ColumnPos(x, z)));
            index.set(ColumnPos(x, z).pack(), columns.back().get());
        }
    }
    for (const auto& column : columns) {
        EXPECT_EQ(index.find(column->position().pack()), column.get());
    }
    EXPECT_EQ(index.find(ColumnPos(1000, 1000).pack()), nullptr);
}

TEST(ColumnIndexTest, ErasedColumnsDoNotBreakTheProbeBehindThem) {
    ColumnIndex index;
    std::vector<std::unique_ptr<ChunkColumn>> columns;
    for (int i = 0; i < 400; ++i) {
        columns.push_back(std::make_unique<ChunkColumn>(ColumnPos(i, -i)));
        index.set(ColumnPos(i, -i).pack(), columns.back().get());
    }
    for (int i = 0; i < 400; i += 2) index.erase(ColumnPos(i, -i).pack());
    for (int i = 0; i < 400; ++i) {
        ChunkColumn* expected = i % 2 ? columns[i].get() : nullptr;
        EXPECT_EQ(index.find(ColumnPos(i, -i).pack()), expected);
    }
    index.set(ColumnPos(0, 0).pack(), columns[0].get());
    EXPECT_EQ(index.find(ColumnPos(0, 0).pack()), columns[0].get());
}

TEST(ChunkColumnRange, PastTheEndsIsAirAndWritesNothing) {
    BlockTypeId stone = BlockTypeId::fromName("concurrency_stone");
    ChunkColumn column(ColumnPos(0, 0));
    column.setBlock(0, 2047, 0, stone);
    column.setBlock(0, -2048, 0, stone);
    EXPECT_EQ(column.getBlock(0, 2047, 0), stone);
    EXPECT_EQ(column.getBlock(0, -2048, 0), stone);

    column.setBlock(0, 2048, 0, stone);
    column.setBlock(0, -2049, 0, stone);
    EXPECT_TRUE(column.getBlock(0, 2048, 0).isAir());
    EXPECT_TRUE(column.getBlock(0, -2049, 0).isAir());
    EXPECT_EQ(column.subChunkCount(), 2u);
}

TEST_F(WorldConcurrencyTest, FluidReadersSurviveTheirSubchunkGoing) {
    BlockTypeId dirt = BlockTypeId::fromName("concurrency_dirt");
    world.setBlock(0, 0, 0, dirt);
    FluidTypeId water = FluidTypeId::fromName("concurrency_water");

    std::atomic<bool> done{false};
    std::thread writer([&] {
        for (int cycle = 0; cycle < CYCLES * LAYERS; ++cycle) {
            world.setBlock(0, 16, 0, stone);
            world.setFluid(BlockCoord(1, 16, 0), water, 8);
            world.removeFluid(BlockCoord(1, 16, 0));
            world.setBlock(0, 16, 0, AIR_BLOCK_TYPE);
        }
        done = true;
    });
    std::thread reader([&] {
        while (!done) {
            (void)world.getFluid(BlockCoord(1, 16, 0));
            (void)world.getFluidLevel(BlockCoord(1, 16, 0));
            (void)world.hasFluid(BlockCoord(1, 16, 0));
        }
    });
    writer.join();
    reader.join();
}
