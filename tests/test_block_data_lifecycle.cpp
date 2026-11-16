// A block's data: lists of anything, saved when written, gone with the block (whose script hears
// of it first, whoever ended it), and block scripts that can call the game's own scripts.

#include <gtest/gtest.h>

#include "finevox/core/block_event.hpp"
#include "finevox/core/block_handler.hpp"
#include "finevox/core/block_mover.hpp"
#include "finevox/core/block_properties.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/serialization.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/script_game_subsystem.hpp"

#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <cstring>
#include <filesystem>
#include <fstream>

using namespace finevox;
using namespace finevox::script;
using finescript::Value;

namespace {

uint32_t sym(const char* name) { return StringInterner::global().intern(name); }

// ============================================================================
// Lists in DataContainer (no world needed)
// ============================================================================

Value symbolValue(const char* name) { return Value::symbol(sym(name)); }

/// An inventory as a script keeps one: empty slots, and [item count] pairs
Value inventory() {
    return Value::array({
        Value::nil(),
        Value::array({symbolValue("stone"), Value::integer(3)}),
        Value::nil(),
        Value::array({symbolValue("clay"), Value::integer(40)}),
    });
}

void expectInventory(const Value& v) {
    ASSERT_TRUE(v.isArray());
    const auto& slots = v.asArray();
    ASSERT_EQ(slots.size(), 4u);
    EXPECT_TRUE(slots[0].isNil());
    ASSERT_TRUE(slots[1].isArray());
    ASSERT_EQ(slots[1].asArray().size(), 2u);
    ASSERT_TRUE(slots[1].asArray()[0].isSymbol());
    EXPECT_EQ(slots[1].asArray()[0].asSymbol(), sym("stone"));
    EXPECT_EQ(slots[1].asArray()[1].asInt(), 3);
    EXPECT_TRUE(slots[2].isNil());
    EXPECT_EQ(slots[3].asArray()[0].asSymbol(), sym("clay"));
    EXPECT_EQ(slots[3].asArray()[1].asInt(), 40);
}

}  // namespace

TEST(BlockDataListTest, AnInventoryIsKeptAndComesBack) {
    DataContainer c;
    DataContainerProxy proxy(c);
    proxy.set(sym("slots"), inventory());
    ASSERT_TRUE(c.has("slots"));
    EXPECT_NE(std::get_if<std::unique_ptr<DataList>>(c.getRaw(sym("slots"))), nullptr);
    expectInventory(proxy.get(sym("slots")));
}

TEST(BlockDataListTest, ListsOfAnythingSurviveCBOR) {
    DataContainer c;
    DataContainerProxy proxy(c);
    proxy.set(sym("slots"), inventory());
    Value inner = Value::map();
    inner.asMap().set(sym("burn"), Value::integer(200));
    proxy.set(sym("mixed"), Value::array({Value::boolean(true), Value::string("x"), Value::number(1.5),
                                          inner, Value::array(std::vector<Value>{}), symbolValue("lit")}));
    proxy.set(sym("flags"), Value::array({Value::boolean(false), Value::boolean(true)}));
    proxy.set(sym("names"), Value::array({symbolValue("a"), symbolValue("b")}));

    auto back = DataContainer::fromCBOR(c.toCBOR());
    ASSERT_NE(back, nullptr);
    DataContainerProxy read(*back);
    expectInventory(read.get(sym("slots")));

    Value mixed = read.get(sym("mixed"));
    ASSERT_TRUE(mixed.isArray());
    const auto& m = mixed.asArray();
    ASSERT_EQ(m.size(), 6u);
    EXPECT_TRUE(m[0].isBool() && m[0].asBool());
    EXPECT_EQ(m[1].asString(), "x");
    EXPECT_DOUBLE_EQ(m[2].asFloat(), 1.5);
    ASSERT_TRUE(m[3].isMap());
    EXPECT_FALSE(m[3].asMap().isProxy());  // A map inside a list is a copy
    EXPECT_EQ(m[3].asMap().get(sym("burn")).asInt(), 200);
    EXPECT_TRUE(m[4].isArray() && m[4].asArray().empty());
    EXPECT_EQ(m[5].asSymbol(), sym("lit"));

    Value flags = read.get(sym("flags"));
    ASSERT_EQ(flags.asArray().size(), 2u);
    EXPECT_TRUE(flags.asArray()[0].isBool());
    EXPECT_FALSE(flags.asArray()[0].asBool());

    // Symbols stay symbols (they once came back as strings)
    Value names = read.get(sym("names"));
    ASSERT_EQ(names.asArray().size(), 2u);
    EXPECT_TRUE(names.asArray()[0].isSymbol());
    EXPECT_EQ(names.asArray()[1].asSymbol(), sym("b"));
}

