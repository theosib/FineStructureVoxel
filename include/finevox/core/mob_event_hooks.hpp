#pragma once

/**
 * @file mob_event_hooks.hpp
 * @brief Virtual callback interface for entity lifecycle events
 *
 * Design: Phase 24 — Entity System Fixes & Script Wiring
 *
 * Core-defined interface implemented by the script layer (ScriptMobEventHooks).
 * Same pattern as BlockHandler — lives in libfinevox so there is no dependency
 * on libfinevox_script. One instance per entity type (all zombies share one).
 */

#include "finevox/core/entity_state.hpp"  // EntityId

namespace finevox {

class MobEntity;

struct MobEventHooks {
    virtual ~MobEventHooks() = default;

    /// Called when entity enters the world
    virtual void onSpawn(MobEntity& mob) = 0;

    /// Called every game tick (after built-in AI and movement)
    virtual void onTick(MobEntity& mob, float dt) = 0;

    /// Called when entity takes damage
    virtual void onDamage(MobEntity& mob, float amount, EntityId source) = 0;

    /// Called when entity health reaches zero (before removal)
    virtual void onDeath(MobEntity& mob, EntityId killer) = 0;

    /// Called when another entity interacts (right-click) with this entity
    virtual void onInteract(MobEntity& mob, EntityId player) = 0;

    /// Called when this entity is struck by another
    virtual void onStrike(MobEntity& mob, EntityId attacker) = 0;
};

}  // namespace finevox
