#include "finevox/core/game_session.hpp"
#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/block_events.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_event.hpp"
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/script/event_value.hpp"

#include <finescript/value.h>
#include <finescript/map_data.h>

#include <thread>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>

namespace finevox {

using namespace finevox::script;

// ============================================================================
// GameActions convenience methods — build Values and call sendAction()
// ============================================================================

bool GameActions::breakBlock(BlockCoord pos) {
    auto oldType = BlockTypeId::fromName("finevox:air"); // actual type checked server-side
    sendAction(makeBlockBrokenValue(pos, oldType));
    return true;
}

bool GameActions::placeBlock(BlockCoord pos, BlockTypeId type) {
    auto oldType = BlockTypeId::fromName("finevox:air");
    sendAction(makeBlockPlacedValue(pos, type, oldType));
    return true;
}

bool GameActions::useBlock(BlockCoord pos, Face face) {
    sendAction(makePlayerUseValue(pos, face));
    return true;
}

bool GameActions::hitBlock(BlockCoord pos, Face face) {
    sendAction(makePlayerHitValue(pos, face));
    return true;
}

bool GameActions::placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level) {
    sendAction(makeFluidPlacedValue(pos, type, level));
    return true;
}

bool GameActions::removeFluid(BlockCoord pos) {
    auto emptyFluid = FluidTypeId{}; // empty
    sendAction(makeFluidRemovedValue(pos, emptyFluid));
    return true;
}

void GameActions::sendPlayerState(EntityId id, const EntityState& state) {
    sendAction(makePlayerPositionValue(id, state.position, state.velocity,
                                        state.onGround, state.inputSequence));
}

void GameActions::setWorldTime(int64_t ticks) {
    sendAction(makeSetWorldTimeValue(ticks));
}

bool GameActions::craftItem(BlockCoord stationPos, RecipeId recipe) {
    sendAction(makeCraftItemValue(stationPos, recipe));
    return true;
}

// ============================================================================
// LocalGameActions — single-player implementation
//
// Sound events are pushed eagerly on the calling thread (instant audio).
// Block mutations and player state are deferred to the game thread via
// the command queue.
// ============================================================================

class LocalGameActions : public GameActions {
public:
    LocalGameActions(World& world, SoundEventQueue& soundQueue, GameCommandQueue& commandQueue)
        : world_(world), soundQueue_(soundQueue), commandQueue_(commandQueue) {}

    void sendAction(finescript::Value action) override {
        // Push the raw Value into the command queue for the game thread
        commandQueue_.push(std::move(action));
    }

    // Override typed methods to add eager sound feedback before queuing
    bool breakBlock(BlockCoord pos) override {
        BlockTypeId oldType = world_.getBlock(pos);
        if (oldType.isAir()) return false;

        // Sound eagerly (instant audio feedback on calling thread)
        auto soundSet = BlockRegistry::global().getType(oldType).soundSet();
        if (soundSet.isValid()) {
            soundQueue_.push(makeSoundEventValue(soundSet, "break", "effects", pos));
        }

        // Defer mutation to game thread
        sendAction(makeBlockBrokenValue(pos, oldType));
        return true;
    }

    bool placeBlock(BlockCoord pos, BlockTypeId type) override {
        // Sound eagerly
        auto soundSet = BlockRegistry::global().getType(type).soundSet();
        if (soundSet.isValid()) {
            soundQueue_.push(makeSoundEventValue(soundSet, "place", "effects", pos));
        }

        // Defer mutation to game thread
        sendAction(makeBlockPlacedValue(pos, type, world_.getBlock(pos)));
        return true;
    }

    bool placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level) override {
        // Sound eagerly
        const FluidType* ft = FluidRegistry::global().getType(type);
        if (ft && ft->soundSet.isValid()) {
            soundQueue_.push(makeSoundEventValue(ft->soundSet, "place", "effects", pos));
        }

        // Defer mutation to game thread
        sendAction(makeFluidPlacedValue(pos, type, level));
        return true;
    }

    bool removeFluid(BlockCoord pos) override {
        FluidTypeId oldFluid = world_.getFluid(pos);
        if (oldFluid.isEmpty()) return false;

        // Sound eagerly
        const FluidType* ft = FluidRegistry::global().getType(oldFluid);
        if (ft && ft->soundSet.isValid()) {
            soundQueue_.push(makeSoundEventValue(ft->soundSet, "break", "effects", pos));
        }

        // Defer mutation to game thread
        sendAction(makeFluidRemovedValue(pos, oldFluid));
        return true;
    }

    bool useBlock(BlockCoord pos, Face face) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        sendAction(makePlayerUseValue(pos, face));
        return true;
    }

    bool hitBlock(BlockCoord pos, Face face) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        sendAction(makePlayerHitValue(pos, face));
        return true;
    }