// What saves held before general lists must read the same
TEST(BlockDataListTest, TypedArraysStillComeBackTyped) {
    DataContainer c;
    c.set<std::vector<int64_t>>("ints", {1, -2, 3});
    c.set<std::vector<double>>("floats", {0.5, 2.0});
    c.set<std::vector<std::string>>("strings", {"a", "b"});
    c.set<std::vector<int64_t>>("empty", {});
    auto back = DataContainer::fromCBOR(c.toCBOR());
    EXPECT_EQ(back->get<std::vector<int64_t>>("ints"), (std::vector<int64_t>{1, -2, 3}));
    EXPECT_EQ(back->get<std::vector<double>>("floats"), (std::vector<double>{0.5, 2.0}));
    EXPECT_EQ(back->get<std::vector<std::string>>("strings"), (std::vector<std::string>{"a", "b"}));
    EXPECT_NE(std::get_if<std::vector<int64_t>>(back->getRaw(sym("empty"))), nullptr);

    // And a script's plain arrays are kept typed too
    DataContainerProxy proxy(c);
    proxy.set(sym("counts"), Value::array({Value::integer(4), Value::integer(5)}));
    EXPECT_NE(std::get_if<std::vector<int64_t>>(c.getRaw(sym("counts"))), nullptr);
}

TEST(BlockDataListTest, CloneIsDeep) {
    DataContainer c;
    DataContainerProxy(c).set(sym("slots"), inventory());
    auto copy = c.clone();
    // Change the original's first pair
    auto* list = std::get_if<std::unique_ptr<DataList>>(c.getRaw(sym("slots")));
    ASSERT_TRUE(list && *list);
    auto* pair = std::get_if<std::unique_ptr<DataList>>(&(*list)->items[1]);
    ASSERT_TRUE(pair && *pair);
    (*pair)->items[1] = int64_t{99};
    expectInventory(DataContainerProxy(*copy).get(sym("slots")));
}

TEST(BlockDataListTest, AFunctionIsNotKept) {
    ASSERT_FALSE(DataContainerProxy::toData(Value::nativeFunction(nullptr)).has_value());
}

// ============================================================================
// In a world: saving, lifetimes, handlers
// ============================================================================

namespace {

class BlockLifecycleTest : public ::testing::Test {
protected:
    void SetUp() override {
        world_ = std::make_unique<World>();
        world_->setColumnManager(&columns_);
        columns_.add(std::make_unique<ChunkColumn>(ColumnPos{0, 0}));
        for (const char* name : {"lc_crate", "lc_kiln", "lc_kiln[lit=true]", "lc_stone", "lc_marker"}) {
            BlockRegistry::global().registerOrUpdateType(name, BlockType{});
        }
        world_->setBlock(marker_, BlockTypeId::fromName("lc_marker"));
        gse_ = std::make_unique<GameScriptEngine>(*world_);
        scheduler_ = std::make_unique<UpdateScheduler>(*world_);
        world_->setUpdateScheduler(scheduler_.get());
        ctx_ = std::make_unique<finescript::ExecutionContext>(gse_->engine());
        userData_.world = world_.get();
        ctx_->setUserData(&userData_);
    }

