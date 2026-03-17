#pragma once

/**
 * @file script_mob_event_hooks.hpp
 * @brief Adapter bridging MobEventHooks (core) to ScriptEntityHandler (script)
 *
 * Allows core library MobEntity to fire event hooks that are dispatched
 * to finescript handlers without core depending on the script library.
 */

#include "finevox/core/mob_event_hooks.hpp"
#include "finevox/script/script_entity_handler.hpp"

namespace finevox::script {

class ScriptMobEventHooks : public MobEventHooks {
public:
    explicit ScriptMobEventHooks(ScriptEntityHandler& handler)
        : handler_(handler) {}

    void onSpawn(MobEntity& mob) override { handler_.onSpawn(mob); }
    void onTick(MobEntity& mob, float dt) override { handler_.onTick(mob, dt); }
    void onDamage(MobEntity& mob, float amount, EntityId source) override { handler_.onDamage(mob, amount, source); }
    void onDeath(MobEntity& mob, EntityId killer) override { handler_.onDeath(mob, killer); }
    void onInteract(MobEntity& mob, EntityId player) override { handler_.onInteract(mob, player); }
    void onStrike(MobEntity& mob, EntityId attacker) override { handler_.onStrike(mob, attacker); }

private:
    ScriptEntityHandler& handler_;
};

}  // namespace finevox::script
