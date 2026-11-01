#include "finevox/game/game_runtime.hpp"

#include "finevox/game/replication_server.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/world.hpp"
#include "finevox/game/mcp_tools.hpp"
#include "finevox/worldgen/biome_loader.hpp"
#include "finevox/worldgen/biome_map.hpp"
#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>
#include <finescript/resource_finder.h>
#include <finescript/script_engine.h>

#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace finevox::game {

namespace fs = std::filesystem;

void registerBlueprintNatives(GameRuntime& runtime);
void registerConflictNatives(GameRuntime& runtime);

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

// Saves on game time, between ticks, so nothing is halfway through a change
class AutosaveClock : public GameSubsystem {
public:
    AutosaveClock(GameRuntime& runtime, uint64_t intervalTicks)
        : runtime_(runtime), interval_(intervalTicks) {}
    [[nodiscard]] std::string_view name() const override { return "AutosaveClock"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PostTick; }
    [[nodiscard]] int32_t priority() const override { return 1000; }
    void tick(float /*dt*/) override {
        if (++ticks_ < interval_) return;
        ticks_ = 0;
        std::string error;
        if (!runtime_.saveGame(error)) std::cerr << "[save] autosave failed: " << error << "\n";
    }

private:
    GameRuntime& runtime_;
    uint64_t interval_;
    uint64_t ticks_ = 0;
};

// Runs as a subsystem so work claims expire on game time
/// Times the tick for `status`. These two only exist when there's a server to tell.
class McpTickStart : public GameSubsystem {
public:
    explicit McpTickStart(McpServer& server) : server_(server) {}
    [[nodiscard]] std::string_view name() const override { return "McpTickStart"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PreTick; }
    [[nodiscard]] int32_t priority() const override { return std::numeric_limits<int32_t>::min(); }
    void tick(float) override { server_.noteTickStart(); }

private:
    McpServer& server_;
};

/// Last in the tick, so that a tool sees everything the tick did and nothing half done
class McpTickEnd : public GameSubsystem {
public:
    explicit McpTickEnd(McpServer& server) : server_(server) {}
    [[nodiscard]] std::string_view name() const override { return "McpTickEnd"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::LateTick; }
    [[nodiscard]] int32_t priority() const override { return std::numeric_limits<int32_t>::max(); }
    void tick(float) override {
        server_.noteTickEnd();
        server_.pump(McpSide::Game);
    }

private:
    McpServer& server_;
};

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
    for (const auto& [key, value] : options_.manifestOverrides) {
        if (!manifest_.applyOverride(key, value)) {
            std::cerr << "[game] a setting can't change " << key << "\n";
        }
    }

    ResourceLocator::instance().setGameRoot(options_.gameRoot);
    blueprints_.loadDirectory(options_.gameRoot / "blueprints");

    content_ = std::make_unique<ContentPackLoader>(manifest_.contentNamespace);
    content_->loadAll(options_.gameRoot);

    GameSessionConfig config;
    config.tickRate = manifest_.tickRate;
    config.gravity = manifest_.gravity;
    config.randomTicksPerChunk = manifest_.randomTicks;
    session_ = GameSession::createLocal(config);

    resumed_ = saveExists() && !options_.newGame;
    loadWorldContent();
    if (resumed_) {
        loadSavedWorld();
    } else {
        generateWorld();
    }

    World& world = session_->world();
    scripts_ = std::make_unique<script::GameScriptEngine>(world);
    scripts_->setEntityManager(&session_->entities());
    scripts_->setFluidTickManager(&session_->fluidTicks());
    scripts_->setWorkClaims(&workClaims_);
    content_->bindScripts(*scripts_);
    session_->entities().setMobEventHooksProvider(scripts_->createHooksProvider());

    resourceFinder_ = std::make_unique<GameRootFinder>(options_.gameRoot);
    scripts_->engine().setResourceFinder(resourceFinder_.get());

    navigation_ = std::make_shared<NavigationSystem>(world, session_->entities());
    navigation_->setMaxFall(manifest_.navMaxFall);
    scripts_->setNavigation(navigation_.get());
    session_->addSubsystem(navigation_);
    session_->addSubsystem(std::make_shared<WorkClaimClock>(workClaims_));

    int32_t half = manifest_.worldRadius * 16;
    VisionBounds bounds{-half, -half, half, half};
    vision_ = std::make_shared<FactionVision>(world, session_->entities(), bounds);
    vision_->addFaction(manifest_.viewerFaction);
    session_->addSubsystem(vision_);

    projectiles_ = std::make_shared<ProjectileSystem>(world, session_->entities());
    session_->addSubsystem(projectiles_);

    revealAll_ = !manifest_.fogOfWar;
    replica_ = &replicaFor(manifest_.viewerFaction);

    // Clients get entities from replication, one view at a time
    session_->entities().setSnapshotsEnabled(false);

    gameScript_ = std::make_shared<script::ScriptGameSubsystem>(*scripts_);
    gameScript_->setMessageSink([this](std::vector<uint8_t> message, script::ScriptGameSubsystem::Audience audience) {
        replication_->publish(std::move(message), audience.player, audience.faction);
    });
    session_->addSubsystem(gameScript_);

    replication_ = std::make_shared<ReplicationServer>(*this);
    session_->addSubsystem(replication_);

    registerNatives();

    session_->setGameCommandHandler([this](const finescript::Value& command, int64_t player) {
        gameScript_->deliverCommand(command, player);
    });

    auto& ctx = gameScript_->context();
    ctx.set("WORLD_SEED", finescript::Value::integer(static_cast<int64_t>(manifest_.worldSeed)));
    ctx.set("WORLD_RADIUS", finescript::Value::integer(manifest_.worldRadius));
    ctx.set("SEA_LEVEL", finescript::Value::integer(manifest_.seaLevel));

    auto& interner = scripts_->engine().interner();
    ctx.set("SETTINGS", options_.settings.empty() ? finescript::Value::map()
                                                  : finescript::cborDecode(options_.settings, interner));
    {
        std::vector<finescript::Value> players;
        std::lock_guard lock(seatMutex_);
        for (const PlayerSeed& seed : options_.players) {
            Seat seat;
            seat.id = seed.id;
            seat.name = seed.name;
            seat.local = seed.local;
            players.push_back(playerValue(seat));
            seats_.emplace(seat.id, std::move(seat));
        }
        ctx.set("PLAYERS", finescript::Value::array(std::move(players)));
    }
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
    if (resumed_) {
        loadSavedState();
        bindSeeds();
        gameScript_->fire("resume");
    } else {
        gameScript_->fire("start");
    }
    bindTheUnbound();

    if (manifest_.autosaveMinutes > 0.0f && !savePath().empty()) {
        session_->addSubsystem(std::make_shared<AutosaveClock>(
            *this, static_cast<uint64_t>(manifest_.autosaveMinutes * 60.0f * manifest_.tickRate)));
    }

    // Don't open on a blank map: show what the starting units can see
    for (const auto& [faction, replica] : replicas_) {
        vision_->refresh(faction);
        replica->syncAll();
    }

    if (options_.mcpPort || options_.mcpServer) startMcp();
}

