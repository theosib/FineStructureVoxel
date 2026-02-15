#include "finevox/core/game_session.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_event.hpp"

#include <thread>
#include <atomic>
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

    bool breakBlock(BlockPos pos) override {
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

    bool placeBlock(BlockPos pos, BlockTypeId type) override {
        // Sound eagerly
        auto soundSet = BlockRegistry::global().getType(type).soundSet();
        if (soundSet.isValid()) {
            soundQueue_.push(SoundEvent::blockPlace(soundSet, pos));
        }

        // Defer mutation to game thread
        commandQueue_.push(BlockEvent::blockPlaced(pos, type, world_.getBlock(pos)));
        return true;
    }

    bool useBlock(BlockPos pos, Face face) override {
        BlockTypeId blockType = world_.getBlock(pos);
        if (blockType.isAir()) return false;

        commandQueue_.push(BlockEvent::playerUse(pos, face));
        return true;
    }

    bool hitBlock(BlockPos pos, Face face) override {
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

private:
    World& world_;
    SoundEventQueue& soundQueue_;
    GameCommandQueue& commandQueue_;
};

// ============================================================================
// Command execution — maps BlockEvent commands to World/Scheduler operations
// ============================================================================

static void executeCommand(World& world, UpdateScheduler& scheduler,
                           EntityManager& entityManager, const BlockEvent& cmd) {
    switch (cmd.type) {
        case EventType::BlockBroken:
            world.breakBlock(cmd.pos);
            break;

        case EventType::BlockPlaced:
            world.placeBlock(cmd.pos, cmd.blockType);
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

        default:
            // Forward other event types to the scheduler
            scheduler.pushExternalEvent(cmd);
            break;
    }
}

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

    // Command queue (graphics thread → game thread)
    std::unique_ptr<GameCommandQueue> commandQueue;

    // Command interface
    std::unique_ptr<LocalGameActions> actions;

    // Game thread
    std::thread gameThread;
    std::atomic<bool> gameThreadRunning{false};

    // Config
    GameSessionConfig config;

    // Drain and execute all pending commands, then process scheduler events
    void drainAndExecuteCommands() {
        auto commands = commandQueue->drainAll();
        if (commands.empty()) return;

        for (const auto& cmd : commands) {
            executeCommand(*world, *scheduler, *entityManager, cmd);
        }
        scheduler->processEvents();
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

            // 2. Process ticks that are due
            auto now = Clock::now();
            int catchup = 0;
            while (now >= nextTickTime && catchup < 10) {
                worldTime->advance(tickDt);
                scheduler->advanceGameTick();
                scheduler->processEvents();
                entityManager->tick(tickDt);

                nextTickTime += tickInterval;
                ++catchup;
            }

            // If we fell behind too much, skip ahead
            if (catchup >= 10 && Clock::now() >= nextTickTime) {
                nextTickTime = Clock::now() + tickInterval;
            }

            // 3. Set next tick alarm
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

    // Stop lighting thread before destroying world
    if (impl_->lightEngine && impl_->lightEngine->isRunning()) {
        impl_->lightEngine->stop();
    }
}

std::unique_ptr<GameSession> GameSession::createLocal(const GameSessionConfig& config) {
    auto session = std::unique_ptr<GameSession>(new GameSession());
    auto& impl = *session->impl_;
    impl.config = config;

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

    // World time
    impl.worldTime = std::make_unique<WorldTime>();
    impl.worldTime->setTicksPerSecond(static_cast<float>(config.tickRate));

    // Command interface
    impl.actions = std::make_unique<LocalGameActions>(
        *impl.world, *impl.soundQueue, *impl.commandQueue);

    return session;
}

GameActions& GameSession::actions() { return *impl_->actions; }

World& GameSession::world() { return *impl_->world; }
const World& GameSession::world() const { return *impl_->world; }

UpdateScheduler& GameSession::scheduler() { return *impl_->scheduler; }
LightEngine& GameSession::lightEngine() { return *impl_->lightEngine; }
EntityManager& GameSession::entities() { return *impl_->entityManager; }
WorldTime& GameSession::worldTime() { return *impl_->worldTime; }

SoundEventQueue& GameSession::soundEvents() { return *impl_->soundQueue; }
GraphicsEventQueue& GameSession::graphicsEvents() { return *impl_->graphicsQueue; }

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
// Synchronous Tick (backwards compat, for tests)
// ============================================================================

void GameSession::tick(float dt) {
    assert(!isGameThreadRunning() && "tick() must not be called while game thread is running");

    // Drain pending commands from the queue (actions pushed from calling thread)
    impl_->drainAndExecuteCommands();

    // Advance world time
    impl_->worldTime->advance(dt);

    // Process scheduled ticks + external events
    impl_->scheduler->advanceGameTick();
    impl_->scheduler->processEvents();

    // Tick entities (publishes snapshots to graphics queue)
    impl_->entityManager->tick(dt);
}

}  // namespace finevox