    void TearDown() override {
        world_->setUpdateScheduler(nullptr);
        ctx_.reset();
        gse_.reset();
        scheduler_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    Value exec(const std::string& code) {
        auto result = gse_->engine().executeCommand(code, *ctx_);
        EXPECT_TRUE(result.success) << code << ": " << result.error;
        return result.returnValue;
    }

    std::string script(const std::string& name, const std::string& text) {
        std::filesystem::create_directories(dir_);
        auto path = dir_ / name;
        std::ofstream(path) << text;
        return path.string();
    }

    /// Crates that, when they end, write what they held and how into the marker block's data
    void loadCrateScript() {
        ASSERT_NE(gse_->loadBlockScript(script("crate.fsc", R"(
on :destroy do
    block_data_set 9 9 9 :spilled data.slots
    block_data_set 9 9 9 :destroyed ({block_data_get 9 9 9 :destroyed} + 1)
end
on :place do
    block_data_set 9 9 9 :placed ({block_data_get 9 9 9 :placed} + 1)
end
)"), "lc_crate"), nullptr);
        exec("block_data_set 9 9 9 :destroyed 0");
        exec("block_data_set 9 9 9 :placed 0");
    }

    void putCrate(BlockCoord at) {
        world_->setBlock(at, BlockTypeId::fromName("lc_crate"));
        exec("block_data_set " + std::to_string(at.x) + " " + std::to_string(at.y) + " " +
             std::to_string(at.z) + " :slots [nil [:stone 3]]");
    }

    int64_t marker(const char* key) { return exec(std::string("block_data_get 9 9 9 :") + key).asInt(); }

    bool dirty() { return columns_.get(ColumnPos{0, 0})->dirty; }
    void clean() { columns_.get(ColumnPos{0, 0})->dirty = false; }

    bool hasData(BlockCoord at) {
        SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(at));
        auto local = at.local();
        return sub && sub->hasBlockData(local.x, local.y, local.z);
    }

    BlockCoord cell_{3, 5, 7};
    BlockCoord marker_{9, 9, 9};
    ColumnManager columns_{16};
    std::unique_ptr<World> world_;
    std::unique_ptr<GameScriptEngine> gse_;
    std::unique_ptr<UpdateScheduler> scheduler_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;
    ScriptUserData userData_;
    std::filesystem::path dir_ = std::filesystem::temp_directory_path() / "finevox_block_lifecycle";
};

}  // namespace

// --- A proxy over a cell finds its data every time ---------------------------

TEST_F(BlockLifecycleTest, ANestedMapIsLiveAndAKeptProxyNeverDangles) {
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));
    DataContainerProxy data(*world_, cell_);
    Value inner = Value::map();
    inner.asMap().set(sym("count"), Value::integer(2));
    data.set(sym("inner"), inner);

    Value live = data.get(sym("inner"));
    ASSERT_TRUE(live.isMap());
    EXPECT_TRUE(live.asMap().isProxy());
    live.asMap().set(sym("count"), Value::integer(5));  // Writes through to the block
    EXPECT_EQ(exec("{block_data_get 3 5 7 :inner}.count").asInt(), 5);

    // The block goes; what a script kept reads nothing and writes nowhere
    world_->setBlock(cell_, BlockTypeId::fromName("lc_stone"));
    EXPECT_FALSE(hasData(cell_));
    EXPECT_TRUE(live.asMap().get(sym("count")).isNil());
    live.asMap().set(sym("count"), Value::integer(7));
    EXPECT_FALSE(hasData(cell_));
    EXPECT_TRUE(data.get(sym("inner")).isNil());

    // And a whole column unloaded: still nothing to read
    DataContainerProxy far(*world_, BlockCoord{900, 5, 900});
    EXPECT_TRUE(far.get(sym("x")).isNil());
    far.set(sym("x"), Value::integer(1));
    EXPECT_TRUE(far.keys().empty());
}