WorldReplica& GameRuntime::replicaFor(uint16_t faction) {
    auto& slot = replicas_[faction];
    if (!slot) {
        vision_->addFaction(faction);
        slot = std::make_shared<WorldReplica>(session_->world(), *vision_, faction);
        slot->setRevealAll(revealAll_);
    }
    return *slot;
}

void GameRuntime::tickReplicas(float dt) {
    for (const auto& [faction, replica] : replicas_) replica->tick(dt);
}

void GameRuntime::setRevealAll(bool reveal) {
    revealAll_ = reveal;
    for (const auto& [faction, replica] : replicas_) replica->setRevealAll(reveal);
}

std::optional<Seat> GameRuntime::seat(int64_t player) const {
    std::lock_guard lock(seatMutex_);
    auto it = seats_.find(player);
    if (it == seats_.end()) return std::nullopt;
    return it->second;
}

std::vector<Seat> GameRuntime::seats() const {
    std::lock_guard lock(seatMutex_);
    std::vector<Seat> out;
    for (const auto& [id, seat] : seats_) out.push_back(seat);
    return out;
}

void GameRuntime::bindPlayer(int64_t player, uint16_t faction) {
    Seat updated;
    {
        std::lock_guard lock(seatMutex_);
        auto it = seats_.find(player);
        if (it == seats_.end()) return;
        it->second.faction = faction;
        updated = it->second;
    }

    replicaFor(faction);

    // A body the first time, and the same body if they change sides
    EntityManager& entities = session_->entities();
    if (updated.body == INVALID_ENTITY_ID) {
        Vec3 spot(0.0f, float(manifest_.seaLevel + 16), 0.0f);
        if (updated.local && resumed_) {
            glm::dvec3 saved = clientSaveCopy().steward;
            spot = Vec3(float(saved.x), float(saved.y), float(saved.z));
        }
        updated.body = entities.spawnPlayer(spot, manifest_.playerEntity);
        updated.spawn = glm::dvec3(spot);
        if (updated.local) entities.setLocalPlayerId(updated.body);
    }
    if (MobEntity* mob = entities.getMob(updated.body)) mob->setFaction(faction);

    std::lock_guard lock(seatMutex_);
    seats_[player] = updated;
}

