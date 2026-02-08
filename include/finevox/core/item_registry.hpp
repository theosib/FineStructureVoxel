#pragma once

/**
 * @file item_registry.hpp
 * @brief Item type registration and lookup
 *
 * Design: Phase 13 Inventory & Items
 *
 * Stores ItemType structs keyed by ItemTypeId (interned name).
 * Thread-safe singleton registry, analogous to BlockRegistry.
 */

#include "finevox/core/item_type.hpp"
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace finevox {

class ItemRegistry {
public:
    /// Get the global item registry instance
    static ItemRegistry& global();

    /// Register an item type. ID comes from the ItemType's id field.
    /// @return true if registered, false if name already exists
    bool registerType(const ItemType& type);

    /// Convenience: register with just a name (creates default ItemType)
    bool registerType(std::string_view name);

    /// Look up by ItemTypeId
    [[nodiscard]] const ItemType* getType(ItemTypeId id) const;

    /// Look up by name (interns first)
    [[nodiscard]] const ItemType* getType(std::string_view name) const;

    /// Check existence
    [[nodiscard]] bool hasType(ItemTypeId id) const;
    [[nodiscard]] bool hasType(std::string_view name) const;

    /// Get number of registered item types
    [[nodiscard]] size_t size() const;

    /// Auto-register block items: for every block in BlockRegistry,
    /// create a corresponding item with placesBlock set.
    void registerBlockItems();

    // Non-copyable singleton
    ItemRegistry(const ItemRegistry&) = delete;
    ItemRegistry& operator=(const ItemRegistry&) = delete;

private:
    ItemRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<ItemTypeId, ItemType> types_;
};

}  // namespace finevox