private:
    World& world_;
    SoundEventQueue& soundQueue_;
    GameCommandQueue& commandQueue_;
};

// ============================================================================
// Command execution — reads finescript Value commands, dispatches to subsystems
// ============================================================================

static void executeCommand(World& world, UpdateScheduler& scheduler,
                           EntityManager& entityManager, WorldTime& worldTime,
                           FluidTickManager* fluidTickManager,
                           const finescript::Value& cmd) {
    auto typeStr = readEventType(cmd);

    if (typeStr == EVT_BLOCK_BROKEN) {
        auto pos = readBlockCoord(cmd);
        world.breakBlock(pos);
        if (fluidTickManager) fluidTickManager->notifyBlockChanged(pos);
    }
    else if (typeStr == EVT_BLOCK_PLACED) {
        auto pos = readBlockCoord(cmd);
        const auto& s = EventSymbols::instance();
        auto blockType = readBlockTypeId(cmd, s.block_type);
        world.placeBlock(pos, blockType);
        if (fluidTickManager) fluidTickManager->notifyBlockChanged(pos);
    }
    else if (typeStr == EVT_FLUID_PLACED) {
        auto pos = readBlockCoord(cmd);
        auto fluidType = readFluidTypeId(cmd);
        auto level = static_cast<uint8_t>(readInt(cmd, EventSymbols::instance().fluid_level, 15));
        world.setFluid(pos, fluidType, level);
        if (fluidTickManager) fluidTickManager->notifyFluidChanged(pos);
    }
    else if (typeStr == EVT_FLUID_REMOVED) {
        auto pos = readBlockCoord(cmd);
        world.removeFluid(pos);
        if (fluidTickManager) fluidTickManager->notifyFluidChanged(pos);
    }
    else if (typeStr == EVT_PLAYER_USE) {
        auto pos = readBlockCoord(cmd);
        auto face = readFace(cmd);
        scheduler.pushExternalEvent(BlockEvent::playerUse(pos, face));
    }
    else if (typeStr == EVT_PLAYER_HIT) {
        auto pos = readBlockCoord(cmd);
        auto face = readFace(cmd);
        scheduler.pushExternalEvent(BlockEvent::playerHit(pos, face));
    }
    else if (typeStr == EVT_PLAYER_POSITION) {
        auto id = readEntityId(cmd);
        const auto& s = EventSymbols::instance();
        auto pos = readDVec3(cmd, s.pos_x, s.pos_y, s.pos_z);
        auto vel = readDVec3(cmd, s.vel_x, s.vel_y, s.vel_z);
        bool onGround = readBool(cmd, s.on_ground);
        uint64_t seq = static_cast<uint64_t>(readInt(cmd, s.input_sequence));

        BlockEvent event = BlockEvent::playerPosition(id, pos, vel, onGround, seq);
        entityManager.handlePlayerPosition(event);
    }
    else if (typeStr == EVT_PLAYER_LOOK) {
        auto id = readEntityId(cmd);
        const auto& s = EventSymbols::instance();
        float yaw = readFloat(cmd, s.yaw);
        float pitch = readFloat(cmd, s.pitch);

        BlockEvent event = BlockEvent::playerLook(id, yaw, pitch);
        entityManager.handlePlayerLook(event);
    }
    else if (typeStr == EVT_PLAYER_JUMP) {
        auto id = readEntityId(cmd);
        BlockEvent event = BlockEvent::playerJump(id);
        entityManager.handlePlayerJump(event);
    }
    else if (typeStr == EVT_PLAYER_SPRINT) {
        auto id = readEntityId(cmd);
        bool starting = readBool(cmd, EventSymbols::instance().starting);
        BlockEvent event = BlockEvent::playerSprint(id, starting);
        entityManager.handlePlayerSprint(event, starting);
    }
    else if (typeStr == EVT_PLAYER_SNEAK) {
        auto id = readEntityId(cmd);
        bool starting = readBool(cmd, EventSymbols::instance().starting);
        BlockEvent event = BlockEvent::playerSneak(id, starting);
        entityManager.handlePlayerSneak(event, starting);
    }
    else if (typeStr == EVT_SET_WORLD_TIME) {
        int64_t ticks = readInt(cmd, EventSymbols::instance().ticks);
        worldTime.setTime(ticks);
    }
    else if (typeStr == EVT_CRAFT_ITEM) {
        // Stub: recipe validation and station-block inventory wiring
        // will be implemented in Phase 22-3 (station block handlers).
    }
}

