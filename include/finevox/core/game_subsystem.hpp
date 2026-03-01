#pragma once

/**
 * @file game_subsystem.hpp
 * @brief Interface for pluggable game-thread subsystems
 *
 * Subsystems are registered with GameSession and ticked in a deterministic
 * order based on (TickPhase, priority). The game thread iterates all
 * registered subsystems each tick.
 *
 * Lifecycle:
 * 1. Subsystem created (by application or module)
 * 2. onAttach() called when added to GameSession
 * 3. tick() called every game tick in phase/priority order
 * 4. onDetach() called during shutdown
 */

#include <cstdint>
#include <memory>
#include <string_view>

namespace finevox {

// Forward declarations
class GameSession;
class EventBus;

// ============================================================================
// TickPhase - When during a game tick a subsystem is updated
// ============================================================================

/**
 * @brief Phases within a single game tick
 *
 * Each tick proceeds through phases in order. Subsystems register for
 * a specific phase and run in priority order within that phase.
 */
enum class TickPhase : uint8_t {
    PreTick  = 0,  ///< Time advance, external event drain
    Tick     = 1,  ///< Block events (process-until-stable), fluid sim
    PostTick = 2,  ///< Entity AI, physics, spawning
    LateTick = 3,  ///< Publish snapshots, sound events
};

// ============================================================================
// GameSubsystem - Interface for game-thread subsystems
// ============================================================================

class GameSubsystem : public std::enable_shared_from_this<GameSubsystem> {
public:
    virtual ~GameSubsystem() = default;

    /// Unique name for this subsystem (for logging and lookup)
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Which tick phase this subsystem runs in
    [[nodiscard]] virtual TickPhase phase() const = 0;

    /// Priority within the phase (lower = earlier). Default: 100.
    [[nodiscard]] virtual int32_t priority() const { return 100; }

    /**
     * @brief Called when the subsystem is attached to a GameSession
     *
     * Use for initialization that requires GameSession access:
     * subscribing to events, accessing World, etc.
     */
    virtual void onAttach(GameSession& session, EventBus& eventBus) {
        (void)session;
        (void)eventBus;
    }

    /**
     * @brief Called when the subsystem is detached from a GameSession
     *
     * Use for cleanup. Called during shutdown in reverse registration order.
     */
    virtual void onDetach() {}

    /**
     * @brief Called every game tick
     *
     * This is the main update method. Called in (phase, priority) order.
     *
     * @param dt Time since last tick in seconds (typically 1/30)
     */
    virtual void tick(float dt) = 0;
};

} // namespace finevox
