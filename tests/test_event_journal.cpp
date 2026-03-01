#include <gtest/gtest.h>
#include "finevox/core/event_journal.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/block_event.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include <filesystem>

using namespace finevox;

class EventJournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temporary directory for journal files
        tempDir_ = std::filesystem::temp_directory_path() / "finevox_test_journals";
        std::filesystem::create_directories(tempDir_);
        journal_.setDirectory(tempDir_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    std::filesystem::path tempDir_;
    EventJournal journal_;
};

TEST_F(EventJournalTest, WriteAndReadSingleEvent) {
    BlockEvent event = BlockEvent::blockUpdate(BlockCoord{10, 20, 30});
    ColumnPos col{0, 1};

    ASSERT_TRUE(journal_.appendEvent(col, event));
    ASSERT_TRUE(journal_.hasJournal(col));

    auto events = journal_.readEvents(col);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::BlockUpdate);
    EXPECT_EQ(events[0].pos.x, 10);
    EXPECT_EQ(events[0].pos.y, 20);
    EXPECT_EQ(events[0].pos.z, 30);
}

TEST_F(EventJournalTest, AppendMultipleEvents) {
    ColumnPos col{5, 5};

    BlockEvent e1 = BlockEvent::blockUpdate(BlockCoord{1, 2, 3});
    BlockEvent e2 = BlockEvent::blockUpdate(BlockCoord{4, 5, 6});
    BlockEvent e3 = BlockEvent::blockUpdate(BlockCoord{7, 8, 9});

    ASSERT_TRUE(journal_.appendEvent(col, e1));
    ASSERT_TRUE(journal_.appendEvent(col, e2));
    ASSERT_TRUE(journal_.appendEvent(col, e3));

    auto events = journal_.readEvents(col);
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].pos.x, 1);
    EXPECT_EQ(events[1].pos.x, 4);
    EXPECT_EQ(events[2].pos.x, 7);
}

TEST_F(EventJournalTest, DeleteJournalAfterLoad) {
    ColumnPos col{2, 3};
    BlockEvent event = BlockEvent::blockUpdate(BlockCoord{0, 64, 0});
    journal_.appendEvent(col, event);

    ASSERT_TRUE(journal_.hasJournal(col));
    ASSERT_TRUE(journal_.deleteJournal(col));
    EXPECT_FALSE(journal_.hasJournal(col));
}

TEST_F(EventJournalTest, ReadNonexistentJournal) {
    ColumnPos col{99, 99};
    auto events = journal_.readEvents(col);
    EXPECT_TRUE(events.empty());
}

TEST_F(EventJournalTest, HasJournalReturnsFalseForMissing) {
    ColumnPos col{100, 100};
    EXPECT_FALSE(journal_.hasJournal(col));
}

TEST_F(EventJournalTest, JournalPathFormat) {
    ColumnPos col{-3, 7};
    auto path = journal_.journalPath(col);
    EXPECT_EQ(path.filename(), "-3_7.journal");
}

TEST_F(EventJournalTest, BatchAppend) {
    ColumnPos col{0, 0};
    std::vector<BlockEvent> events;
    for (int i = 0; i < 5; ++i) {
        events.push_back(BlockEvent::blockUpdate(BlockCoord{i, 64, 0}));
    }

    ASSERT_TRUE(journal_.appendEvents(col, events));

    auto loaded = journal_.readEvents(col);
    ASSERT_EQ(loaded.size(), 5u);
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(loaded[i].pos.x, i);
    }
}

TEST_F(EventJournalTest, RoundTripFluidEvent) {
    ColumnPos col{1, 1};
    FluidTypeId waterId = FluidTypeId::fromName("journal_test_water");

    BlockEvent event = BlockEvent::fluidPlaced(BlockCoord{5, 50, 5}, waterId, 15);
    ASSERT_TRUE(journal_.appendEvent(col, event));

    auto events = journal_.readEvents(col);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::FluidPlaced);
    EXPECT_EQ(events[0].pos.x, 5);
    EXPECT_EQ(events[0].pos.y, 50);
    EXPECT_EQ(events[0].pos.z, 5);
    EXPECT_EQ(events[0].fluidType, waterId);
    EXPECT_EQ(events[0].fluidLevel, 15);
}

// Integration test: UpdateScheduler writes deferred events to journal
TEST_F(EventJournalTest, SchedulerDeferWritesToJournal) {
    World world;
    UpdateScheduler scheduler(world);
    scheduler.setEventJournal(&journal_);

    // Push a BlockUpdate event for a chunk that doesn't exist
    // (not loaded → will be deferred)
    BlockEvent event = BlockEvent::blockUpdate(BlockCoord{1000, 64, 2000});
    scheduler.pushExternalEvent(event);
    scheduler.processEvents();

    // Should have been deferred and journaled
    ColumnPos col{1000 >> 4, 2000 >> 4};
    EXPECT_TRUE(journal_.hasJournal(col));

    auto events = journal_.readEvents(col);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, EventType::BlockUpdate);
    EXPECT_EQ(events[0].pos.x, 1000);
}

// Test flushDeferredToJournal
TEST_F(EventJournalTest, FlushDeferredToJournal) {
    World world;
    UpdateScheduler scheduler(world);
    // Don't set journal initially — events get deferred in-memory only
    BlockEvent e1 = BlockEvent::blockUpdate(BlockCoord{100, 64, 200});
    BlockEvent e2 = BlockEvent::blockUpdate(BlockCoord{300, 64, 400});
    scheduler.pushExternalEvent(e1);
    scheduler.pushExternalEvent(e2);
    scheduler.processEvents();

    EXPECT_EQ(scheduler.deferredEventCount(), 2u);

    // Now set journal and flush
    scheduler.setEventJournal(&journal_);
    scheduler.flushDeferredToJournal();

    // Both should be journaled
    ColumnPos col1{100 >> 4, 200 >> 4};
    ColumnPos col2{300 >> 4, 400 >> 4};
    EXPECT_TRUE(journal_.hasJournal(col1));
    EXPECT_TRUE(journal_.hasJournal(col2));
}
