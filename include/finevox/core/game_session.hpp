#pragma once

#include "finevox/core/game_actions.hpp"
#include <finescript/value.h>
#include <memory>
#include <vector>

namespace finevox {

// Forward declarations
class World;
class UpdateScheduler;
class LightEngine;
class EntityManager;
class WorldTime;
class PhysicsSystem;
class FluidTickManager;
class GameSubsystem;
class EventBus;
template<typename T> class Queue;
using SoundEventQueue = Queue<finescript::Value>;
struct GraphicsMessage;
using GraphicsEventQueue = Queue<GraphicsMessage>;
using GameCommandQueue = Queue<finescript::Value>;

// Forward declaration
class DataContainer;

/// Configuration for creating a GameSession.
/// Fields can be set directly or loaded from a DataContainer / config file.
struct GameSessionConfig {
    bool enableLighting = true;
    bool enableSound = true;
    bool enableFluidSimulation = true;
    float gravity = -14.0f;
    uint32_t tickRate = 30;           // TPS
    uint32_t randomTicksPerChunk = 4;

    /// Create a config with all defaults
    static GameSessionConfig defaults() { return {}; }

    /// Populate from a DataContainer (missing keys use defaults)
    static GameSessionConfig fromDataContainer(const DataContainer& dc);

    /// Serialize to a DataContainer
    [[nodiscard]] DataContainer toDataContainer() const;
};

/// Owns all game state and provides the session boundary.
/// Gameplay code interacts ONLY through:
///   - actions()       -> send commands (mutations)
///   - world()         -> read state (rendering, physics, raycasting)
///   - soundEvents()   -> receive sound events
///   - graphicsEvents()-> receive entity/visual events
///   - tick()          -> advance game time (synchronous, for tests)
///   - startGameThread() / stopGameThread() -> threaded operation
class GameSession {
public:
    /// Create a local (single-player) session
    static std::unique_ptr<GameSession> createLocal(const GameSessionConfig& config = {});

    ~GameSession();

    // Non-copyable, non-movable
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // === Command Interface (mutations go IN) ===
    GameActions& actions();

    // === State Access (reads, for rendering/physics) ===
    World& world();
    const World& world() const;

    // === Subsystem Access ===
    UpdateScheduler& scheduler();
    LightEngine& lightEngine();
    EntityManager& entities();
    WorldTime& worldTime();
    FluidTickManager& fluidTicks();

    // === Event Channels (events come OUT) ===
    SoundEventQueue& soundEvents();
    GraphicsEventQueue& graphicsEvents();

    // === EventBus ===
    EventBus& eventBus();

    // === Subsystem Management ===
    /// Add a game subsystem. Sorted by (phase, priority) at insertion time.
    /// Must be called before startGameThread().
    void addSubsystem(std::shared_ptr<GameSubsystem> subsystem);

    /// Remove a game subsystem.
    void removeSubsystem(const std::shared_ptr<GameSubsystem>& subsystem);

    /// Get all registered subsystems (in tick order)
    [[nodiscard]] const std::vector<std::shared_ptr<GameSubsystem>>& subsystems() const;

    // === Game Thread Lifecycle ===
    /// Start the game thread (processes commands + ticks at configured rate)
    void startGameThread();
    /// Stop the game thread (blocks until thread exits)
    void stopGameThread();
    /// Check if game thread is running
    [[nodiscard]] bool isGameThreadRunning() const;

    // === Tick Processing (synchronous, for tests / backwards compat) ===
    /// Advance game state by dt seconds. Must NOT be called while game thread is running.
    void tick(float dt);

private:
    GameSession();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox
