#include <gtest/gtest.h>
#include "finevox/script/value_graph.hpp"
#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/game/game_runtime.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>

#include <filesystem>
#include <fstream>

using namespace finevox;
using namespace finevox::game;
namespace fs = std::filesystem;

namespace {

/// A complete minimal game on disk, as a game author would write one
class GameRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = fs::temp_directory_path() /
               ("finevox_runtime_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fs::create_directories(root / "scripts");
        fs::create_directories(root / "blocks" / "base");
        fs::create_directories(root / "biomes");
        fs::create_directories(root / "entities");
        fs::create_directories(root / "blueprints");

        write("game.conf",
              "name: Test Game\nnamespace: testgame\nworld_seed: 7\nworld_radius: 2\n"
              "sea_level: 20\nfog_of_war: true\n");
        write("blocks/base/solid_cube.model", "collision: full\nrotations: none\n");
        write("blocks/stone.model", "include: base/solid_cube\ntexture: blocks/stone\n");
        write("blocks/grass.model", "include: base/solid_cube\n");
        write("biomes/flat.biome",
              "name: Flat\ntemperature_min: 0\ntemperature_max: 1\nhumidity_min: 0\n"
              "humidity_max: 1\nbase_height: 30\nheight_variation: 1\nsurface: grass\n"
              "filler: stone\nfiller_depth: 1\nstone: stone\n");
        write("entities/walker.entity", "name: walker\nai: none\nmax_speed: 4\n");
        write("blueprints/hut.blueprint", "s = stone\n---\nss\nss\n");
    }

    void TearDown() override {
        runtime.reset();
        fs::remove_all(root);
    }

    void write(const std::string& path, const std::string& content) {
        std::ofstream(root / path) << content;
    }

    GameRuntime& boot(const std::string& mainScript) {
        write("scripts/main.fsc", mainScript);
        RuntimeOptions options;
        options.gameRoot = root;
        runtime = std::make_unique<GameRuntime>(options);
        runtime->load();
        return *runtime;
    }

    fs::path root;
    std::unique_ptr<GameRuntime> runtime;
};

}  // namespace

TEST_F(GameRuntimeTest, RefusesADirectoryThatIsNotAGame) {
    fs::remove(root / "game.conf");
    RuntimeOptions options;
    options.gameRoot = root;
    GameRuntime game(options);
    EXPECT_THROW(game.load(), std::runtime_error);
}

TEST_F(GameRuntimeTest, GeneratesTheWorldTheManifestAsksFor) {
    auto& game = boot("");
    EXPECT_EQ(game.manifest().name, "Test Game");
    EXPECT_FALSE(game.session().world().getBlock(8, 10, 8).isAir());
    EXPECT_TRUE(game.session().world().getBlock(64, 10, 8).isAir()) << "beyond world_radius";
}

TEST_F(GameRuntimeTest, RunsStartAndTickHandlers) {
    auto& game = boot(R"(
set started false
set ticks 0
on :start do
    set started true
end
on :tick do
    set ticks (ticks + 1)
end
)");
    game.runTicks(5);
    auto& ctx = game.gameScript().context();
    EXPECT_TRUE(ctx.get("started").asBool());
    EXPECT_EQ(ctx.get("ticks").asInt(), 5);
    EXPECT_EQ(game.gameScript().errorCount(), 0u);
}

TEST_F(GameRuntimeTest, CommandsReachTheGameScriptThroughTheWire) {
    auto& game = boot(R"(
set got nil
on :command do
    set got cmd.value
end
)");
    auto command = finescript::Value::map();
    command.asMap().set(StringInterner::global().intern("value"), finescript::Value::integer(42));
    game.session().actions().sendGameCommand(command);
    game.runTicks(2);
    EXPECT_EQ(game.gameScript().context().get("got").asInt(), 42);
}

TEST_F(GameRuntimeTest, PublishedStateIsCopiedForTheClient) {
    auto& game = boot(R"(
on :tick do
    ui_publish {=hello 7}
end
)");
    game.runTicks(1);
    auto messages = game.uiOutbox().drainAll();
    ASSERT_EQ(messages.size(), 1u);
    script::FineVoxInterner interner;
    auto decoded = finescript::cborDecode(messages[0], interner);
    EXPECT_EQ(decoded.asMap().get(StringInterner::global().intern("hello")).asInt(), 7);
}

TEST_F(GameRuntimeTest, ABrokenHandlerReportsOnceAndTheGameContinues) {
    auto& game = boot(R"(
set ticks 0
on :tick do
    set ticks (ticks + 1)
    let nothing nil
    let boom nothing[0]
end
)");
    game.runTicks(10);
    EXPECT_EQ(game.gameScript().errorCount(), 10u);
    EXPECT_EQ(game.gameScript().errors().size(), 1u) << "one distinct message";
    EXPECT_EQ(game.gameScript().context().get("ticks").asInt(), 10);
}

TEST_F(GameRuntimeTest, AMainScriptThatWontParseStopsTheGame) {
    write("scripts/main.fsc", "if if if");
    RuntimeOptions options;
    options.gameRoot = root;
    GameRuntime game(options);
    EXPECT_THROW(game.load(), std::exception);
}

