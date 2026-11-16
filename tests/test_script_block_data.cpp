// Any script reaching any cell's block data and ticks: block_data_get, block_data_set,
// world_schedule_tick.

#include <gtest/gtest.h>

#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <filesystem>
#include <fstream>

using namespace finevox;
using namespace finevox::script;

class ScriptBlockDataTest : public ::testing::Test {
protected:
    void SetUp() override {
        world_ = std::make_unique<World>();
        world_->setColumnManager(&columns_);
        columns_.add(std::make_unique<ChunkColumn>(ColumnPos{0, 0}));
        world_->setBlock(cell_, BlockTypeId::fromName("block_data_test:box"));
        gse_ = std::make_unique<GameScriptEngine>(*world_);
        ctx_ = std::make_unique<finescript::ExecutionContext>(gse_->engine());
        userData_.world = world_.get();
        ctx_->setUserData(&userData_);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(tempDir_, ec);
    }

    finescript::Value exec(const std::string& cmd) {
        auto result = gse_->engine().executeCommand(cmd, *ctx_);
        EXPECT_TRUE(result.success) << cmd << ": " << result.error;
        return result.returnValue;
    }

    const DataContainer* stored() const {
        const SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
        auto local = cell_.local();
        return sub ? sub->blockData(local.x, local.y, local.z) : nullptr;
    }

    bool dirty() { return columns_.get(ColumnPos{0, 0})->dirty; }
    void clean() { columns_.get(ColumnPos{0, 0})->dirty = false; }

    std::string writeScript(const std::string& name, const std::string& content) {
        std::filesystem::create_directories(tempDir_);
        auto path = tempDir_ / name;
        std::ofstream(path) << content;
        return path.string();
    }

    static uint32_t key(const char* name) { return StringInterner::global().intern(name); }

    BlockCoord cell_{3, 5, 7};
    ColumnManager columns_{16};
    std::unique_ptr<World> world_;
    std::unique_ptr<GameScriptEngine> gse_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;
    ScriptUserData userData_;
    std::filesystem::path tempDir_ =
        std::filesystem::temp_directory_path() / "finevox_block_data_test";
};

TEST_F(ScriptBlockDataTest, SetThenGetEachKindOfValue) {
    EXPECT_TRUE(exec("block_data_set 3 5 7 :level 4").asBool());
    EXPECT_TRUE(exec("block_data_set 3 5 7 \"heat\" 1.5").asBool());
    EXPECT_TRUE(exec("block_data_set [3 5 7] :name \"crate\"").asBool());
    EXPECT_TRUE(exec("block_data_set 3 5 7 :mode :fast").asBool());
    EXPECT_TRUE(exec("block_data_set 3 5 7 :slots [1 2 3]").asBool());

    ASSERT_NE(stored(), nullptr);
    EXPECT_EQ(stored()->get<int64_t>("level"), 4);
    EXPECT_DOUBLE_EQ(stored()->get<double>("heat"), 1.5);

    EXPECT_EQ(exec("block_data_get 3 5 7 :level").asInt(), 4);
    EXPECT_DOUBLE_EQ(exec("block_data_get 3 5 7 :heat").asFloat(), 1.5);
    EXPECT_EQ(exec("block_data_get [3 5 7] \"name\"").asString(), "crate");
    EXPECT_EQ(exec("block_data_get 3 5 7 :mode").asSymbol(), key("fast"));
    auto slots = exec("block_data_get 3 5 7 :slots");
    ASSERT_TRUE(slots.isArray());
    ASSERT_EQ(slots.asArray().size(), 3u);
    EXPECT_EQ(slots.asArray()[2].asInt(), 3);
}

TEST_F(ScriptBlockDataTest, MissingCellsKeysAndDataAreNil) {
    // The cell has no data at all yet
    EXPECT_TRUE(exec("block_data_get 3 5 7 :level").isNil());
    EXPECT_TRUE(exec("block_data_get 3 5 7").isNil());
    exec("block_data_set 3 5 7 :level 1");
    EXPECT_TRUE(exec("block_data_get 3 5 7 :other").isNil());
    // A neighbouring cell of the same subchunk has none of it
    EXPECT_TRUE(exec("block_data_get 4 5 7 :level").isNil());
    // An unloaded column: nothing to read, nothing written
    EXPECT_TRUE(exec("block_data_get 500 5 500 :level").isNil());
    EXPECT_FALSE(exec("block_data_set 500 5 500 :level 1").asBool());
    EXPECT_FALSE(world_->hasColumn(ColumnPos::fromBlock(BlockCoord{500, 5, 500})));
}

TEST_F(ScriptBlockDataTest, NilRemovesAKey) {
    exec("block_data_set 3 5 7 :level 4");
    exec("block_data_set 3 5 7 :keep 1");
    EXPECT_TRUE(exec("block_data_set 3 5 7 :level nil").asBool());
    EXPECT_FALSE(stored()->has("level"));
    EXPECT_TRUE(stored()->has("keep"));
    // Removing what isn't there is fine
    EXPECT_TRUE(exec("block_data_set 3 5 7 :level nil").asBool());
}

