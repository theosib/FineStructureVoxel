#include <gtest/gtest.h>
#include "finevox/core/tick_journal.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/block_handler.hpp"  // TickType definition
#include "finevox/core/world.hpp"
#include <filesystem>

using namespace finevox;

class TickJournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = std::filesystem::temp_directory_path() / "finevox_test_tick_journals";
        std::filesystem::create_directories(tempDir_);
        journal_.setDirectory(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    ScheduledTick makeTick(int32_t x, int32_t y, int32_t z,
                           uint64_t targetTick, TickType type = TickType::Scheduled) {
        ScheduledTick tick;
        tick.pos = BlockCoord{x, y, z};
        tick.targetTick = targetTick;
        tick.type = type;
        return tick;
    }

    std::filesystem::path tempDir_;
    TickJournal journal_;
};

TEST_F(TickJournalTest, WriteAndReadSingleTick) {
    ColumnPos col{0, 1};
    std::vector<ScheduledTick> ticks = {makeTick(10, 20, 30, 1000)};

    ASSERT_TRUE(journal_.writeTicks(col, ticks));
    ASSERT_TRUE(journal_.hasJournal(col));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, 10);
    EXPECT_EQ(loaded[0].pos.y, 20);
    EXPECT_EQ(loaded[0].pos.z, 30);
    EXPECT_EQ(loaded[0].targetTick, 1000u);
    EXPECT_EQ(loaded[0].type, TickType::Scheduled);
}

TEST_F(TickJournalTest, WriteMultipleTicks) {
    ColumnPos col{5, 5};
    std::vector<ScheduledTick> ticks = {
        makeTick(1, 2, 3, 100),
        makeTick(4, 5, 6, 200),
        makeTick(7, 8, 9, 300),
    };

    ASSERT_TRUE(journal_.writeTicks(col, ticks));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0].pos.x, 1);
    EXPECT_EQ(loaded[0].targetTick, 100u);
    EXPECT_EQ(loaded[1].pos.x, 4);
    EXPECT_EQ(loaded[1].targetTick, 200u);
    EXPECT_EQ(loaded[2].pos.x, 7);
    EXPECT_EQ(loaded[2].targetTick, 300u);
}

TEST_F(TickJournalTest, AppendTicks) {
    ColumnPos col{3, 3};

    std::vector<ScheduledTick> batch1 = {makeTick(1, 0, 0, 100)};
    std::vector<ScheduledTick> batch2 = {makeTick(2, 0, 0, 200)};

    ASSERT_TRUE(journal_.writeTicks(col, batch1));
    ASSERT_TRUE(journal_.appendTicks(col, batch2));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].pos.x, 1);
    EXPECT_EQ(loaded[1].pos.x, 2);
}

TEST_F(TickJournalTest, WriteOverwritesExisting) {
    ColumnPos col{1, 1};

    std::vector<ScheduledTick> first = {makeTick(1, 0, 0, 100), makeTick(2, 0, 0, 200)};
    std::vector<ScheduledTick> second = {makeTick(9, 0, 0, 999)};

    ASSERT_TRUE(journal_.writeTicks(col, first));
    ASSERT_TRUE(journal_.writeTicks(col, second));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, 9);
    EXPECT_EQ(loaded[0].targetTick, 999u);
}

TEST_F(TickJournalTest, WriteEmptyDeletesJournal) {
    ColumnPos col{2, 2};

    std::vector<ScheduledTick> ticks = {makeTick(1, 0, 0, 100)};
    ASSERT_TRUE(journal_.writeTicks(col, ticks));
    ASSERT_TRUE(journal_.hasJournal(col));

    // Writing empty set should delete the file
    ASSERT_TRUE(journal_.writeTicks(col, {}));
    EXPECT_FALSE(journal_.hasJournal(col));
}

