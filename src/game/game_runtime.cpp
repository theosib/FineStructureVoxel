#include "finevox/game/game_runtime.hpp"

#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/world.hpp"
#include "finevox/worldgen/biome_loader.hpp"
#include "finevox/worldgen/biome_map.hpp"
#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"

#include <finescript/resource_finder.h>
#include <finescript/script_engine.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

namespace finevox::game {

namespace fs = std::filesystem;

namespace {

// `source "x"` in game scripts resolves against the game root
class GameRootFinder : public finescript::ResourceFinder {
public:
    explicit GameRootFinder(fs::path root) : root_(std::move(root)) {}

    fs::path resolve(std::string_view name) override {
        for (const char* suffix : {"", ".fsc", ".fs"}) {
            fs::path candidate = root_ / (std::string(name) + suffix);
            if (fs::is_regular_file(candidate)) return candidate;
        }
        return {};
    }

private:
    fs::path root_;
};

// Runs as a subsystem so work claims expire on game time
class WorkClaimClock : public GameSubsystem {
public:
    explicit WorkClaimClock(WorkClaimRegistry& claims) : claims_(claims) {}
    [[nodiscard]] std::string_view name() const override { return "WorkClaimClock"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PreTick; }
    void tick(float /*dt*/) override { claims_.setTick(++tick_); }

private:
    WorkClaimRegistry& claims_;
    uint64_t tick_ = 0;
};

}  // namespace

GameRuntime::GameRuntime(RuntimeOptions options) : options_(std::move(options)) {}

GameRuntime::~GameRuntime() {
    stopThreads();
    if (session_) {
        // Subsystems detach in the session's destructor and may reach for scripts,
        // so the session goes first.
        session_.reset();
    }
}

void GameRuntime::load() {
    if (!fs::is_directory(options_.gameRoot)) {
        throw std::runtime_error("game root is not a directory: " + options_.gameRoot.string());
    }
    auto manifest = GameManifest::load(options_.gameRoot);
    if (!manifest) {
        throw std::runtime_error("no readable game.conf in " + options_.gameRoot.string());
    }
    manifest_ = *manifest;
    if (options_.seedOverride) manifest_.worldSeed = *options_.seedOverride;
    if (options_.radiusOverride) manifest_.worldRadius = *options_.radiusOverride;

    ResourceLocator::instance().setGameRoot(options_.gameRoot);

    content_ = std::make_unique<ContentPackLoader>(manifest_.contentNamespace);
    content_->loadAll(options_.gameRoot);

    GameSessionConfig config;
    config.tickRate = manifest_.tickRate;
    config.gravity = manifest_.gravity;
    session_ = GameSession::createLocal(config);

    generateWorld();

    World& world = session_->world();
    scripts_ = std::make_unique<script::GameScriptEngine>(world);
    scripts_->setEntityManager(&session_->entities());
    scripts_->setFluidTickManager(&session_->fluidTicks());
    scripts_->setWorkClaims(&workClaims_);
    content_->bindScripts(*scripts_);
    session_->entities().setMobEventHooksProvider(scripts_->createHooksProvider());

    static std::unique_ptr<GameRootFinder> finder;
    finder = std::make_unique<GameRootFinder>(options_.gameRoot);
    scripts_->engine().setResourceFinder(finder.get());

    navigation_ = std::make_shared<NavigationSystem>(world, session_->entities());
    scripts_->setNavigation(navigation_.get());
    session_->addSubsystem(navigation_);
    session_->addSubsystem(std::make_shared<WorkClaimClock>(workClaims_));

    gameScript_ = std::make_shared<script::ScriptGameSubsystem>(*scripts_);
    gameScript_->setMessageSink(
        [this](std::vector<uint8_t> message) { uiOutbox_.push(std::move(message)); });
    session_->addSubsystem(gameScript_);

    scripts_->engine().registerFunction(
        "game_exit", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            requestExit(args.empty() ? 0 : static_cast<int>(args[0].asNumber()));
            return finescript::Value::nil();
        });

