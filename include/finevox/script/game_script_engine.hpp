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
#include <memory>
#include <string>
#include <unordered_map>

namespace finevox {
class World;
}

namespace finevox::script {

/// User data passed through ExecutionContext::setUserData()
struct ScriptUserData {
    BlockContext* blockCtx = nullptr;
    World* world = nullptr;
};

class GameScriptEngine {
public:
    explicit GameScriptEngine(World& world);
    ~GameScriptEngine();

    /// Access the underlying finescript engine
    finescript::ScriptEngine& engine() { return *engine_; }

    /// Load a script file and create a persistent handler.
    /// Returns nullptr if the script doesn't register any event handlers.
    ScriptBlockHandler* loadBlockScript(const std::string& scriptPath,
                                         const std::string& blockName);

    /// Hot-reload: check all loaded scripts for changes.
    void reloadChangedScripts();

    /// Access cache for direct script use outside block handlers.
    ScriptCache& cache() { return cache_; }

private:
    void registerNativeFunctions();

    std::unique_ptr<finescript::ScriptEngine> engine_;
    FineVoxInterner interner_;
    ScriptCache cache_;
    World& world_;
    ScriptUserData userData_;

    // Owns all script block handlers (keyed by block name)
    std::unordered_map<std::string, std::unique_ptr<ScriptBlockHandler>> handlers_;
};

}  // namespace finevox::script