TEST_F(TickJournalTest, DeleteJournal) {
    ColumnPos col{2, 3};
    std::vector<ScheduledTick> ticks = {makeTick(0, 64, 0, 500)};
    journal_.writeTicks(col, ticks);

    ASSERT_TRUE(journal_.hasJournal(col));
    ASSERT_TRUE(journal_.deleteJournal(col));
    EXPECT_FALSE(journal_.hasJournal(col));
}

TEST_F(TickJournalTest, ReadNonexistentJournal) {
    ColumnPos col{99, 99};
    auto ticks = journal_.readTicks(col);
    EXPECT_TRUE(ticks.empty());
}

TEST_F(TickJournalTest, HasJournalReturnsFalseForMissing) {
    ColumnPos col{100, 100};
    EXPECT_FALSE(journal_.hasJournal(col));
}

TEST_F(TickJournalTest, JournalPathFormat) {
    ColumnPos col{-3, 7};
    auto path = journal_.journalPath(col);
    EXPECT_EQ(path.filename(), "-3_7.ticks");
}

TEST_F(TickJournalTest, RoundTripAllTickTypes) {
    ColumnPos col{0, 0};
    std::vector<ScheduledTick> ticks = {
        makeTick(0, 0, 0, 100, TickType::Scheduled),
        makeTick(1, 0, 0, 200, TickType::Repeat),
        makeTick(2, 0, 0, 300, TickType::Random),
    };

    ASSERT_TRUE(journal_.writeTicks(col, ticks));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 3u);
    EXPECT_EQ(loaded[0].type, TickType::Scheduled);
    EXPECT_EQ(loaded[1].type, TickType::Repeat);
    EXPECT_EQ(loaded[2].type, TickType::Random);
}

TEST_F(TickJournalTest, LargeTickCount) {
    ColumnPos col{0, 0};
    std::vector<ScheduledTick> ticks;
    for (int i = 0; i < 1000; ++i) {
        ticks.push_back(makeTick(i % 16, i / 16, 0, 1000 + i));
    }

    ASSERT_TRUE(journal_.writeTicks(col, ticks));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 1000u);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(loaded[i].pos.x, i % 16);
        EXPECT_EQ(loaded[i].targetTick, static_cast<uint64_t>(1000 + i));
    }
}

TEST_F(TickJournalTest, NegativeCoordinates) {
    ColumnPos col{-10, -20};
    std::vector<ScheduledTick> ticks = {makeTick(-100, -50, -200, 5000)};

    ASSERT_TRUE(journal_.writeTicks(col, ticks));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, -100);
    EXPECT_EQ(loaded[0].pos.y, -50);
    EXPECT_EQ(loaded[0].pos.z, -200);
}

TEST_F(TickJournalTest, MaxTickValues) {
    ColumnPos col{0, 0};
    ScheduledTick tick;
    tick.pos = BlockCoord{INT32_MAX, INT32_MIN, 0};
    tick.targetTick = UINT64_MAX;
    tick.type = TickType::Repeat;

    ASSERT_TRUE(journal_.writeTicks(col, {tick}));

    auto loaded = journal_.readTicks(col);
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, INT32_MAX);
    EXPECT_EQ(loaded[0].pos.y, INT32_MIN);
    EXPECT_EQ(loaded[0].targetTick, UINT64_MAX);
    EXPECT_EQ(loaded[0].type, TickType::Repeat);
}

// Integration: scheduler copy/extract + journal round-trip
TEST_F(TickJournalTest, SchedulerCopyTicksForColumn) {
    World world;
    UpdateScheduler scheduler(world);

    // Schedule ticks in column (0, 0): block coords 0-15 in X and Z
    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{3, 32, 7}, 100, TickType::Repeat);

    // Schedule a tick in a different column (1, 0)
    scheduler.scheduleTick(BlockCoord{20, 64, 5}, 75, TickType::Scheduled);

    // Copy ticks for column (0, 0) — should get 2 ticks
    auto ticks = scheduler.copyTicksForColumn(ColumnPos{0, 0});
    ASSERT_EQ(ticks.size(), 2u);

    // Ticks should still be in the scheduler
    EXPECT_EQ(scheduler.scheduledTickCount(), 3u);

    // Write to journal and read back
    ASSERT_TRUE(journal_.writeTicks(ColumnPos{0, 0}, ticks));
    auto loaded = journal_.readTicks(ColumnPos{0, 0});
    ASSERT_EQ(loaded.size(), 2u);
}

