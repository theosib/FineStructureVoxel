#pragma once

/**
 * @file fluid_interaction.hpp
 * @brief Registry for fluid-fluid interaction rules
 *
 * Games register what happens when two different fluids meet.
 * Examples: water + lava → cobblestone block, custom fluid conversions.
 */

#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/string_interner.hpp"  // BlockTypeId
#include <shared_mutex>
#include <unordered_map>
#include <functional>

namespace finevox {

/// Describes the result of two fluids meeting
struct FluidInteraction {
    FluidTypeId fluidA;          // First fluid
    FluidTypeId fluidB;          // Second fluid
    BlockTypeId resultBlock;     // Block created at boundary (e.g., cobblestone)
    FluidTypeId resultFluid;     // Fluid that survives (empty = both removed)
    bool consumeA = true;        // Remove fluid A at contact point?
    bool consumeB = true;        // Remove fluid B at contact point?
};

/// Registry for fluid-fluid interaction rules.
/// Interactions are symmetric: registering (A, B) also covers (B, A).
class FluidInteractionRegistry {
public:
    /// Singleton accessor
    static FluidInteractionRegistry& global();

    /// Register an interaction between two fluid types.
    /// Returns false if this pair is already registered.
    bool registerInteraction(FluidInteraction interaction);

    /// Look up the interaction between two fluids.
    /// Returns nullptr if no interaction is registered (fluids ignore each other).
    /// Handles symmetry: query(A, B) == query(B, A), with fluidA/fluidB swapped if needed.
    [[nodiscard]] const FluidInteraction* getInteraction(FluidTypeId a, FluidTypeId b) const;

    /// Check if an interaction is registered for this pair
    [[nodiscard]] bool hasInteraction(FluidTypeId a, FluidTypeId b) const;

    /// Number of registered interactions
    [[nodiscard]] size_t size() const;

    /// Clear all registrations (for testing)
    void clear();

    // Non-copyable
    FluidInteractionRegistry(const FluidInteractionRegistry&) = delete;
    FluidInteractionRegistry& operator=(const FluidInteractionRegistry&) = delete;

private:
    FluidInteractionRegistry() = default;

    /// Create canonical key: smaller ID first
    [[nodiscard]] static uint64_t makeKey(FluidTypeId a, FluidTypeId b);

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, FluidInteraction> interactions_;
};

}  // namespace finevox