// ============================================================================
// Built-in subsystem wrappers
// ============================================================================

class WorldTimeSubsystem : public GameSubsystem {
public:
    explicit WorldTimeSubsystem(WorldTime& wt) : worldTime_(wt) {}
    std::string_view name() const override { return "WorldTime"; }
    TickPhase phase() const override { return TickPhase::PreTick; }
    int32_t priority() const override { return 0; }
    void tick(float dt) override { worldTime_.advance(dt); }
private:
    WorldTime& worldTime_;
};

class BlockEventSubsystem : public GameSubsystem {
public:
    explicit BlockEventSubsystem(UpdateScheduler& sched) : scheduler_(sched) {}
    std::string_view name() const override { return "BlockEvents"; }
    TickPhase phase() const override { return TickPhase::Tick; }
    int32_t priority() const override { return 0; }
    void tick(float /*dt*/) override {
        scheduler_.advanceGameTick();
        scheduler_.processEvents();
    }
private:
    UpdateScheduler& scheduler_;
};

class FluidSubsystem : public GameSubsystem {
public:
    explicit FluidSubsystem(FluidTickManager& ftm) : fluidTicks_(ftm) {}
    std::string_view name() const override { return "FluidSim"; }
    TickPhase phase() const override { return TickPhase::Tick; }
    int32_t priority() const override { return 100; }
    void tick(float /*dt*/) override { fluidTicks_.tick(); }
private:
    FluidTickManager& fluidTicks_;
};

class EntitySubsystem : public GameSubsystem {
public:
    explicit EntitySubsystem(EntityManager& em) : entities_(em) {}
    std::string_view name() const override { return "Entities"; }
    TickPhase phase() const override { return TickPhase::PostTick; }
    int32_t priority() const override { return 0; }
    void tick(float dt) override { entities_.tick(dt); }
private:
    EntityManager& entities_;
};

// ============================================================================
// GameSession::Impl
// ============================================================================

struct GameSession::Impl {
    // Owned subsystems (order matters for destruction)
    std::unique_ptr<World> world;
    std::unique_ptr<UpdateScheduler> scheduler;
    std::unique_ptr<LightEngine> lightEngine;
    std::unique_ptr<SoundEventQueue> soundQueue;
    std::unique_ptr<GraphicsEventQueue> graphicsQueue;
    std::unique_ptr<EntityManager> entityManager;
    std::unique_ptr<WorldTime> worldTime;
    std::unique_ptr<FluidTickManager> fluidTickManager;

    // EventBus
    std::shared_ptr<EventBus> eventBus;

    // Registered subsystems (sorted by phase/priority)
    std::vector<std::shared_ptr<GameSubsystem>> subsystems;

    // Command queue (graphics thread -> game thread)
    std::unique_ptr<GameCommandQueue> commandQueue;

    // Command interface
    std::unique_ptr<LocalGameActions> actions;

    // Game thread
    std::thread gameThread;
    std::atomic<bool> gameThreadRunning{false};

    // Config
    GameSessionConfig config;

    // Sort subsystems by (phase, priority)
    void sortSubsystems() {
        std::sort(subsystems.begin(), subsystems.end(),
            [](const std::shared_ptr<GameSubsystem>& a,
               const std::shared_ptr<GameSubsystem>& b) {
                if (a->phase() != b->phase())
                    return static_cast<uint8_t>(a->phase()) < static_cast<uint8_t>(b->phase());
                return a->priority() < b->priority();
            });
    }

    // Drain and execute all pending commands, then process scheduler events
    void drainAndExecuteCommands() {
        auto commands = commandQueue->drainAll();
        if (commands.empty()) return;

        for (const auto& cmd : commands) {
            executeCommand(*world, *scheduler, *entityManager, *worldTime,
                           fluidTickManager.get(), cmd);
        }
        scheduler->processEvents();
    }

    // Tick all subsystems in order
    void tickSubsystems(float dt) {
        for (auto& sub : subsystems) {
            sub->tick(dt);
        }
    }

    // Game thread main loop
    void gameThreadLoop() {
        using Clock = std::chrono::steady_clock;

        const float tickDt = 1.0f / static_cast<float>(config.tickRate);
        const auto tickInterval = std::chrono::microseconds(
            static_cast<int64_t>(1000000.0 / config.tickRate));

        auto nextTickTime = Clock::now() + tickInterval;
        commandQueue->setAlarm(nextTickTime);

        while (commandQueue->waitForWork()) {
            // 1. Drain and execute all pending commands
            drainAndExecuteCommands();

            // 2. Dispatch cross-thread events
            if (eventBus) eventBus->dispatchQueued();

            // 3. Process ticks that are due
            auto now = Clock::now();
            int catchup = 0;
            while (now >= nextTickTime && catchup < 10) {
                tickSubsystems(tickDt);

                nextTickTime += tickInterval;
                ++catchup;
            }

            // If we fell behind too much, skip ahead
            if (catchup >= 10 && Clock::now() >= nextTickTime) {
                nextTickTime = Clock::now() + tickInterval;
            }

            // 4. Set next tick alarm
            commandQueue->setAlarm(nextTickTime);
        }
    }
};

// ============================================================================
// GameSession
// ============================================================================

GameSession::GameSession() : impl_(std::make_unique<Impl>()) {}

GameSession::~GameSession() {
    // Stop game thread first
    stopGameThread();

    // Detach subsystems in reverse order
    for (auto it = impl_->subsystems.rbegin(); it != impl_->subsystems.rend(); ++it) {
        (*it)->onDetach();
    }

    // Stop lighting thread before destroying world
    if (impl_->lightEngine && impl_->lightEngine->isRunning()) {
        impl_->lightEngine->stop();
    }
}

std::unique_ptr<GameSession> GameSession::createLocal(const GameSessionConfig& config) {
    auto session = std::unique_ptr<GameSession>(new GameSession());
    auto& impl = *session->impl_;
    impl.config = config;

    // Create EventBus
    impl.eventBus = std::make_shared<EventBus>();

    // Create core systems
    impl.world = std::make_unique<World>();
    impl.scheduler = std::make_unique<UpdateScheduler>(*impl.world);

    // Lighting
    impl.lightEngine = std::make_unique<LightEngine>(*impl.world);
    impl.lightEngine->setMaxPropagationDistance(10000);

    // Wire lighting to world
    impl.world->setLightEngine(impl.lightEngine.get());
    impl.world->setUpdateScheduler(impl.scheduler.get());

    // Event queues
    impl.soundQueue = std::make_unique<SoundEventQueue>();
    impl.graphicsQueue = std::make_unique<GraphicsEventQueue>();
    impl.commandQueue = std::make_unique<GameCommandQueue>();

    // Entity system
    impl.entityManager = std::make_unique<EntityManager>(*impl.world, *impl.graphicsQueue);
    impl.entityManager->setSoundQueue(impl.soundQueue.get());

    // World time
    impl.worldTime = std::make_unique<WorldTime>();
    impl.worldTime->setTicksPerSecond(static_cast<float>(config.tickRate));

    // Fluid simulation
    if (config.enableFluidSimulation) {
        impl.fluidTickManager = std::make_unique<FluidTickManager>(*impl.world);
        impl.fluidTickManager->simulator().setLightEngine(impl.lightEngine.get());
    }

    // Command interface
    impl.actions = std::make_unique<LocalGameActions>(
        *impl.world, *impl.soundQueue, *impl.commandQueue);

    // Register built-in subsystems
    session->addSubsystem(std::make_shared<WorldTimeSubsystem>(*impl.worldTime));
    session->addSubsystem(std::make_shared<BlockEventSubsystem>(*impl.scheduler));
    session->addSubsystem(std::make_shared<EntitySubsystem>(*impl.entityManager));

    if (impl.fluidTickManager) {
        session->addSubsystem(std::make_shared<FluidSubsystem>(*impl.fluidTickManager));
    }

    return session;
}

GameActions& GameSession::actions() { return *impl_->actions; }

World& GameSession::world() { return *impl_->world; }
const World& GameSession::world() const { return *impl_->world; }

UpdateScheduler& GameSession::scheduler() { return *impl_->scheduler; }
LightEngine& GameSession::lightEngine() { return *impl_->lightEngine; }
EntityManager& GameSession::entities() { return *impl_->entityManager; }
WorldTime& GameSession::worldTime() { return *impl_->worldTime; }
FluidTickManager& GameSession::fluidTicks() { return *impl_->fluidTickManager; }

SoundEventQueue& GameSession::soundEvents() { return *impl_->soundQueue; }
GraphicsEventQueue& GameSession::graphicsEvents() { return *impl_->graphicsQueue; }

EventBus& GameSession::eventBus() { return *impl_->eventBus; }

// ============================================================================
// Subsystem Management
// ============================================================================

void GameSession::addSubsystem(std::shared_ptr<GameSubsystem> subsystem) {
    subsystem->onAttach(*this, *impl_->eventBus);
    impl_->subsystems.push_back(std::move(subsystem));
    impl_->sortSubsystems();
}

void GameSession::removeSubsystem(const std::shared_ptr<GameSubsystem>& subsystem) {
    auto it = std::find(impl_->subsystems.begin(), impl_->subsystems.end(), subsystem);
    if (it != impl_->subsystems.end()) {
        (*it)->onDetach();
        impl_->subsystems.erase(it);
    }
}

const std::vector<std::shared_ptr<GameSubsystem>>& GameSession::subsystems() const {
    return impl_->subsystems;
}

// ============================================================================
// Game Thread Lifecycle
// ============================================================================

void GameSession::startGameThread() {
    if (impl_->gameThreadRunning.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    impl_->commandQueue->resetShutdown();
    impl_->gameThreadRunning.store(true, std::memory_order_release);
    impl_->gameThread = std::thread([this]() {
        impl_->gameThreadLoop();
        impl_->gameThreadRunning.store(false, std::memory_order_release);
    });
}

void GameSession::stopGameThread() {
    if (!impl_->gameThreadRunning.load(std::memory_order_acquire)) {
        return;  // Not running
    }

    impl_->commandQueue->shutdown();
    if (impl_->gameThread.joinable()) {
        impl_->gameThread.join();
    }
}

bool GameSession::isGameThreadRunning() const {
    return impl_->gameThreadRunning.load(std::memory_order_acquire);
}

// ============================================================================
// Synchronous Tick (for tests)
// ============================================================================

void GameSession::tick(float dt) {
    assert(!isGameThreadRunning() && "tick() must not be called while game thread is running");

    // Drain pending commands from the queue (actions pushed from calling thread)
    impl_->drainAndExecuteCommands();

    // Dispatch cross-thread events
    if (impl_->eventBus) impl_->eventBus->dispatchQueued();

    // Tick all subsystems in order
    impl_->tickSubsystems(dt);
}

// ============================================================================
// GameSessionConfig serialization
// ============================================================================

GameSessionConfig GameSessionConfig::fromDataContainer(const DataContainer& dc) {
    GameSessionConfig c;
    c.enableLighting = dc.get<bool>("enable_lighting", true);
    c.enableSound = dc.get<bool>("enable_sound", true);
    c.enableFluidSimulation = dc.get<bool>("enable_fluid_simulation", true);
    c.gravity = dc.get<float>("gravity", -14.0f);
    c.tickRate = dc.get<uint32_t>("tick_rate", 30);
    c.randomTicksPerChunk = dc.get<uint32_t>("random_ticks_per_chunk", 4);
    return c;
}

DataContainer GameSessionConfig::toDataContainer() const {
    DataContainer dc;
    dc.set("enable_lighting", enableLighting);
    dc.set("enable_sound", enableSound);
    dc.set("enable_fluid_simulation", enableFluidSimulation);
    dc.set("gravity", gravity);
    dc.set("tick_rate", tickRate);
    dc.set("random_ticks_per_chunk", randomTicksPerChunk);
    return dc;
}

}  // namespace finevox