TEST_F(GameRuntimeTest, ScriptsCanExitTheRun) {
    auto& game = boot(R"(
on :tick do
    game_exit 3
end
)");
    game.runTicks(100);
    EXPECT_TRUE(game.exitRequested());
    EXPECT_EQ(game.exitCode(), 3);
    EXPECT_EQ(game.gameScript().tickCount(), 1u);
}

TEST_F(GameRuntimeTest, UnitNativesAddressEntitiesById) {
    auto& game = boot(R"(
set unit nil
set found nil
on :start do
    set unit {ent_spawn :walker 8.5 32 8.5 =faction 3}
    set found {ent_find 8.5 32 8.5 5 =faction 3}
end
)");
    auto& ctx = game.gameScript().context();
    ASSERT_TRUE(ctx.get("unit").isInt());
    EntityId id = static_cast<EntityId>(ctx.get("unit").asInt());
    ASSERT_NE(game.session().entities().getMob(id), nullptr);
    EXPECT_EQ(game.session().entities().getMob(id)->faction(), 3);
}

TEST_F(GameRuntimeTest, BlueprintSitesWorkFromScript) {
    auto& game = boot(R"(
set site nil
set progress nil
on :start do
    set site {site_create :hut 8 40 8}
    let cell {site_claim site 1 8 40 8}
    world_set_block cell.x cell.y cell.z cell.block
    set progress {site_progress site}
end
)");
    auto& ctx = game.gameScript().context();
    auto progress = ctx.get("progress");
    ASSERT_TRUE(progress.isMap());
    auto& si = StringInterner::global();
    EXPECT_EQ(progress.asMap().get(si.intern("done")).asInt(), 1);
    EXPECT_EQ(progress.asMap().get(si.intern("total")).asInt(), 4);
}

TEST_F(GameRuntimeTest, AirSymbolMeansRealAir) {
    auto& game = boot(R"(
on :start do
    world_set_block 8 40 8 :stone
    world_set_block 8 40 8 :air
end
)");
    EXPECT_TRUE(game.session().world().getBlock(8, 40, 8).isAir());
}

// ============================================================================
// Saving and resuming
// ============================================================================

namespace {

finescript::Value evaluate(finescript::ScriptEngine& engine, const char* source) {
    finescript::ExecutionContext context(engine);
    auto result = engine.executeCommand(source, context);
    EXPECT_TRUE(result.success) << result.error;
    return result.returnValue;
}

}  // namespace

TEST(ValueGraph, KeepsEveryKindOfValue) {
    finescript::ScriptEngine engine;
    auto value = evaluate(engine, R"({=i 3 =f 2.5 =whole 2.0 =s "text" =sym :timber =yes true =none nil =list [1 [2]]})");
    auto back = script::decodeValueGraph(script::encodeValueGraph(value, engine.interner()), engine.interner());

    auto& map = back.asMap();
    auto at = [&](const char* name) { return map.get(engine.intern(name)); };
    EXPECT_TRUE(at("i").isInt());
    EXPECT_EQ(at("i").asInt(), 3);
    EXPECT_TRUE(at("f").isFloat());
    EXPECT_EQ(at("f").asFloat(), 2.5);
    EXPECT_TRUE(at("whole").isFloat()) << "2.0 must not come back as an integer";
    EXPECT_EQ(at("s").asString(), "text");
    EXPECT_EQ(at("sym").asSymbol(), engine.intern("timber"));
    EXPECT_TRUE(at("yes").asBool());
    EXPECT_TRUE(at("none").isNil());
    EXPECT_EQ(at("list").asArray()[1].asArray()[0].asInt(), 2);
}

TEST(ValueGraph, TwoReferencesToOneThingStayOneThing) {
    finescript::ScriptEngine engine;
    auto shared = finescript::Value::map();
    auto root = finescript::Value::array(std::vector<finescript::Value>{shared, shared});

    auto back = script::decodeValueGraph(script::encodeValueGraph(root, engine.interner()), engine.interner());
    finescript::Value first = back.asArray()[0];
    finescript::Value second = back.asArray()[1];
    first.asMap().set(engine.intern("mark"), finescript::Value::integer(1));
    EXPECT_EQ(second.asMap().get(engine.intern("mark")).asInt(), 1);
}

TEST(ValueGraph, ContainersCanHoldThemselves) {
    finescript::ScriptEngine engine;
    auto node = finescript::Value::map();
    node.asMap().set(engine.intern("self"), node);

    auto back = script::decodeValueGraph(script::encodeValueGraph(node, engine.interner()), engine.interner());
    auto again = back.asMap().get(engine.intern("self"));
    EXPECT_EQ(&again.asMap(), &back.asMap());
}

TEST(ValueGraph, AFunctionCannotBeSaved) {
    finescript::ScriptEngine engine;
    auto value = evaluate(engine, "fn [] do return 1 end");
    EXPECT_THROW((void)script::encodeValueGraph(value, engine.interner()), std::runtime_error);
}