TEST_F(ScriptBlockDataTest, WholeCellComesBackAsAPlainMap) {
    exec("block_data_set 3 5 7 :level 4");
    exec("block_data_set 3 5 7 :inner {=count 2 =deeper {=n 9}}");
    auto all = exec("block_data_get 3 5 7");
    ASSERT_TRUE(all.isMap());
    EXPECT_FALSE(all.asMap().isProxy());
    EXPECT_EQ(all.asMap().get(key("level")).asInt(), 4);
    auto inner = all.asMap().get(key("inner"));
    ASSERT_TRUE(inner.isMap());
    EXPECT_FALSE(inner.asMap().isProxy());
    EXPECT_EQ(inner.asMap().get(key("count")).asInt(), 2);
    auto deeper = inner.asMap().get(key("deeper"));
    ASSERT_TRUE(deeper.isMap());
    EXPECT_FALSE(deeper.asMap().isProxy());
    EXPECT_EQ(deeper.asMap().get(key("n")).asInt(), 9);
}

TEST_F(ScriptBlockDataTest, WhatIsReadIsACopy) {
    exec("block_data_set 3 5 7 :inner {=count 2}");
    // Change the map a script read: the cell keeps its own. Then a map kept from before
    // a write still says what it said.
    auto seen = exec(R"(
set m {block_data_get 3 5 7 :inner}
set m.count 99
set a {block_data_get 3 5 7 :inner}.count
block_data_set 3 5 7 :inner {=count 3}
[m.count a {block_data_get 3 5 7 :inner}.count]
)");
    ASSERT_TRUE(seen.isArray());
    ASSERT_EQ(seen.asArray().size(), 3u);
    EXPECT_EQ(seen.asArray()[0].asInt(), 99);
    EXPECT_EQ(seen.asArray()[1].asInt(), 2);
    EXPECT_EQ(seen.asArray()[2].asInt(), 3);
    const DataContainer* inner = stored()->getChild(key("inner"));
    ASSERT_NE(inner, nullptr);
    EXPECT_EQ(inner->get<int64_t>("count"), 3);
}

TEST_F(ScriptBlockDataTest, WritesMarkTheColumnForSavingAndReadsDont) {
    clean();
    exec("block_data_get 3 5 7 :level");
    EXPECT_FALSE(dirty());
    exec("block_data_set 3 5 7 :level 4");
    EXPECT_TRUE(dirty());

    clean();
    exec("block_data_get 3 5 7");
    EXPECT_FALSE(dirty());
    exec("block_data_set 3 5 7 :level nil");
    EXPECT_TRUE(dirty());

    // Removing a key that isn't there changes nothing to save
    clean();
    exec("block_data_set 3 5 7 :level nil");
    EXPECT_FALSE(dirty());
}

TEST_F(ScriptBlockDataTest, BadArgumentsDoNothing) {
    EXPECT_FALSE(exec("block_data_set 3 5 7").asBool());     // no key
    EXPECT_FALSE(exec("block_data_set 3 5 7 4 1").asBool());  // key not a name
    EXPECT_TRUE(exec("block_data_get 3 5 7 4").isNil());
    EXPECT_TRUE(exec("block_data_get 3 5").isNil());
    EXPECT_EQ(stored(), nullptr);
}

TEST_F(ScriptBlockDataTest, ScheduledTickReachesTheBlocksHandler) {
    // The block's own script counts its ticks in its data; game code schedules them
    auto path = writeScript("ticker.fsc", R"(
on :tick do
    if (data.ticks == nil) { set data.ticks 0 }
    set data.ticks (data.ticks + 1)
end
)");
    ASSERT_NE(gse_->loadBlockScript(path, "block_data_test:box"), nullptr);

    // No scheduler: nothing to schedule on
    EXPECT_FALSE(exec("world_schedule_tick 3 5 7 2").asBool());

    UpdateScheduler scheduler(*world_);
    world_->setUpdateScheduler(&scheduler);
    EXPECT_FALSE(scheduler.hasScheduledTick(cell_));
    EXPECT_TRUE(exec("world_schedule_tick 3 5 7 2").asBool());
    EXPECT_TRUE(scheduler.hasScheduledTick(cell_));

    scheduler.advanceGameTick();
    scheduler.processEvents();
    EXPECT_TRUE(exec("block_data_get 3 5 7 :ticks").isNil());  // one tick early
    scheduler.advanceGameTick();
    scheduler.processEvents();
    EXPECT_EQ(exec("block_data_get 3 5 7 :ticks").asInt(), 1);
    EXPECT_FALSE(scheduler.hasScheduledTick(cell_));

    // [x y z] form, and the handler sees what game code wrote
    exec("block_data_set 3 5 7 :ticks 10");
    EXPECT_TRUE(exec("world_schedule_tick [3 5 7] 1").asBool());
    scheduler.advanceGameTick();
    scheduler.processEvents();
    EXPECT_EQ(exec("block_data_get 3 5 7 :ticks").asInt(), 11);

    EXPECT_FALSE(exec("world_schedule_tick 3 5 7").asBool());  // no delay
    EXPECT_FALSE(scheduler.hasScheduledTick(cell_));
    world_->setUpdateScheduler(nullptr);
}
