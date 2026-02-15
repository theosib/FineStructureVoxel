#pragma once

#include "finevox/core/game_actions.hpp"
#include <memory>

namespace finevox {

// Forward declarations
class World;
class UpdateScheduler;
class LightEngine;
class EntityManager;
class WorldTime;
class PhysicsSystem;
struct SoundEvent;
template<typename T> class Queue;
using SoundEventQueue = Queue<SoundEvent>;
struct GraphicsEvent;
using GraphicsEventQueue = Queue<GraphicsEvent>;
struct BlockEvent;
using GameCommandQueue = Queue<BlockEvent>;

/// Configuration for creating a GameSession
struct GameSessionConfig {
    bool enableLighting = true;
    bool enableSound = true;
    float gravity = -14.0f;
    uint32_t tickRate = 20;           // TPS
    uint32_t randomTicksPerChunk = 3;
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

    // === Event Channels (events come OUT) ===
    SoundEventQueue& soundEvents();
    GraphicsEventQueue& graphicsEvents();

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
