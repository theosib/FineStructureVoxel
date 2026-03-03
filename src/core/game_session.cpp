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

#include <thread>
#include <atomic>
#include <algorithm>
#include <cassert>
#include <chrono>

namespace finevox {

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

    bool breakBlock(BlockCoord pos) override {
        BlockTypeId oldType = world_.getBlock(pos);
        if (oldType.isAir()) return false;

        // Sound eagerly (instant audio feedback on calling thread)
        auto soundSet = BlockRegistry::global().getType(oldType).soundSet();
        if (soundSet.isValid()) {
            soundQueue_.push(SoundEvent::blockBreak(soundSet, pos));
        }

        // Defer mutation to game thread
        commandQueue_.push(BlockEvent::blockBroken(pos, oldType));
        return true;
    }

    bool placeBlock(BlockCoord pos, BlockTypeId type) override {
        // Sound eagerly
        auto soundSet = BlockRegistry::global().getType(type).soundSet();
        if (soundSet.isValid()) {
            soundQueue_.push(SoundEvent::blockPlace(soundSet, pos));
        }

        // Defer mutation to game thread
        commandQueue_.push(BlockEvent::blockPlaced(pos, type, world_.getBlock(pos)));
        return true;
    }

    bool placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level) override {
        // Sound eagerly
        const FluidType* ft = FluidRegistry::global().getType(type);
        if (ft && ft->soundSet.isValid()) {
            soundQueue_.push(SoundEvent::blockPlace(ft->soundSet, pos));
        }

        // Defer mutation to game thread
        commandQueue_.push(BlockEvent::fluidPlaced(pos, type, level));
        return true;
    }

    bool removeFluid(BlockCoord pos) override {
        FluidTypeId oldFluid = world_.getFluid(pos);
        if (oldFluid.isEmpty()) return false;

        // Sound eagerly
        const FluidType* ft = FluidRegistry::global().getType(oldFluid);
        if (ft && ft->soundSet.isValid()) {
            soundQueue_.push(SoundEvent::blockBreak(ft->soundSet, pos));
        }

        // Defer mutation to game thread
        commandQueue_.push(BlockEvent::fluidRemoved(pos, oldFluid));
        return true;
    }

    bool useBlock(BlockCoord pos, Face face) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        commandQueue_.push(BlockEvent::playerUse(pos, face));
        return true;
    }

    bool hitBlock(BlockCoord pos, Face face) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        commandQueue_.push(BlockEvent::playerHit(pos, face));
        return true;
    }

    void sendPlayerState(EntityId id, const EntityState& state) override {
        BlockEvent event;
        event.type = EventType::PlayerPosition;
        event.entityId = id;
        event.entityState = state;
        event.entityState.id = id;
        commandQueue_.push(std::move(event));
    }

    void setWorldTime(int64_t ticks) override {
        commandQueue_.push(BlockEvent::setWorldTime(ticks));
    }

private:
    World& world_;
    SoundEventQueue& soundQueue_;
    GameCommandQueue& commandQueue_;
};

// ============================================================================
// Command execution — maps BlockEvent commands to World/Scheduler operations
// ============================================================================

static void executeCommand(World& world, UpdateScheduler& scheduler,
                           EntityManager& entityManager, WorldTime& worldTime,
                           FluidTickManager* fluidTickManager,
                           const BlockEvent& cmd) {
    switch (cmd.type) {
        case EventType::BlockBroken:
            world.breakBlock(cmd.pos);
            if (fluidTickManager) fluidTickManager->notifyBlockChanged(cmd.pos);
            break;

        case EventType::BlockPlaced:
            world.placeBlock(cmd.pos, cmd.blockType);
            if (fluidTickManager) fluidTickManager->notifyBlockChanged(cmd.pos);
            break;

        case EventType::FluidPlaced:
            world.setFluid(cmd.pos, cmd.fluidType, cmd.fluidLevel);
            if (fluidTickManager) fluidTickManager->notifyFluidChanged(cmd.pos);
            break;

        case EventType::FluidRemoved:
            world.removeFluid(cmd.pos);
            if (fluidTickManager) fluidTickManager->notifyFluidChanged(cmd.pos);
            break;

        case EventType::PlayerUse:
        case EventType::PlayerHit:
            scheduler.pushExternalEvent(cmd);
            break;

        case EventType::PlayerPosition:
            entityManager.handlePlayerPosition(cmd);
            break;

        case EventType::PlayerLook:
            entityManager.handlePlayerLook(cmd);
            break;

        case EventType::PlayerJump:
            entityManager.handlePlayerJump(cmd);
            break;

        case EventType::PlayerStartSprint:
        case EventType::PlayerStopSprint:
            entityManager.handlePlayerSprint(cmd,
                cmd.type == EventType::PlayerStartSprint);
            break;

        case EventType::PlayerStartSneak:
        case EventType::PlayerStopSneak:
            entityManager.handlePlayerSneak(cmd,
                cmd.type == EventType::PlayerStartSneak);
            break;

        case EventType::SetWorldTime:
            worldTime.setTime(static_cast<int64_t>(cmd.entityState.inputSequence));
            break;

        default:
            // Forward other event types to the scheduler
            scheduler.pushExternalEvent(cmd);
            break;
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

    // Command queue (graphics thread → game thread)
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

}  // namespace finevox
