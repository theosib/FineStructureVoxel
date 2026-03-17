#pragma once

/**
 * @file ai_driver.hpp
 * @brief AIDriver — top-level decision-maker for entity behavior
 *
 * Replaces AIBrain as the entry point for entity AI. AIBrain (goal system)
 * becomes one tool that drivers can use internally.
 *
 * Three implementations:
 * - ScriptAIDriver: delegates to finescript event handlers + optional AIBrain goals
 * - PlayerInputDriver: consumes input messages from graphics-thread proxy
 * - NativeAIDriver: loads from game module shared library (future)
 */

#include "finevox/core/entity_state.hpp"
#include "finevox/core/string_interner.hpp"
#include <finescript/value.h>
#include <vector>

namespace finevox {

class MobEntity;

class AIDriver {
public:
    virtual ~AIDriver() = default;

    /// Periodic update — called every game tick after entity's own tick
    virtual void tick(MobEntity& mob, float dt) = 0;

    /// Event delivery — driver receives only events it subscribed to.
    /// Called immediately when the game thread processes a command targeting this entity.
    virtual void onEvent(MobEntity& mob, const finescript::Value& event) = 0;

    /// Declare which event types this driver consumes (checked once at registration).
    /// Events not in this set are silently dropped for this entity.
    virtual std::vector<InternedId> subscribedEvents() const = 0;

    /// Check if driver subscribes to a given event type
    bool subscribesTo(InternedId eventType) const;
};

// ============================================================================
// BrainAIDriver — wraps the existing AIBrain goal system
// ============================================================================

/**
 * Default driver for mobs with AIType != None.
 * Delegates tick to AIBrain and doesn't consume any events itself.
 * Event hooks (onDamage, onDeath) are handled separately by MobEventHooks.
 */
class BrainAIDriver : public AIDriver {
public:
    void tick(MobEntity& mob, float dt) override;
    void onEvent(MobEntity& mob, const finescript::Value& event) override;
    std::vector<InternedId> subscribedEvents() const override;
};

// ============================================================================
// PlayerInputDriver — consumes input from graphics-thread proxy
// ============================================================================

/**
 * Driver for player entities. Consumes movement, look, jump, sprint,
 * sneak, attack, and use events from the graphics thread.
 *
 * The player's graphics-thread proxy handles responsive movement;
 * this driver receives authoritative state updates and interaction events.
 */
class PlayerInputDriver : public AIDriver {
public:
    PlayerInputDriver();

    void tick(MobEntity& mob, float dt) override;
    void onEvent(MobEntity& mob, const finescript::Value& event) override;
    std::vector<InternedId> subscribedEvents() const override;

    /// Track whether player is sprinting/sneaking
    [[nodiscard]] bool isSprinting() const { return sprinting_; }
    [[nodiscard]] bool isSneaking() const { return sneaking_; }

private:
    bool sprinting_ = false;
    bool sneaking_ = false;

    // Pre-interned event type symbols
    struct EventSyms {
        InternedId move, look, jump, sprint, sneak, attack, use_block;
        static const EventSyms& instance();
    };
};

}  // namespace finevox
