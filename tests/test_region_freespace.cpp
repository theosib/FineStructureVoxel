#include <gtest/gtest.h>
#include <memory>
#include "finevox/core/region_file.hpp"
#include "finevox/core/serialization.hpp"
#include <filesystem>
#include <map>
#include <random>
#include <vector>

using namespace finevox;

// Tests for RegionFile free-space bookkeeping.
//
// The property under test: the allocator must never hand out a span of the .dat
// file that is still referenced by a live (latest-timestamp) ToC entry. If it
// does, saving one column silently overwrites another column's bytes in place,
// and the reader gets valid-looking but wrong data.
//
// The historically broken case was rebuilding the free list from the ToC
// journal: because saveColumnRaw legitimately re-uses reclaimed offsets, the
// same offset can appear in several ToC entries, and a naive replay marks an
// offset free even though the *latest* entry lives there.
class RegionFreeSpaceTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        // NOTE: fixed directory name (matching the style of test_region_file.cpp),
        // so these tests must not be run concurrently with each other.
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_region_freespace";
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir);
    }

    // Build a column whose content is a deterministic function of `seed`.
    // `blocks` controls the serialized payload size.
    static std::unique_ptr<ChunkColumn> makeColumn(ColumnPos pos, int seed, int blocks = 16) {
        auto col = std::make_unique<ChunkColumn>(pos);
        BlockTypeId type = BlockTypeId::fromName(
            "test:block_" + std::to_string(seed % 4));
        for (int i = 0; i < blocks; ++i) {
            col->setBlock(i % 16, (i / 16) % 64, (seed + i) % 16, type);
        }
        return col;
    }

    // Verify that a column reads back with exactly the content makeColumn produced.
    static void expectColumnMatches(RegionFile& region, ColumnPos pos, int seed,
                                    int blocks, const char* what) {
        auto expectedColumn = makeColumn(pos, seed, blocks);
        ChunkColumn& expected = *expectedColumn;
        auto loaded = region.loadColumn(pos);
        ASSERT_NE(loaded, nullptr) << what << ": column (" << pos.x << ", " << pos.z
                                   << ") failed to load";
        EXPECT_EQ(loaded->nonAirCount(), expected.nonAirCount())
            << what << ": column (" << pos.x << ", " << pos.z << ") block count";
        for (int i = 0; i < blocks; ++i) {
            int bx = i % 16, by = (i / 16) % 64, bz = (seed + i) % 16;
            EXPECT_EQ(loaded->getBlock(bx, by, bz), expected.getBlock(bx, by, bz))
                << what << ": column (" << pos.x << ", " << pos.z << ") block "
                << i << " differs";
        }
    }
};

// ============================================================================
// (a) Reproduction of the reported corruption
// ============================================================================

// Bug report: fresh region dir; save one column three times with the same
// content; reopen the region in a new RegionFile; save a *different* column
// whose payload is no larger; read the first column back -> no error, but the
// second column's block data comes out, because its bytes were written on top
// of the first column's live span.
TEST_F(RegionFreeSpaceTest, SecondColumnMustNotOverwriteLiveSpanOfFirst) {
    const ColumnPos posA{1, 2};
    const ColumnPos posB{3, 4};
    const int seedA = 1;
    const int seedB = 2;
    // A's payload is the larger one; B's is smaller, exactly as reported
    // ("a DIFFERENT column whose serialized payload is no larger than the
    // first one's"), so a free span covering A looks like a valid best fit.
    const int blocksA = 64;
    const int blocksB = 8;

    // Three saves of the same column: save #2 appends, save #3 re-uses the
    // offset freed by save #1. The ToC therefore contains offset X twice, the
    // second time as the *live* entry.
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(region.saveColumn(*makeColumn(posA, seedA, blocksA), posA))
                << "save #" << (i + 1) << " failed";
        }
    }

    // Restart: the free list is rebuilt from the ToC journal.
    {
        RegionFile region(tempDir, RegionPos{0, 0});

        // Sanity: A is intact right after reopening.
        expectColumnMatches(region, posA, seedA, blocksA, "after reopen");

        ASSERT_TRUE(region.saveColumn(*makeColumn(posB, seedB, blocksB), posB));

        // The whole point: A must still be A, checked on block contents and
        // not merely on loadColumn() returning non-null.
        expectColumnMatches(region, posA, seedA, blocksA, "after saving B");
        expectColumnMatches(region, posB, seedB, blocksB, "after saving B");
    }

    // And it must still be true across another restart (the .dat bytes, not
    // just the in-memory index, must be uncorrupted).
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        expectColumnMatches(region, posA, seedA, blocksA, "after second reopen");
        expectColumnMatches(region, posB, seedB, blocksB, "after second reopen");
    }
}

