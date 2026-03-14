#pragma once

/**
 * @file spawn_predicate.hpp
 * @brief Extensible spawn predicate system for mod/script-defined spawn conditions
 *
 * Allows registering named predicates that evaluate custom conditions
 * beyond the built-in SpawnRule fields (light, surface, biome, distance).
 */

#include "finevox/core/position.hpp"
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finevox {

// Forward declarations
class World;
struct SpawnRule;

/// Context provided to spawn predicates for evaluation
struct SpawnContext {
    const World& world;
    BlockCoord pos;             // Candidate spawn position
    float timeOfDay;            // Current time of day [0, 1)
    float distanceToPlayer;     // Distance to nearest player
};

/// A predicate function that evaluates whether spawning is allowed
using SpawnPredicate = std::function<bool(const SpawnRule& rule, const SpawnContext& ctx)>;

/// Registry of named spawn predicates.
/// Mods/scripts register predicates here; SpawnManager evaluates them.
class SpawnPredicateRegistry {
public:
    /// Get the global singleton instance
    static SpawnPredicateRegistry& global();

    /// Register a named predicate
    void registerPredicate(std::string_view name, SpawnPredicate pred);

    /// Unregister a predicate
    void unregisterPredicate(std::string_view name);

    /// Check if a predicate is registered
    [[nodiscard]] bool hasPredicate(std::string_view name) const;

    /// Evaluate all predicates listed in a rule's customPredicates.
    /// Returns false if any predicate returns false (AND logic).
    [[nodiscard]] bool evaluateAll(const SpawnRule& rule, const SpawnContext& ctx) const;

    /// Get number of registered predicates
    [[nodiscard]] size_t size() const { return predicates_.size(); }

    /// Clear all predicates (for testing)
    void clear();

private:
    SpawnPredicateRegistry() = default;
    std::unordered_map<std::string, SpawnPredicate> predicates_;
};

}  // namespace finevox