namespace {

const char* const SAVING_SCRIPT = R"(
set record {=count 0}
on :start do
    set record.count 41
    set record.unit {ent_spawn :walker 1.5 40 2.5 =faction 2}
    set record.site {site_create :hut 3 40 3}
    world_set_block 5 45 5 :stone
end
on :tick do
    set record.count (record.count + 1)
end
on :save do
    game_state_store record
end
on :resume do
    set record {game_state_load}
end
)";

}  // namespace

class SavedGameTest : public GameRuntimeTest {
protected:
    GameRuntime& bootWith(const fs::path& save, const std::string& script, bool fresh) {
        write("scripts/main.fsc", script);
        RuntimeOptions options;
        options.gameRoot = root;
        options.savePath = save;
        options.newGame = fresh;
        runtime = std::make_unique<GameRuntime>(options);
        runtime->load();
        return *runtime;
    }

    fs::path slot() { return root / "saves" / "slot"; }
};

TEST_F(SavedGameTest, AGameComesBackWhereItWas) {
    GameRuntime& first = bootWith(slot(), SAVING_SCRIPT, true);
    EXPECT_FALSE(first.resumed());
    first.runTicks(9);
    auto& scene = first.session().world();
    BlockTypeId stone = scene.getBlock(5, 45, 5);
    ASSERT_FALSE(stone.isAir());
    EntityId unit = 0;
    for (const auto& [id, entity] : first.session().entities().entities()) unit = id;
    ASSERT_NE(unit, 0u);

    std::string error;
    ASSERT_TRUE(first.saveGame(error)) << error;
    ASSERT_TRUE(fs::is_regular_file(slot() / "game.cbor"));
    runtime.reset();

    GameRuntime& second = bootWith(slot(), SAVING_SCRIPT, false);
    EXPECT_TRUE(second.resumed());
    EXPECT_EQ(second.gameScript().tickCount(), 9u);
    EXPECT_EQ(second.session().world().getBlock(5, 45, 5), stone);

    const Entity* entity = second.session().entities().getEntity(unit);
    ASSERT_NE(entity, nullptr) << "scripts hold this id, so it has to survive";
    EXPECT_EQ(entity->faction(), 2);
    EXPECT_EQ(second.sites().size(), 1u);

    second.runTicks(1);
    finescript::Value record = second.gameScript().context().get("record");
    ASSERT_TRUE(record.isMap());
    EXPECT_EQ(record.asMap().get(second.scripts().engine().intern("count")).asInt(), 51);

    // New things don't take the id of anything restored
    EntityId fresh = second.session().entities().spawnEntity(
        std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName("walker")));
    EXPECT_GT(fresh, unit);
}

TEST_F(SavedGameTest, AFailedSaveLeavesTheLastGoodOneAlone) {
    GameRuntime& first = bootWith(slot(), SAVING_SCRIPT, true);
    first.runTicks(3);
    std::string error;
    ASSERT_TRUE(first.saveGame(error)) << error;
    auto written = fs::last_write_time(slot() / "game.cbor");
    runtime.reset();

    std::string broken = std::string(SAVING_SCRIPT) + R"(
on :save do
    game_state_store fn [] do return 1 end
end
)";
    GameRuntime& second = bootWith(slot(), broken, true);
    EXPECT_FALSE(second.saveGame(error));
    EXPECT_NE(error.find("save"), std::string::npos) << error;
    EXPECT_EQ(fs::last_write_time(slot() / "game.cbor"), written);
    EXPECT_FALSE(fs::exists(fs::path(slot()).concat(".tmp")));
}

TEST_F(SavedGameTest, ThePreviousSaveIsKept) {
    GameRuntime& game = bootWith(slot(), SAVING_SCRIPT, true);
    std::string error;
    ASSERT_TRUE(game.saveGame(error)) << error;
    ASSERT_TRUE(game.saveGame(error)) << error;
    EXPECT_TRUE(fs::is_regular_file(fs::path(slot()).concat(".bak") / "game.cbor"));
}

TEST_F(SavedGameTest, NothingIsWrittenWithoutASlot) {
    GameRuntime& game = boot("on :start do\nend\n");
    std::string error;
    EXPECT_TRUE(game.saveGame(error));
    EXPECT_FALSE(game.resumed());
    EXPECT_FALSE(fs::exists(root / "saves"));
}

TEST_F(SavedGameTest, NoSavesLeavesAnExistingGameAlone) {
    GameRuntime& first = bootWith(slot(), SAVING_SCRIPT, true);
    std::string error;
    ASSERT_TRUE(first.saveGame(error)) << error;
    auto written = fs::last_write_time(slot() / "game.cbor");
    runtime.reset();

    write("scripts/main.fsc", SAVING_SCRIPT);
    RuntimeOptions options;
    options.gameRoot = root;
    options.savePath = slot();
    options.noSaves = true;
    runtime = std::make_unique<GameRuntime>(options);
    runtime->load();
    EXPECT_FALSE(runtime->resumed());
    EXPECT_TRUE(runtime->saveGame(error));
    EXPECT_EQ(fs::last_write_time(slot() / "game.cbor"), written);
}
