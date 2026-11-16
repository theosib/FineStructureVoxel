// Starting and stopping games over and over in one process: each stop leaves nothing behind,
// and the next game, the same or another, loads as if into a fresh process.

#include <gtest/gtest.h>

#include "finevox/core/block_type.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/global_registries.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/live_counts.hpp"
#include "finevox/core/world.hpp"
#include "finevox/game/game_runtime.hpp"
#include "finevox/worldgen/biome.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

using namespace finevox;
using namespace finevox::game;
namespace fs = std::filesystem;

namespace {

struct Snapshot {
    int64_t worlds, columns, subchunks, entities, sessions;
    size_t threads;
    size_t heap;
    std::map<std::string, size_t> registries;

    static Snapshot now() {
        Snapshot s{liveCount(Counted::World), liveCount(Counted::ChunkColumn),
                   liveCount(Counted::SubChunk), liveCount(Counted::Entity),
                   liveCount(Counted::GameSession), processThreadCount(), heapBytesInUse(), {}};
        for (auto& [name, size] : GlobalRegistries::sizes()) s.registries[name] += size;
        return s;
    }
};

class GameCyclesTest : public ::testing::Test {
protected:
    void SetUp() override {
        base_ = fs::temp_directory_path() /
                ("finevox_cycles_" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        // Two games with nothing in common: their own namespaces, blocks, biomes and creatures
        writeGame("one", "cycleone", "granite", "meadow", "walker", "30");
        writeGame("two", "cycletwo", "basalt", "dunes", "crawler", "26");
        // Nothing left over from tests before this one
        GlobalRegistries::clearAll();
    }

    void TearDown() override {
        GlobalRegistries::clearAll();
        fs::remove_all(base_);
    }

    void writeGame(const std::string& dir, const std::string& ns, const std::string& rock,
                   const std::string& biome, const std::string& creature,
                   const std::string& height) {
        fs::path root = base_ / dir;
        for (const char* sub : {"scripts", "blocks/base", "biomes", "entities"}) {
            fs::create_directories(root / sub);
        }
        auto write = [&](const std::string& path, const std::string& content) {
            std::ofstream(root / path) << content;
        };
        write("game.conf", "name: Game " + dir + "\nnamespace: " + ns +
                               "\nworld_seed: 3\nworld_radius: 2\nsea_level: 10\n");
        write("blocks/base/solid_cube.model", "collision: full\nrotations: none\n");
        write("blocks/" + rock + ".model", "include: base/solid_cube\n");
        write("blocks/soil.model", "include: base/solid_cube\n");
        write("biomes/" + biome + ".biome",
              "name: " + biome + "\ntemperature_min: 0\ntemperature_max: 1\nhumidity_min: 0\n"
              "humidity_max: 1\nbase_height: " + height + "\nheight_variation: 2\nsurface: soil\n"
              "filler: " + rock + "\nfiller_depth: 2\nstone: " + rock + "\n");
        write("entities/" + creature + ".entity", "name: " + creature + "\nai: none\nmax_speed: 3\n");
        // Something alive, and a script that keeps state, so a cycle has entities and scripts
        write("scripts/main.fsc",
              "on :start do\n"
              "    set spawned {ent_spawn :" + creature + " 4 " + height + " 4}\n"
              "end\n"
              "set ticks 0\n"
              "on :tick do\n"
              "    set ticks (ticks + 1)\n"
              "end\n");
    }

    /// One game from start to stop, both ways the engine runs one: ticks on this thread as
    /// headless does, then on the session's own threads as a client does
    void cycle(const std::string& dir) {
        {
            RuntimeOptions options;
            options.gameRoot = base_ / dir;
            options.noSaves = true;
            GameRuntime runtime(options);
            runtime.load();
            EXPECT_EQ(runtime.gameScript().errorCount(), 0u);

            runtime.session().lightEngine().start();
            runtime.runTicks(10);
            runtime.session().lightEngine().stop();

            runtime.startThreads();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            runtime.stopThreads();

            EXPECT_GE(liveCount(Counted::World), 1);
            EXPECT_GE(liveCount(Counted::SubChunk), 1);
            EXPECT_GE(liveCount(Counted::Entity), 1);
        }
        GlobalRegistries::clearAll();
    }

    static void expectNothingLeft(const Snapshot& before, const Snapshot& after, int cycle) {
        EXPECT_EQ(after.worlds, before.worlds) << "cycle " << cycle;
        EXPECT_EQ(after.columns, before.columns) << "cycle " << cycle;
        EXPECT_EQ(after.subchunks, before.subchunks) << "cycle " << cycle;
        EXPECT_EQ(after.entities, before.entities) << "cycle " << cycle;
        EXPECT_EQ(after.sessions, before.sessions) << "cycle " << cycle;
        if (before.threads != 0) {
            EXPECT_EQ(after.threads, before.threads) << "threads left running after cycle " << cycle;
        }
        for (auto& [name, size] : after.registries) {
            EXPECT_EQ(size, 0u) << name << " after cycle " << cycle;
        }
    }

    fs::path base_;
};

}  // namespace

TEST_F(GameCyclesTest, TheSameGameStartsAndStopsAgainAndAgain) {
    const Snapshot before = Snapshot::now();
    std::vector<size_t> heap;
    for (int i = 0; i < 5; ++i) {
        cycle("one");
        Snapshot after = Snapshot::now();
        expectNothingLeft(before, after, i);
        heap.push_back(after.heap);
    }
    if (heap.front() == 0) GTEST_SKIP() << "no heap statistics on this platform";
    // The first cycle may fill caches that live as long as the process (interned names); after
    // it, the heap stays where it is. A leak of one world a cycle would be megabytes.
    for (size_t i = 2; i < heap.size(); ++i) {
        EXPECT_LT(static_cast<int64_t>(heap[i]) - static_cast<int64_t>(heap[1]), 256 * 1024)
            << "heap grew by " << (static_cast<int64_t>(heap[i]) - static_cast<int64_t>(heap[1]))
            << " bytes from cycle 1 to cycle " << i;
    }
}

TEST_F(GameCyclesTest, TwoGamesInTurnEachSeeOnlyTheirOwnContent) {
    const Snapshot before = Snapshot::now();
    std::vector<size_t> heap;
    for (int i = 0; i < 6; ++i) {
        bool one = i % 2 == 0;
        std::string ns = one ? "cycleone" : "cycletwo";
        std::string other = one ? "cycletwo" : "cycleone";
        {
            RuntimeOptions options;
            options.gameRoot = base_ / (one ? "one" : "two");
            options.noSaves = true;
            GameRuntime runtime(options);
            runtime.load();
            EXPECT_EQ(runtime.gameScript().errorCount(), 0u);
            std::string rock = one ? "granite" : "basalt";
            std::string otherRock = one ? "basalt" : "granite";
            auto& blocks = BlockRegistry::global();
            EXPECT_TRUE(blocks.hasType(ns + ":" + rock)) << "cycle " << i;
            EXPECT_TRUE(blocks.hasType(rock)) << "cycle " << i;
            EXPECT_FALSE(blocks.hasType(other + ":" + otherRock)) << "cycle " << i;
            EXPECT_FALSE(blocks.hasType(otherRock)) << "cycle " << i;
            EXPECT_FALSE(blocks.hasType(other + ":soil")) << "cycle " << i;
            EXPECT_EQ(worldgen::BiomeRegistry::global().size(), 1u) << "cycle " << i;
            // The ground is this game's rock under this game's height
            int height = one ? 30 : 26;
            BlockTypeId below = runtime.session().world().getBlock(8, height - 4, 8);
            EXPECT_EQ(below, BlockTypeId::fromName(rock)) << "cycle " << i << ": " << below.name();
            runtime.runTicks(5);
        }
        GlobalRegistries::clearAll();
        Snapshot after = Snapshot::now();
        expectNothingLeft(before, after, i);
        heap.push_back(after.heap);
    }
    if (heap.front() == 0) GTEST_SKIP() << "no heap statistics on this platform";
    // Each game once fills what lasts the process; from then on, flat
    for (size_t i = 3; i < heap.size(); ++i) {
        EXPECT_LT(static_cast<int64_t>(heap[i]) - static_cast<int64_t>(heap[i - 2]), 256 * 1024)
            << "heap grew by " << (static_cast<int64_t>(heap[i]) - static_cast<int64_t>(heap[i - 2]))
            << " bytes between cycles " << i - 2 << " and " << i;
    }
}
