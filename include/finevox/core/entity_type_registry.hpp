#pragma once

/**
 * @file entity_type_registry.hpp
 * @brief Registry mapping EntityTypeId to EntityTypeDef
 *
 * Thread-safe singleton registry for entity type definitions.
 * Entity types are registered during initialization, then looked up at runtime.
 */

#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/entity_type_def.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <functional>

namespace finevox {

class EntityTypeRegistry {
public:
    /// Get the global registry instance (singleton)
    static EntityTypeRegistry& global();

    /// Register an entity type definition
    /// Returns false if ID is already registered (won't overwrite)
    bool registerType(std::string_view name, EntityTypeDef def);

    /// Get entity type definition by ID
    /// Returns nullptr if not registered
    [[nodiscard]] const EntityTypeDef* getType(EntityTypeId id) const;

    /// Get entity type definition by name
    /// Returns nullptr if not registered
    [[nodiscard]] const EntityTypeDef* getType(std::string_view name) const;

    /// Check if a type is registered
    [[nodiscard]] bool hasType(EntityTypeId id) const;

    /// Check if a type is registered by name
    [[nodiscard]] bool hasType(std::string_view name) const;

    /// Get number of registered types
    [[nodiscard]] size_t size() const;

    /// Iterate over all registered types
    void forEachType(const std::function<void(EntityTypeId, const EntityTypeDef&)>& fn) const;

    /// Clear all registered types (for testing)
    void clear();

    // Non-copyable singleton
    EntityTypeRegistry(const EntityTypeRegistry&) = delete;
    EntityTypeRegistry& operator=(const EntityTypeRegistry&) = delete;

private:
    EntityTypeRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<EntityTypeId, EntityTypeDef> types_;
};

}  // namespace finevox
