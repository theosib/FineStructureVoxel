#pragma once

/**
 * @file entity_registry.hpp
 * @brief Entity type registration (stub)
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
// EntityRegistry - Stub for entity type registration
// ============================================================================

/**
 * @brief Registry for entity types (STUB - Phase 7)
 *
 * This is a placeholder for the entity registration system.
 * Full implementation will come when the entity system is developed.
 *
 * Entities are dynamic game objects: players, mobs, items on ground,
 * minecarts, etc.
 */
class EntityRegistry {
public:
    /**
     * @brief Get the global entity registry instance
     */
    static EntityRegistry& global();

    /**
     * @brief Register an entity type (stub)
     *
     * Currently just tracks the name for validation purposes.
     *
     * @param name Fully-qualified entity name (e.g., "blockgame:zombie")
     * @return true if registered, false if name already exists
     */
    bool registerType(std::string_view name);

    /**
     * @brief Check if an entity type is registered
     * @param name Entity type name
     * @return true if registered
     */
    [[nodiscard]] bool hasType(std::string_view name) const;

    /**
     * @brief Get number of registered entity types
     */
    [[nodiscard]] size_t size() const;

    // Non-copyable singleton
    EntityRegistry(const EntityRegistry&) = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;

private:
    EntityRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, bool> types_;  // Just names for now
};

}  // namespace finevox
