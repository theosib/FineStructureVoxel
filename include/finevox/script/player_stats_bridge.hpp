#pragma once

/**
 * @file player_stats_bridge.hpp
 * @brief HUD bridge: registers native functions for UI scripts to read player state
 *
 * These functions are registered on the GUI engine (render thread) and read
 * player entity state for HUD display. They operate on the local player entity
 * found via EntityManager.
 */

#include <finescript/script_engine.h>

namespace finevox {

class EntityManager;

namespace script {

class PlayerStatsBridge {
public:
    explicit PlayerStatsBridge(EntityManager& em) : entityManager_(em) {}

    /// Register all player stat query functions on the given engine
    void registerNativeFunctions(finescript::ScriptEngine& engine);

private:
    EntityManager& entityManager_;
};

}  // namespace script
}  // namespace finevox
