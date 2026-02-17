#pragma once

/**
 * @file script_entity_handler.hpp
 * @brief Script handler for entity events
 *
 * Owns a persistent ExecutionContext containing cached event closures.
 * Entity events (spawn, tick, damage, death, interact, strike) dispatch to
 * the matching script closure via pre-interned symbol IDs.
 */

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <string>
#include <unordered_map>

namespace finevox {
class MobEntity;
}

namespace finevox::script {

class ScriptEntityHandler {
public:
    ScriptEntityHandler(const std::string& name,
                        finescript::ScriptEngine& engine,
                        std::unique_ptr<finescript::ExecutionContext> ctx);

    [[nodiscard]] const std::string& name() const { return name_; }

    /// Dispatch entity events to script
    void onSpawn(MobEntity& mob);
    void onTick(MobEntity& mob, float dt);
    void onDamage(MobEntity& mob, float amount, uint64_t source);
    void onDeath(MobEntity& mob, uint64_t killer);
    void onInteract(MobEntity& mob, uint64_t player);
    void onStrike(MobEntity& mob, uint64_t attacker);

    [[nodiscard]] bool hasHandlers() const { return !handlers_.empty(); }
    [[nodiscard]] finescript::ExecutionContext& context() { return *ctx_; }

private:
    finescript::Value invokeHandler(uint32_t eventSymbol, MobEntity& mob);

    std::string name_;
    finescript::ScriptEngine& engine_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;
    std::unordered_map<uint32_t, finescript::Value> handlers_;
};

}  // namespace finevox::script
