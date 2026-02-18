#pragma once

/**
 * @file fluid_registry.hpp
 * @brief Singleton registry for fluid types
 *
 * Thread-safe concurrent reader/exclusive writer via shared_mutex.
 * Follows the same pattern as SoundRegistry, EntityTypeRegistry.
 */

#include "finevox/core/fluid_type.hpp"
#include <functional>
#include <shared_mutex>
#include <unordered_map>

namespace finevox {

class FluidRegistry {
public:
    /// Singleton accessor
    static FluidRegistry& global();

    /// Register a fluid type. Returns false if name already registered.
    bool registerType(const std::string& name, FluidType type);

    /// Look up by ID (returns nullptr if not found)
    [[nodiscard]] const FluidType* getType(FluidTypeId id) const;

    /// Look up by name (returns nullptr if not found)
    [[nodiscard]] const FluidType* getType(const std::string& name) const;

    /// Check if a fluid type is registered
    [[nodiscard]] bool hasType(FluidTypeId id) const;

    /// Get the FluidTypeId for a named fluid (returns EMPTY_FLUID_TYPE if not found)
    [[nodiscard]] FluidTypeId getTypeId(const std::string& name) const;

    /// Get total number of registered fluid types
    [[nodiscard]] size_t size() const;

    /// Iterate over all registered types
    void forEachType(const std::function<void(FluidTypeId, const FluidType&)>& fn) const;

    /// Clear all registrations (for testing)
    void clear();

    // Non-copyable
    FluidRegistry(const FluidRegistry&) = delete;
    FluidRegistry& operator=(const FluidRegistry&) = delete;

private:
    FluidRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<FluidTypeId, FluidType> types_;
};

}  // namespace finevox