TEST_F(TickJournalTest, SchedulerExtractTicksForColumn) {
    World world;
    UpdateScheduler scheduler(world);

    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{3, 32, 7}, 100, TickType::Repeat);
    scheduler.scheduleTick(BlockCoord{20, 64, 5}, 75, TickType::Scheduled);

    // Extract ticks for column (0, 0) — should remove 2, leave 1
    auto ticks = scheduler.extractTicksForColumn(ColumnPos{0, 0});
    ASSERT_EQ(ticks.size(), 2u);
    EXPECT_EQ(scheduler.scheduledTickCount(), 1u);
}

TEST_F(TickJournalTest, SchedulerPushAndMergePendingTicks) {
    World world;
    UpdateScheduler scheduler(world);

    // Push ticks from "IO thread" (simulated)
    std::vector<ScheduledTick> ticks;
    ScheduledTick t;
    t.pos = BlockCoord{5, 64, 10};
    t.targetTick = 50;
    t.type = TickType::Scheduled;
    ticks.push_back(t);

    scheduler.pushPendingTicks(std::move(ticks));

    // Not yet merged
    EXPECT_EQ(scheduler.scheduledTickCount(), 0u);

    // advanceGameTick triggers merge
    scheduler.advanceGameTick();
    EXPECT_EQ(scheduler.scheduledTickCount(), 1u);
}

TEST_F(TickJournalTest, SchedulerExtractAllTicks) {
    World world;
    UpdateScheduler scheduler(world);

    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{20, 64, 5}, 75, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{20, 32, 8}, 100, TickType::Repeat);

    auto allTicks = scheduler.extractAllTicks();
    EXPECT_EQ(scheduler.scheduledTickCount(), 0u);

    // Column (0, 0) should have 1 tick, column (1, 0) should have 2
    ColumnPos col00{0, 0};
    ColumnPos col10{1, 0};
    EXPECT_EQ(allTicks[col00].size(), 1u);
    EXPECT_EQ(allTicks[col10].size(), 2u);
}

TEST_F(TickJournalTest, FlushTicksToJournal) {
    World world;
    UpdateScheduler scheduler(world);
    scheduler.setTickJournal(&journal_);

    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{20, 64, 5}, 75, TickType::Scheduled);

    scheduler.flushTicksToJournal();

    // Scheduler should be empty
    EXPECT_EQ(scheduler.scheduledTickCount(), 0u);

    // Both columns should have journals
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{0, 0}));
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{1, 0}));

    auto ticks0 = journal_.readTicks(ColumnPos{0, 0});
    auto ticks1 = journal_.readTicks(ColumnPos{1, 0});
    EXPECT_EQ(ticks0.size(), 1u);
    EXPECT_EQ(ticks1.size(), 1u);
}

TEST_F(TickJournalTest, SetCurrentTick) {
    World world;
    UpdateScheduler scheduler(world);

    scheduler.setCurrentTick(500);
    EXPECT_EQ(scheduler.currentTick(), 500u);

    // Schedule a tick 10 ticks from now — should target tick 511
    scheduler.scheduleTick(BlockCoord{0, 0, 0}, 10, TickType::Scheduled);

    auto ticks = scheduler.copyTicksForColumn(ColumnPos{0, 0});
    ASSERT_EQ(ticks.size(), 1u);
    EXPECT_EQ(ticks[0].targetTick, 510u);  // currentTick(500) + ticksFromNow(10)
}
