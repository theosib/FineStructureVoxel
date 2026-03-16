#pragma once

/**
 * @file inventory_bridge.hpp
 * @brief Bridges InventoryView operations to finescript native functions
 *
 * Manages named inventory owners (each a DataContainer with named sections)
 * and registers inv_* native functions on a script engine.
 *
 * Scripts reference inventories by owner name (e.g., "player", "chest_42"):
 *   {inv_get "player" "main" 0}      → item map or nil
 *   {inv_add "player" "main" "stone" 5} → int (remainder)
 *
 * Also registers the L label lookup function.
 */

#include "finevox/core/data_container.hpp"
#include "finevox/core/name_registry.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace finescript { class ScriptEngine; }

namespace finevox::script {

class InventoryBridge {
public:
    InventoryBridge();
    ~InventoryBridge();

    /// Register an inventory owner. The bridge does NOT own the DC or registry.
    void registerOwner(const std::string& name, DataContainer& dc, NameRegistry& registry);

    /// Unregister an inventory owner
    void unregisterOwner(const std::string& name);

    /// Register all native functions (inv_*, L, item_icon) on the given script engine
    void registerNativeFunctions(finescript::ScriptEngine& engine);

    /// Icon info returned by the icon lookup callback
    struct IconInfo {
        std::string textureName;  ///< Name in TextureRegistry (e.g., "block_atlas")
        float u0, v0, u1, v1;    ///< UV coordinates
    };

    /// Callback type for icon lookup: typeName → optional IconInfo
    using IconLookup = std::function<std::optional<IconInfo>(std::string_view typeName)>;

    /// Set the icon lookup callback (enables item_icon native function)
    void setIconLookup(IconLookup lookup);

private:
    struct OwnerEntry {
        DataContainer* dc;
        NameRegistry* registry;
    };

    std::unordered_map<std::string, OwnerEntry> owners_;
    mutable std::mutex mutex_;

    OwnerEntry* findOwner(const std::string& name);
    IconLookup iconLookup_;
};

}  // namespace finevox::script