    // set_time_of_day fraction: 0 dawn, 0.25 noon, 0.5 dusk, 0.75 midnight
    scripts_->engine().registerFunction(
        "set_time_of_day", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            if (!args.empty() && args[0].isNumeric()) {
                session_->worldTime().setTime(static_cast<int64_t>(args[0].asNumber() * WorldTime::TICKS_PER_DAY));
            }
            return finescript::Value::nil();
        });

    session_->setGameCommandHandler(
        [this](const finescript::Value& command) { gameScript_->deliverCommand(command); });

    auto& ctx = gameScript_->context();
    ctx.set("WORLD_SEED", finescript::Value::integer(static_cast<int64_t>(manifest_.worldSeed)));
    ctx.set("WORLD_RADIUS", finescript::Value::integer(manifest_.worldRadius));
    ctx.set("SEA_LEVEL", finescript::Value::integer(manifest_.seaLevel));
    for (const auto& [name, value] : options_.scriptDefines) {
        ctx.set(name, finescript::Value::string(value));
    }

    if (!gameScript_->loadScript((options_.gameRoot / manifest_.mainScript).string())) {
        throw std::runtime_error("main script failed: " +
                                 (gameScript_->errors().empty() ? "" : gameScript_->errors().back()));
    }
    for (const auto& path : options_.extraScripts) {
        if (!gameScript_->loadScript(path.string())) {
            throw std::runtime_error("script failed: " + path.string());
        }
    }
    gameScript_->fire("start");
}

void GameRuntime::generateWorld() {
    auto started = std::chrono::steady_clock::now();
    World& world = session_->world();
    std::string ns = manifest_.contentNamespace;

    worldgen::BiomeLoader::loadDirectory((options_.gameRoot / "biomes").string(), ns);
    worldgen::FeatureLoader::loadDirectory((options_.gameRoot / "features").string(), ns);

    uint64_t seed = manifest_.worldSeed;
    worldgen::GenerationPipeline pipeline;
    pipeline.setWorldSeed(seed);
    pipeline.addPass(std::make_unique<worldgen::TerrainPass>(seed));
    pipeline.addPass(std::make_unique<worldgen::SurfacePass>());
    if (manifest_.caves) pipeline.addPass(std::make_unique<worldgen::CavePass>(seed));
    pipeline.addPass(std::make_unique<worldgen::OrePass>());
    pipeline.addPass(std::make_unique<worldgen::StructurePass>());
    pipeline.addPass(std::make_unique<worldgen::DecorationPass>());
    pipeline.addPass(std::make_unique<worldgen::FluidPass>(manifest_.seaLevel));

    worldgen::BiomeMap biomeMap(seed, worldgen::BiomeRegistry::global());

    int32_t radius = manifest_.worldRadius;
    for (int32_t cx = -radius; cx < radius; ++cx) {
        for (int32_t cz = -radius; cz < radius; ++cz) {
            auto& column = world.getOrCreateColumn(ColumnPos(cx, cz));
            pipeline.generateColumn(column, world, biomeMap);
        }
    }

    // Structures write through the world after each column's heightmap was built
    std::vector<ColumnPos> columns;
    world.forEachColumn([&columns](ColumnPos pos, ChunkColumn& column) {
        column.recalculateHeightmap();
        columns.push_back(pos);
    });

    LightEngine& light = session_->lightEngine();
    for (const ColumnPos& pos : columns) {
        light.initializeSkyLight(pos);
        light.initializeColumnLighting(pos);
        if (ChunkColumn* column = world.getColumn(pos)) column->markLightInitialized();
    }

    worldgenSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

void GameRuntime::runTicks(uint64_t count) {
    float dt = 1.0f / static_cast<float>(manifest_.tickRate);
    for (uint64_t i = 0; i < count && !exitRequested_; ++i) {
        session_->tick(dt);
    }
}

void GameRuntime::startThreads() {
    if (threadsRunning_) return;
    session_->lightEngine().start();
    session_->startGameThread();
    threadsRunning_ = true;
}

void GameRuntime::stopThreads() {
    if (!threadsRunning_) return;
    session_->stopGameThread();
    session_->lightEngine().stop();
    threadsRunning_ = false;
}

void GameRuntime::requestExit(int code) {
    exitRequested_ = true;
    exitCode_ = code;
}

}  // namespace finevox::game
