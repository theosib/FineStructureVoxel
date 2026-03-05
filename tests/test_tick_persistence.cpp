#include <gtest/gtest.h>
#include "finevox/core/tick_journal.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/block_handler.hpp"  // TickType definition
#include "finevox/core/io_manager.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/world.hpp"
#include <filesystem>
#include <atomic>
#include <thread>

using namespace finevox;

class TickPersistenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempDir_ = std::filesystem::temp_directory_path() / "finevox_test_tick_persist";
        std::filesystem::create_directories(tempDir_);

        tickJournalDir_ = tempDir_ / "tick_journals";
        std::filesystem::create_directories(tickJournalDir_);

        journal_.setDirectory(tickJournalDir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    std::filesystem::path tempDir_;
    std::filesystem::path tickJournalDir_;
    TickJournal journal_;
};

// Test: IOManager save thread writes tick journal alongside region data
TEST_F(TickPersistenceTest, IOManagerSaveWritesTickJournal) {
    IOManager io(tempDir_);
    io.setTickJournal(&journal_);
    io.start();

    // Create a column with some blocks
    ChunkColumn col(ColumnPos{2, 3});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    col.setBlock(0, 64, 0, stone);

    // Create ticks for this column
    std::vector<ScheduledTick> ticks;
    ScheduledTick t;
    t.pos = BlockCoord{32 + 5, 64, 48 + 10};  // Column (2, 3)
    t.targetTick = 500;
    t.type = TickType::Scheduled;
    ticks.push_back(t);

    // Save with ticks
    std::atomic<bool> done{false};
    io.queueSave(ColumnPos{2, 3}, col, std::move(ticks),
                  [&](ColumnPos, bool success) {
        EXPECT_TRUE(success);
        done = true;
    });

    io.flush();
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Verify tick journal was written
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{2, 3}));
    auto loaded = journal_.readTicks(ColumnPos{2, 3});
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, 32 + 5);
    EXPECT_EQ(loaded[0].targetTick, 500u);

    io.stop();
}

// Test: IOManager save with empty ticks deletes stale journal
TEST_F(TickPersistenceTest, IOManagerSaveDeletesStaleJournal) {
    // Pre-create a journal
    ScheduledTick old;
    old.pos = BlockCoord{0, 0, 0};
    old.targetTick = 100;
    old.type = TickType::Scheduled;
    journal_.writeTicks(ColumnPos{1, 1}, {old});
    ASSERT_TRUE(journal_.hasJournal(ColumnPos{1, 1}));

    IOManager io(tempDir_);
    io.setTickJournal(&journal_);
    io.start();

    ChunkColumn col(ColumnPos{1, 1});

    // Save with no ticks — should delete journal
    std::atomic<bool> done{false};
    io.queueSave(ColumnPos{1, 1}, col, std::vector<ScheduledTick>{},
                  [&](ColumnPos, bool) { done = true; });

    io.flush();
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    EXPECT_FALSE(journal_.hasJournal(ColumnPos{1, 1}));

    io.stop();
}

// Test: IOManager tick-only save (eviction path) appends to journal
TEST_F(TickPersistenceTest, IOManagerTickOnlySaveAppends) {
    // Pre-create a journal with one tick
    ScheduledTick existing;
    existing.pos = BlockCoord{0, 64, 0};
    existing.targetTick = 100;
    existing.type = TickType::Scheduled;
    journal_.writeTicks(ColumnPos{0, 0}, {existing});

    IOManager io(tempDir_);
    io.setTickJournal(&journal_);
    io.start();

    // Queue tick-only save (eviction)
    ScheduledTick newTick;
    newTick.pos = BlockCoord{5, 32, 10};
    newTick.targetTick = 200;
    newTick.type = TickType::Repeat;

    io.queueTickSave(ColumnPos{0, 0}, {newTick});
    io.flush();
    // Give the save thread time to process
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Journal should now have both ticks (appended)
    auto loaded = journal_.readTicks(ColumnPos{0, 0});
    ASSERT_EQ(loaded.size(), 2u);
    EXPECT_EQ(loaded[0].targetTick, 100u);
    EXPECT_EQ(loaded[1].targetTick, 200u);

    io.stop();
}

