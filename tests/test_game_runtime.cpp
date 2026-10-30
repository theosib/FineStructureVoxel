#include <gtest/gtest.h>
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