finescript::Value GameRuntime::playerValue(const Seat& seat) const {
    using finescript::Value;
    auto& interner = StringInterner::global();
    Value out = Value::map();
    auto& map = out.asMap();
    map.set(interner.intern("id"), Value::integer(seat.id));
    map.set(interner.intern("name"), Value::string(seat.name));
    map.set(interner.intern("local"), Value::boolean(seat.local));
    return out;
}

void GameRuntime::playerJoined(int64_t player, const std::string& name, bool local) {
    Seat seat;
    seat.id = player;
    seat.name = name;
    seat.local = local;
    {
        std::lock_guard lock(seatMutex_);
        seats_.emplace(player, seat);
    }
    gameScript_->context().set("player", playerValue(seat));
    gameScript_->fire("player_join");
    bindTheUnbound();
}

void GameRuntime::bindTheUnbound() {
    // A game that says nothing about players has one point of view, and everyone gets it
    for (const Seat& seat : seats()) {
        if (seat.faction == 0) bindPlayer(seat.id, manifest_.viewerFaction);
    }
}

void GameRuntime::postPlayerLeft(int64_t player) {
    replication_->post([this, player] {
        auto left = seat(player);
        if (!left) return;
        gameScript_->context().set("player", playerValue(*left));
        gameScript_->fire("player_leave");
    });
}

void GameRuntime::bindSeeds() {
    // Each returning player to the faction they had: the host by being the host, others by name
    for (const PlayerSeed& seed : options_.players) {
        uint16_t faction = 0;
        for (const SavedSeat& saved : savedSeats_) {
            if ((seed.local && saved.local) || (!seed.local && saved.name == seed.name)) faction = saved.faction;
        }
        if (faction == 0 && seed.local) faction = manifest_.viewerFaction;
        if (faction != 0) bindPlayer(seed.id, faction);
    }
}

finescript::Value GameRuntime::clientState() const {
    using finescript::Value;
    ClientSave save = clientSaveCopy();
    auto vector3 = [](const glm::dvec3& v) {
        return Value::array(std::vector<Value>{Value::number(v.x), Value::number(v.y), Value::number(v.z)});
    };
    auto& interner = StringInterner::global();
    Value out = Value::map();
    auto& map = out.asMap();
    map.set(interner.intern("pivot"), vector3(save.pivot));
    map.set(interner.intern("steward"), vector3(save.steward));
    map.set(interner.intern("camera"), vector3(glm::dvec3(save.yaw, save.pitch, save.distance)));
    return out;
}

void GameRuntime::setClientState(const finescript::Value& state) {
    using finescript::Value;
    if (!state.isMap()) return;
    auto& interner = StringInterner::global();
    auto& map = const_cast<Value&>(state).asMap();
    auto vector3 = [&](const char* name, glm::dvec3& out) {
        Value value = map.get(interner.intern(name));
        if (value.isArray() && value.asArray().size() == 3) {
            const auto& v = value.asArray();
            out = glm::dvec3(v[0].asNumber(), v[1].asNumber(), v[2].asNumber());
        }
    };
    ClientSave save = clientSaveCopy();
    glm::dvec3 camera(save.yaw, save.pitch, save.distance);
    vector3("pivot", save.pivot);
    vector3("steward", save.steward);
    vector3("camera", camera);
    save.yaw = float(camera.x);
    save.pitch = float(camera.y);
    save.distance = float(camera.z);

    std::lock_guard lock(clientSaveMutex_);
    clientSave_ = save;
}

ClientSave GameRuntime::clientSaveCopy() const {
    std::lock_guard lock(clientSaveMutex_);
    return clientSave_;
}

void GameRuntime::settleLight() {
    LightEngine& light = session_->lightEngine();
    light.start();

    // Nothing lit what `on :start` built, and a client's first frame shouldn't show that
    constexpr auto LIMIT = std::chrono::seconds(5);
    auto deadline = std::chrono::steady_clock::now() + LIMIT;
    while (!light.queue().empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const auto& [faction, replica] : replicas_) replica->refreshLight();
}