TEST_F(BlockLifecycleTest, RemovingTheLastKeyLeavesNoData) {
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));
    DataContainerProxy data(*world_, cell_);
    data.set(sym("a"), Value::integer(1));
    EXPECT_TRUE(hasData(cell_));
    EXPECT_TRUE(data.remove(sym("a")));
    EXPECT_FALSE(hasData(cell_));
}

// --- Writes mark the column for saving --------------------------------------

TEST_F(BlockLifecycleTest, AScriptWritingItsBlocksDataMarksTheColumn) {
    ASSERT_NE(gse_->loadBlockScript(script("writer.fsc", R"(
on :place do
    set data.slots [nil [:stone 3]]
end
on :tick do
    set data.inner.count 9
end
on :interact do
    data.slots
end
)"), "lc_crate"), nullptr);
    world_->setBlock(cell_, BlockTypeId::fromName("lc_crate"));
    SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
    BlockHandler* handler = BlockRegistry::global().getHandler(BlockTypeId::fromName("lc_crate"));
    ASSERT_NE(handler, nullptr);

    clean();
    BlockContext ctx(*world_, *sub, cell_, cell_.local());
    handler->onPlace(ctx);
    EXPECT_TRUE(dirty());
    Value slots = exec("block_data_get 3 5 7 :slots");
    ASSERT_EQ(slots.asArray().size(), 2u);
    EXPECT_EQ(slots.asArray()[1].asArray()[0].asSymbol(), sym("stone"));

    // A read changes nothing to save
    clean();
    BlockContext readCtx(*world_, *sub, cell_, cell_.local());
    handler->onInteract(readCtx, Face::PosY);
    EXPECT_FALSE(dirty());

    // A write through a nested map does
    exec("block_data_set 3 5 7 :inner {=count 1}");
    clean();
    BlockContext tickCtx(*world_, *sub, cell_, cell_.local());
    handler->onTick(tickCtx, TickType::Scheduled);
    EXPECT_TRUE(dirty());
    EXPECT_EQ(exec("{block_data_get 3 5 7 :inner}.count").asInt(), 9);
}

// Symbols are tagged in CBOR, and a float whose low bits happened to be 27 was once skipped
// twice over: either cut the block's data short, or lost what came after it
TEST_F(BlockLifecycleTest, ABlocksListsSurviveASaveOfItsSubchunk) {
    putCrate(cell_);
    exec("block_data_set 3 5 7 :mode :smelting");
    uint64_t bits = 0x3FF000000000001Bull;
    double awkward;
    std::memcpy(&awkward, &bits, sizeof(awkward));
    DataContainerProxy(*world_, cell_).set(sym("heat"), Value::number(awkward));
    world_->setBlock(BlockCoord{4, 5, 7}, BlockTypeId::fromName("lc_kiln"));
    exec("block_data_set 4 5 7 :after 12");
    SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
    auto bytes = SubChunkSerializer::toCBOR(*sub, 0);
    int32_t y = 0;
    auto loaded = SubChunkSerializer::fromCBOR(bytes, &y);
    ASSERT_NE(loaded, nullptr);
    auto local = cell_.local();
    const DataContainer* data = loaded->blockData(local.x, local.y, local.z);
    ASSERT_NE(data, nullptr);
    Value slots = DataContainerProxy(const_cast<DataContainer&>(*data)).get(sym("slots"));
    ASSERT_TRUE(slots.isArray());
    ASSERT_EQ(slots.asArray().size(), 2u);
    EXPECT_TRUE(slots.asArray()[0].isNil());
    EXPECT_EQ(slots.asArray()[1].asArray()[0].asSymbol(), sym("stone"));
    EXPECT_EQ(slots.asArray()[1].asArray()[1].asInt(), 3);
    EXPECT_EQ(data->get<InternedString>("mode"), InternedString("smelting"));
    EXPECT_EQ(data->get<double>("heat"), awkward);
    auto next = BlockCoord{4, 5, 7}.local();
    const DataContainer* other = loaded->blockData(next.x, next.y, next.z);
    ASSERT_NE(other, nullptr);
    EXPECT_EQ(other->get<int64_t>("after"), 12);
}

// --- Data goes with the block -----------------------------------------------

TEST(BlockFamilyTest, StatesAndNamesOfOneBlockAreOneFamily) {
    auto id = [](const char* n) { return BlockTypeId::fromName(n); };
    EXPECT_TRUE(BlockPropertyRegistry::sameFamily(id("lc:kiln"), id("lc:kiln[lit=true]")));
    EXPECT_TRUE(BlockPropertyRegistry::sameFamily(id("kiln"), id("lc:kiln[lit=true]")));
    EXPECT_FALSE(BlockPropertyRegistry::sameFamily(id("lc:kiln"), id("lc:kiln_cart")));
    EXPECT_FALSE(BlockPropertyRegistry::sameFamily(id("lc:kiln"), id("lc:stone")));
    EXPECT_FALSE(BlockPropertyRegistry::sameFamily(id("lc:kiln"), AIR_BLOCK_TYPE));
    EXPECT_TRUE(BlockPropertyRegistry::sameFamily(AIR_BLOCK_TYPE, AIR_BLOCK_TYPE));
}

TEST_F(BlockLifecycleTest, AStateChangeKeepsTheDataAndAnotherBlockDoesnt) {
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));
    exec("block_data_set 3 5 7 :fuel 8");
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln[lit=true]"));  // It lights up
    EXPECT_EQ(exec("block_data_get 3 5 7 :fuel").asInt(), 8);
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));  // And goes out
    EXPECT_EQ(exec("block_data_get 3 5 7 :fuel").asInt(), 8);
    world_->setBlock(cell_, BlockTypeId::fromName("lc_stone"));
    EXPECT_FALSE(hasData(cell_));
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));
    EXPECT_TRUE(exec("block_data_get 3 5 7 :fuel").isNil());  // A new kiln starts empty
}

