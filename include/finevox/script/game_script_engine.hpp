#pragma once

/**
 * @file game_script_engine.hpp
 * @brief Central owner of the finescript scripting subsystem
 *
 * Creates and owns the ScriptEngine, sets the shared interner,
 * registers native function bindings (ctx.*, world.*), and
 * manages ScriptBlockHandlers for scripted block types.
 */

#include "finevox/script/finevox_interner.hpp"
#include "finevox/script/script_cache.hpp"
#include "finevox/script/script_block_handler.hpp"
#include "finevox/script/script_entity_handler.hpp"
#include "finevox/script/script_mob_event_hooks.hpp"
#include "finevox/core/entity_manager.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace finevox {
class World;
class MobEntity;
class EntityManager;
class FluidTickManager;
}

namespace finevox {
class NavigationSystem;
class WorkClaimRegistry;
}

namespace finevox::script {

class ScriptGameSubsystem;

/// User data passed through ExecutionContext::setUserData()
struct ScriptUserData {
    BlockContext* blockCtx = nullptr;
    World* world = nullptr;
    MobEntity* entityCtx = nullptr;
    EntityManager* entityManager = nullptr;
    FluidTickManager* fluidTicks = nullptr;
    NavigationSystem* navigation = nullptr;
    WorkClaimRegistry* workClaims = nullptr;
    ScriptGameSubsystem* gameScript = nullptr;
};

/// `loc` and friends. Separate from GameScriptEngine so a UI script engine, which
/// has no World, can have them too.
void registerLocalizationNatives(finescript::ScriptEngine& engine);

class GameScriptEngine {
public:
    explicit GameScriptEngine(World& world);
    ~GameScriptEngine();

    /// Access the underlying finescript engine
    finescript::ScriptEngine& engine() { return *engine_; }

    /// Load a script file and create a persistent block handler.
    /// Returns nullptr if the script doesn't register any event handlers.
    ScriptBlockHandler* loadBlockScript(const std::string& scriptPath,
                                         const std::string& blockName);

    /// Load a script file and create a persistent entity handler.
    /// Returns nullptr if the script doesn't register any event handlers.
    ScriptEntityHandler* loadEntityScript(const std::string& scriptPath,
                                           const std::string& entityName);

    /// Get an entity handler by name
    ScriptEntityHandler* getEntityHandler(const std::string& entityName);

    /// Hot-reload: check all loaded scripts for changes.
    void reloadChangedScripts();

    /// Access cache for direct script use outside block handlers.
    ScriptCache& cache() { return cache_; }

    /// Set entity manager reference (for mob.spawn etc.)
    void setEntityManager(EntityManager* em);

    /// Set fluid tick manager reference (for script fluid/block notifications)
    void setFluidTickManager(FluidTickManager* ftm);

    /// Set navigation service (for ent_goto and friends)
    void setNavigation(NavigationSystem* nav) { userData_.navigation = nav; }

    /// Set work claim registry (for claim_block and friends)
    void setWorkClaims(WorkClaimRegistry* claims) { userData_.workClaims = claims; }

    /// Shared user data for contexts created outside this class
    [[nodiscard]] ScriptUserData& userData() { return userData_; }

    /// Load entity scripts for all registered entity types with script fields
    void loadEntityScriptsFromRegistry();

    /// Create a MobEventHooksProvider that looks up handlers by entity type name
    MobEventHooksProvider createHooksProvider();

private:
    void registerNativeFunctions();
    void registerMobNativeFunctions();
    void registerSpatialNativeFunctions();
    void registerCombatNativeFunctions();
    void registerUnitNativeFunctions();

    std::unique_ptr<finescript::ScriptEngine> engine_;
    FineVoxInterner interner_;
    ScriptCache cache_;
    World& world_;
    ScriptUserData userData_;

    // Owns all script block handlers (keyed by block name)
    std::unordered_map<std::string, std::unique_ptr<ScriptBlockHandler>> handlers_;

    // Owns all script entity handlers (keyed by entity name)
    std::unordered_map<std::string, std::unique_ptr<ScriptEntityHandler>> entityHandlers_;

    // Owns hook adapters (keyed by entity name, lazy-created by hooks provider)
    std::unordered_map<std::string, std::unique_ptr<ScriptMobEventHooks>> hooksAdapters_;
};

}  // namespace finevox::script