// Test: IOManager load thread reads tick journal and delivers via callback
TEST_F(TickPersistenceTest, IOManagerLoadReadsTickJournal) {
    // Pre-create region data
    IOManager ioSetup(tempDir_);
    ioSetup.start();
    ChunkColumn col(ColumnPos{4, 5});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    col.setBlock(0, 0, 0, stone);
    std::atomic<bool> saved{false};
    ioSetup.queueSave(ColumnPos{4, 5}, col, [&](ColumnPos, bool) { saved = true; });
    ioSetup.flush();
    while (!saved) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ioSetup.stop();

    // Pre-create tick journal
    ScheduledTick tick;
    tick.pos = BlockCoord{64 + 3, 40, 80 + 7};  // Column (4, 5)
    tick.targetTick = 999;
    tick.type = TickType::Repeat;
    journal_.writeTicks(ColumnPos{4, 5}, {tick});

    // Now load with tick journal
    IOManager io(tempDir_);
    io.setTickJournal(&journal_);

    std::atomic<bool> ticksReceived{false};
    std::vector<ScheduledTick> receivedTicks;
    io.setTickLoadCallback([&](ColumnPos pos, std::vector<ScheduledTick> ticks) {
        EXPECT_EQ(pos, (ColumnPos{4, 5}));
        receivedTicks = std::move(ticks);
        ticksReceived = true;
    });

    io.start();

    std::atomic<bool> loadDone{false};
    io.requestLoad(ColumnPos{4, 5}, [&](ColumnPos, std::unique_ptr<ChunkColumn> loaded) {
        EXPECT_NE(loaded, nullptr);
        loadDone = true;
    });

    while (!loadDone) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_TRUE(ticksReceived);
    ASSERT_EQ(receivedTicks.size(), 1u);
    EXPECT_EQ(receivedTicks[0].targetTick, 999u);
    EXPECT_EQ(receivedTicks[0].type, TickType::Repeat);

    // Journal should be deleted after load
    EXPECT_FALSE(journal_.hasJournal(ColumnPos{4, 5}));

    io.stop();
}

// Test: Full round-trip — schedule ticks → flush to journal → reload into scheduler
TEST_F(TickPersistenceTest, FullRoundTrip) {
    World world;
    UpdateScheduler scheduler(world);
    scheduler.setCurrentTick(1000);
    scheduler.setTickJournal(&journal_);

    // Schedule some ticks
    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);
    scheduler.scheduleTick(BlockCoord{20, 32, 5}, 100, TickType::Repeat);
    EXPECT_EQ(scheduler.scheduledTickCount(), 2u);

    // Flush all ticks to journal (shutdown path)
    scheduler.flushTicksToJournal();
    EXPECT_EQ(scheduler.scheduledTickCount(), 0u);

    // Verify journals exist
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{0, 0}));
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{1, 0}));

    // Create new scheduler (simulating world reload)
    World world2;
    UpdateScheduler scheduler2(world2);
    scheduler2.setCurrentTick(1000);

    // Read journals and push pending ticks
    auto ticks0 = journal_.readTicks(ColumnPos{0, 0});
    auto ticks1 = journal_.readTicks(ColumnPos{1, 0});

    scheduler2.pushPendingTicks(std::move(ticks0));
    scheduler2.pushPendingTicks(std::move(ticks1));

    // Not yet merged
    EXPECT_EQ(scheduler2.scheduledTickCount(), 0u);

    // Advance tick to merge
    scheduler2.advanceGameTick();
    EXPECT_EQ(scheduler2.scheduledTickCount(), 2u);

    // Verify the ticks have correct target ticks
    auto all = scheduler2.extractAllTicks();
    size_t totalTicks = 0;
    for (const auto& [_, ticks] : all) {
        totalTicks += ticks.size();
    }
    EXPECT_EQ(totalTicks, 2u);
}

// Test: Column eviction extracts ticks via pre-eviction callback
TEST_F(TickPersistenceTest, ColumnEvictionExtractsTicks) {
    World world;
    UpdateScheduler scheduler(world);
    scheduler.setCurrentTick(100);

    IOManager io(tempDir_);
    io.setTickJournal(&journal_);
    io.start();

    // Small cache to force eviction
    ColumnManager cm(2);
    cm.bindIOManager(&io);

    // Set pre-eviction callback to extract ticks
    cm.setPreEvictionCallback([&](ColumnPos pos) {
        auto ticks = scheduler.extractTicksForColumn(pos);
        if (!ticks.empty()) {
            io.queueTickSave(pos, std::move(ticks));
        }
    });

    // Schedule a tick in column (0, 0)
    scheduler.scheduleTick(BlockCoord{5, 64, 10}, 50, TickType::Scheduled);

    // Add 3 columns to force eviction of the first (cache size = 2)
    auto col0 = std::make_unique<ChunkColumn>(ColumnPos{0, 0});
    auto col1 = std::make_unique<ChunkColumn>(ColumnPos{1, 0});
    auto col2 = std::make_unique<ChunkColumn>(ColumnPos{2, 0});

    cm.add(std::move(col0));
    cm.release(ColumnPos{0, 0});  // Move to unload cache

    cm.add(std::move(col1));
    cm.release(ColumnPos{1, 0});  // Move to unload cache

    cm.add(std::move(col2));
    cm.release(ColumnPos{2, 0});  // This should evict col0

    // Wait for IO to process
    io.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Tick should have been extracted from scheduler
    EXPECT_EQ(scheduler.scheduledTickCount(), 0u);

    // And written to journal
    EXPECT_TRUE(journal_.hasJournal(ColumnPos{0, 0}));
    auto loaded = journal_.readTicks(ColumnPos{0, 0});
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].pos.x, 5);
    EXPECT_EQ(loaded[0].targetTick, 150u);

    io.stop();
}