// --- The old block's handler hears of its end, with its data -----------------

TEST_F(BlockLifecycleTest, SettingABlockOverAScriptedOneRunsItsDestroy) {
    loadCrateScript();
    putCrate(cell_);
    exec("world_set_block 3 5 7 :lc_stone");
    EXPECT_EQ(marker("destroyed"), 1);
    Value spilled = exec("block_data_get 9 9 9 :spilled");  // What it held, as it was
    ASSERT_TRUE(spilled.isArray());
    ASSERT_EQ(spilled.asArray().size(), 2u);
    EXPECT_TRUE(spilled.asArray()[0].isNil());
    EXPECT_EQ(spilled.asArray()[1].asArray()[0].asSymbol(), sym("stone"));
    EXPECT_FALSE(hasData(cell_));

    // Placing a crate by script runs its place; a state of the same block runs neither
    exec("world_set_block 3 5 7 :lc_crate");
    EXPECT_EQ(marker("placed"), 1);
    exec("world_set_block 3 5 7 :lc_crate");
    EXPECT_EQ(marker("placed"), 1);
    EXPECT_EQ(marker("destroyed"), 1);
}

TEST_F(BlockLifecycleTest, AnExplosionRunsTheDestroyOfWhatItBreaks) {
    loadCrateScript();
    putCrate(cell_);
    exec("explode 3.5 5.5 7.5 3");
    EXPECT_TRUE(world_->getBlock(cell_).isAir());
    EXPECT_EQ(marker("destroyed"), 1);
    Value spilled = exec("block_data_get 9 9 9 :spilled");
    ASSERT_TRUE(spilled.isArray());
    EXPECT_EQ(spilled.asArray()[1].asArray()[1].asInt(), 3);
}