// ============================================================================
// (b) The invariant itself, stated directly
// ============================================================================

// INVARIANT: writing any column never changes the bytes belonging to any other
// live column. Equivalently: allocated spans never overlap live spans.
//
// This is asserted exhaustively rather than for one lucky ordering: after every
// single save, *every* column previously written is read back and compared.
// Restarts are interleaved so both the incremental free list (saveColumnRaw)
// and the rebuilt one (loadToc) are exercised.
TEST_F(RegionFreeSpaceTest, WritingAColumnNeverDisturbsAnyOtherLiveColumn) {
    // (column, seed, blocks) writes, mixing rewrites of the same column with
    // new columns, and varying payload sizes so spans get split and re-used.
    struct Write { ColumnPos pos; int seed; int blocks; };
    const std::vector<Write> writes = {
        {{0, 0}, 1, 16}, {{0, 0}, 2, 16}, {{0, 0}, 3, 16},
        {{1, 0}, 4, 16}, {{2, 0}, 5, 8},  {{1, 0}, 6, 32},
        {{0, 0}, 7, 8},  {{3, 0}, 8, 48}, {{2, 0}, 9, 16},
        {{0, 0}, 10, 64}, {{4, 4}, 11, 16}, {{3, 0}, 12, 8},
        {{1, 0}, 13, 16}, {{5, 5}, 14, 24}, {{4, 4}, 15, 16},
    };

    // Expected live content per column.
    std::map<std::pair<int32_t, int32_t>, std::pair<int, int>> expected;

    for (size_t restartAfter = 0; restartAfter <= writes.size(); ++restartAfter) {
        SCOPED_TRACE("restart after write #" + std::to_string(restartAfter));
        std::filesystem::remove_all(tempDir);
        std::filesystem::create_directories(tempDir);
        expected.clear();

        auto region = std::make_unique<RegionFile>(tempDir, RegionPos{0, 0});
        for (size_t i = 0; i < writes.size(); ++i) {
            if (i == restartAfter) {
                region.reset();  // close
                region = std::make_unique<RegionFile>(tempDir, RegionPos{0, 0});
                // Everything must survive the reopen.
                for (const auto& [xz, sb] : expected) {
                    expectColumnMatches(*region, ColumnPos{xz.first, xz.second},
                                        sb.first, sb.second, "at restart");
                }
            }

            const Write& w = writes[i];
            ASSERT_TRUE(region->saveColumn(*makeColumn(w.pos, w.seed, w.blocks), w.pos));
            expected[{w.pos.x, w.pos.z}] = {w.seed, w.blocks};

            // INVARIANT CHECK: no live column has been disturbed by this write.
            for (const auto& [xz, sb] : expected) {
                expectColumnMatches(*region, ColumnPos{xz.first, xz.second},
                                    sb.first, sb.second,
                                    "after write");
            }
        }

        // Final check through a fresh instance.
        region.reset();
        RegionFile reopened(tempDir, RegionPos{0, 0});
        EXPECT_EQ(reopened.columnCount(), expected.size());
        for (const auto& [xz, sb] : expected) {
            expectColumnMatches(reopened, ColumnPos{xz.first, xz.second},
                                sb.first, sb.second, "final reopen");
        }
    }
}

// ============================================================================
// (c) Space reclamation must still happen
// ============================================================================

