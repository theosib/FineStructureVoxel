#pragma once

/**
 * @file item_registry.hpp
 * @brief Item type registration (stub)
 *
 * Design: [18-modules.md] §18.5 Registries
 */

#include <string>
#include <string_view>
#include <unordered_map>
#include <shared_mutex>
#include <memory>

namespace finevox {

// ============================================================================
// ItemRegistry - Stub for item type registration
// ============================================================================

/**
 * @brief Registry for item types (STUB - Phase 7)
 *
 * This is a placeholder for the item registration system.
 * Full implementation will come when the inventory system is developed.
 *
 * Items are things that can be in inventories: tools, materials, food, etc.
 * Many blocks have corresponding items (for placement), but items and blocks
 * are registered separately.
 */
class ItemRegistry {
public:
    /**
     * @brief Get the global item registry instance
     */
    static ItemRegistry& global();

    /**
     * @brief Register an item type (stub)
     *
     * Currently just tracks the name for validation purposes.
     *
     * @param name Fully-qualified item name (e.g., "blockgame:diamond_sword")
     * @return true if registered, false if name already exists
     */
    bool registerType(std::string_view name);

    /**
     * @brief Check if an item type is registered
     * @param name Item type name
     * @return true if registered
     */
    [[nodiscard]] bool hasType(std::string_view name) const;

    /**
     * @brief Get number of registered item types
     */
    [[nodiscard]] size_t size() const;

    // Non-copyable singleton
    ItemRegistry(const ItemRegistry&) = delete;
    ItemRegistry& operator=(const ItemRegistry&) = delete;

private:
    ItemRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, bool> types_;  // Just names for now
};

}  // namespace finevox