void GameRuntime::startMcp() {
    mcp_ = options_.mcpServer;
    if (!mcp_) {
        ownedMcp_ = std::make_unique<McpServer>(*options_.mcpPort, options_.mcpPlayerOnly);
        mcp_ = ownedMcp_.get();
        mcp_->captureOutput();
    }
    addServerMcpTools(*mcp_, *this);
    session_->addSubsystem(std::make_shared<McpTickStart>(*mcp_));
    session_->addSubsystem(std::make_shared<McpTickEnd>(*mcp_));
}

void GameRuntime::registerNatives() {
    auto& engine = scripts_->engine();

    // game_state_store value: in `on :save`, hand over what to keep
    engine.registerFunction(
        "game_state_store", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            savedScriptState_ = args.empty() ? finescript::Value::nil() : args[0];
            return finescript::Value::nil();
        });

    // game_state_load -> value: in `on :resume`, what `on :save` handed over
    engine.registerFunction(
        "game_state_load", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&) {
            return savedScriptState_;
        });

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

    // hold_time_of_day fraction: stop the clock here, for a game with no use for nights
    scripts_->engine().registerFunction(
        "hold_time_of_day", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            if (!args.empty() && args[0].isNumeric()) {
                session_->worldTime().setTime(static_cast<int64_t>(args[0].asNumber() * WorldTime::TICKS_PER_DAY));
                session_->worldTime().setFrozen(true);
            }
            return finescript::Value::nil();
        });

    // light_at x y z -> {=sky =block}, each 0 to 15
    scripts_->engine().registerFunction(
        "light_at", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            if (args.size() < 3) return finescript::Value::nil();
            BlockCoord pos(static_cast<int32_t>(args[0].asNumber()), static_cast<int32_t>(args[1].asNumber()),
                           static_cast<int32_t>(args[2].asNumber()));
            auto& interner = StringInterner::global();
            auto result = finescript::Value::map();
            auto& map = result.asMap();
            map.set(interner.intern("sky"), finescript::Value::integer(session_->lightEngine().getSkyLight(pos)));
            map.set(interner.intern("block"), finescript::Value::integer(session_->lightEngine().getBlockLight(pos)));
            return result;
        });

    // player_bind id faction: this player sees and acts as this faction
    engine.registerFunction(
        "player_bind", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            if (args.size() >= 2) bindPlayer(static_cast<int64_t>(args[0].asNumber()), static_cast<uint16_t>(args[1].asNumber()));
            return finescript::Value::nil();
        });

    // player_faction id -> faction, or nil if they have none
    engine.registerFunction(
        "player_faction", [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args) {
            if (args.empty() || !args[0].isNumeric()) return finescript::Value::nil();
            auto found = seat(static_cast<int64_t>(args[0].asNumber()));
            if (!found || found->faction == 0) return finescript::Value::nil();
            return finescript::Value::integer(found->faction);
        });

    registerBlueprintNatives(*this);
    registerConflictNatives(*this);
}

void GameRuntime::loadWorldContent() {
    std::string ns = manifest_.contentNamespace;
    worldgen::BiomeLoader::loadDirectory((options_.gameRoot / "biomes").string(), ns);
    worldgen::FeatureLoader::loadDirectory((options_.gameRoot / "features").string(), ns);
}

void GameRuntime::generateWorld() {
    auto started = std::chrono::steady_clock::now();
    World& world = session_->world();

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

    lightWorld();
    worldgenSeconds_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
}

void GameRuntime::lightWorld() {
    World& world = session_->world();

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

    // Last, so that the game is running by the time anyone can call a tool
    if (ownedMcp_) {
        std::string error;
        if (mcp_->start(error)) {
            std::cerr << "[mcp] listening on http://127.0.0.1:" << mcp_->port() << "/mcp\n";
        } else {
            std::cerr << "[mcp] " << error << ": running without it\n";
        }
    }
}

void GameRuntime::stopThreads() {
    // The light thread can be running without the rest: see settleLight
    if (session_) session_->lightEngine().stop();
    if (!threadsRunning_) return;
    if (ownedMcp_) ownedMcp_->stop();
    session_->stopGameThread();
    threadsRunning_ = false;
}

void GameRuntime::setSiteGhosts(uint16_t faction, std::vector<worldgen::SiteGhost> ghosts) {
    siteGhosts_[faction] = std::make_shared<const std::vector<worldgen::SiteGhost>>(std::move(ghosts));
}

std::shared_ptr<const std::vector<worldgen::SiteGhost>> GameRuntime::siteGhosts(uint16_t faction) const {
    auto it = siteGhosts_.find(faction);
    return it == siteGhosts_.end() ? nullptr : it->second;
}

void GameRuntime::requestExit(int code) {
    exitRequested_ = true;
    exitCode_ = code;
}

}  // namespace finevox::game