TEST_F(BlockLifecycleTest, ABlockAMoverBreaksRunsItsDestroy) {
    loadCrateScript();
    putCrate(cell_);
    MovePlan plan;
    plan.breaks.push_back(cell_);
    applyMove(*world_, plan, Face::PosX);
    EXPECT_TRUE(world_->getBlock(cell_).isAir());
    EXPECT_EQ(marker("destroyed"), 1);
    EXPECT_EQ(exec("block_data_get 9 9 9 :spilled").asArray()[1].asArray()[1].asInt(), 3);
}

TEST_F(BlockLifecycleTest, PlacingOverAScriptedBlockRunsItsDestroyThenThePlace) {
    loadCrateScript();
    putCrate(cell_);
    scheduler_->pushExternalEvent(BlockEvent::blockPlaced(cell_, BlockTypeId::fromName("lc_stone"),
                                                          BlockTypeId::fromName("lc_crate")));
    scheduler_->processEvents();
    EXPECT_EQ(world_->getBlock(cell_), BlockTypeId::fromName("lc_stone"));
    EXPECT_EQ(marker("destroyed"), 1);
    EXPECT_FALSE(hasData(cell_));
}

TEST_F(BlockLifecycleTest, BreakingStillRunsDestroyOnce) {
    loadCrateScript();
    putCrate(cell_);
    scheduler_->pushExternalEvent(BlockEvent::blockBroken(cell_, BlockTypeId::fromName("lc_crate")));
    scheduler_->processEvents();
    EXPECT_TRUE(world_->getBlock(cell_).isAir());
    EXPECT_EQ(marker("destroyed"), 1);
    EXPECT_FALSE(hasData(cell_));
}

TEST_F(BlockLifecycleTest, AReplacedBlocksTicksAreCancelled) {
    loadCrateScript();
    putCrate(cell_);
    exec("world_schedule_tick 3 5 7 5");
    ASSERT_TRUE(scheduler_->hasScheduledTick(cell_));
    exec("world_set_block 3 5 7 :lc_stone");
    EXPECT_FALSE(scheduler_->hasScheduledTick(cell_));
}

TEST_F(BlockLifecycleTest, BlocksWithoutHandlersAreLeftAlone) {
    world_->setBlock(cell_, BlockTypeId::fromName("lc_kiln"));
    EXPECT_FALSE(beforeBlockReplaced(*world_, cell_, BlockTypeId::fromName("lc_stone")));
    EXPECT_FALSE(afterBlockReplaced(*world_, cell_, AIR_BLOCK_TYPE));
}

// --- Block scripts see the game's scripts ------------------------------------

TEST_F(BlockLifecycleTest, ABlockScriptCallsWhatTheGameScriptsDefine) {
    // The block's script loads first, as content does; the game's scripts after
    ASSERT_NE(gse_->loadBlockScript(script("chute.fsc", R"(
fn mine [] 1
on :tick do
    set data.moved {game_move CAPACITY}
end
)"), "lc_crate"), nullptr);
    ScriptGameSubsystem game(*gse_);
    ASSERT_TRUE(game.loadScript(script("main.fsc", R"(
set CAPACITY 50
fn game_move [n] (n - 1)
)")));

    world_->setBlock(cell_, BlockTypeId::fromName("lc_crate"));
    SubChunk* sub = world_->getSubChunk(ChunkPos::fromBlock(cell_));
    BlockContext ctx(*world_, *sub, cell_, cell_.local());
    BlockRegistry::global().getHandler(BlockTypeId::fromName("lc_crate"))->onTick(ctx, TickType::Scheduled);
    EXPECT_EQ(exec("block_data_get 3 5 7 :moved").asInt(), 49);

    // What the block script defines stays its own
    EXPECT_FALSE(gse_->gameScope()->hasLocal(sym("mine")));
    EXPECT_TRUE(gse_->gameScope()->hasLocal(sym("game_move")));
}