// Guards against "fixing" the overlap bug by simply never re-using freed space.
// Rewriting one column with same-sized content many times must reach a steady
// state, not grow the .dat file linearly with the number of saves.
TEST_F(RegionFreeSpaceTest, RepeatedOverwriteDoesNotGrowDataFileWithoutBound) {
    const ColumnPos pos{7, 7};
    const int blocks = 64;

    RegionFile region(tempDir, RegionPos{0, 0});

    ASSERT_TRUE(region.saveColumn(*makeColumn(pos, 0, blocks), pos));
    const uint64_t oneColumnSize = region.dataFileSize();
    ASSERT_GT(oneColumnSize, 0u);

    for (int i = 0; i < 200; ++i) {
        // Same seed => byte-identical payload each time.
        ASSERT_TRUE(region.saveColumn(*makeColumn(pos, 0, blocks), pos));
    }

    // A leak-free allocator settles at a small multiple of one column's size.
    // (Two live-ish spans is what alternating best-fit re-use needs.)
    EXPECT_LE(region.dataFileSize(), oneColumnSize * 4)
        << "data file grew to " << region.dataFileSize() << " bytes for a single "
        << "column of " << oneColumnSize << " bytes: freed space is not being reused";

    expectColumnMatches(region, pos, 0, blocks, "after 201 overwrites");

    // Same requirement across restarts: reopening must not lose track of the
    // reclaimable space and start appending forever.
    uint64_t sizeBefore = region.dataFileSize();
    for (int restart = 0; restart < 5; ++restart) {
        RegionFile reopened(tempDir, RegionPos{0, 0});
        for (int i = 0; i < 20; ++i) {
            ASSERT_TRUE(reopened.saveColumn(*makeColumn(pos, 0, blocks), pos));
        }
        expectColumnMatches(reopened, pos, 0, blocks, "after restart overwrites");
        EXPECT_LE(reopened.dataFileSize(), sizeBefore + oneColumnSize)
            << "restart #" << restart << " leaked space";
        sizeBefore = reopened.dataFileSize();
    }
}

// ============================================================================
// (d) Seeded random operation-sequence test (~100 steps)
// ============================================================================

TEST_F(RegionFreeSpaceTest, SeededRandomOperationSequencePreservesAllLiveColumns) {
    std::mt19937 rng(0xC0FFEE42u);
    std::uniform_int_distribution<int32_t> coordDist(0, 7);
    std::uniform_int_distribution<int> blocksDist(4, 96);
    std::uniform_int_distribution<int> opDist(0, 99);

    struct LiveState {
        int seed;
        int blocks;
        std::vector<uint8_t> rawCbor;
    };
    std::map<std::pair<int32_t, int32_t>, LiveState> liveColumns;

    auto region = std::make_unique<RegionFile>(tempDir, RegionPos{0, 0});

    for (int step = 0; step < 120; ++step) {
        SCOPED_TRACE("step " + std::to_string(step));
        int op = opDist(rng);

        if (op < 15) {
            // Reopen RegionFile from disk (tests ToC replay + free-list rebuild)
            region.reset();
            region = std::make_unique<RegionFile>(tempDir, RegionPos{0, 0});
            EXPECT_NO_THROW(region->validateFreeListInvariants());
        } else if (op < 25) {
            // Compact ToC and validate invariants
            region->compactToc();
            EXPECT_NO_THROW(region->validateFreeListInvariants());
        } else {
            // Save or overwrite a column with varying payload size
            ColumnPos pos{coordDist(rng), coordDist(rng)};
            int seed = step + 1;
            int blocks = blocksDist(rng);
            auto column = makeColumn(pos, seed, blocks);
            ChunkColumn& col = *column;
            auto cbor = ColumnSerializer::toCBOR(col, pos.x, pos.z);

            ASSERT_TRUE(region->saveColumnRaw(pos, cbor));
            EXPECT_NO_THROW(region->validateFreeListInvariants());

            liveColumns[{pos.x, pos.z}] = LiveState{seed, blocks, std::move(cbor)};
        }

        // Periodically verify every live column's exact serialized content
        if (step % 20 == 19 || step == 119) {
            EXPECT_EQ(region->columnCount(), liveColumns.size());
            for (const auto& [xz, state] : liveColumns) {
                ColumnPos pos{xz.first, xz.second};
                expectColumnMatches(*region, pos, state.seed, state.blocks, "random sequence check");
                auto loaded = region->loadColumn(pos);
                ASSERT_NE(loaded, nullptr);
                auto reserialized = ColumnSerializer::toCBOR(*loaded, pos.x, pos.z);
                EXPECT_EQ(reserialized, state.rawCbor);
            }
        }
    }

    // Final reopen and full verification
    region.reset();
    RegionFile finalRegion(tempDir, RegionPos{0, 0});
    EXPECT_NO_THROW(finalRegion.validateFreeListInvariants());
    EXPECT_EQ(finalRegion.columnCount(), liveColumns.size());
    for (const auto& [xz, state] : liveColumns) {
        ColumnPos pos{xz.first, xz.second};
        expectColumnMatches(finalRegion, pos, state.seed, state.blocks, "final reopen check");
    }
}


