/**
 * @file feature.hpp
 * @brief Feature interface for world generation decorations
 *
 * Design: [27-world-generation.md] Section 27.5.1
 *
 * Features are multi-block structures placed during world generation
 * (trees, ore veins, structures). Each Feature knows how to place itself
 * given a placement context with position, seed, and world access.
 */

#pragma once

#include "finevox/biome.hpp"
#include "finevox/position.hpp"

#include <cstdint>
#include <string_view>

namespace finevox {

class World;
struct GenerationContext;

// ============================================================================
// FeatureResult
// ============================================================================

/// Outcome of a feature placement attempt
enum class FeatureResult {
    Placed,     ///< Feature was successfully placed
    Skipped,    ///< Placement skipped (conditions not met, e.g., no soil)
    Failed      ///< Placement failed (error)
};

// ============================================================================
// FeaturePlacementContext
// ============================================================================

/// Context passed to Feature::place() with all information needed for placement
struct FeaturePlacementContext {
    World& world;
    BlockPos origin;                ///< Placement origin (usually surface position)
    BiomeId biome;
    uint64_t seed;                  ///< Per-placement deterministic seed
    GenerationContext* genCtx;      ///< Null for runtime placement
};

// ============================================================================
// Feature Interface
// ============================================================================

/// Abstract base for all world generation features
class Feature {
public:
    virtual ~Feature() = default;

    /// Name of this feature type (e.g., "oak_tree", "iron_ore")
    [[nodiscard]] virtual std::string_view name() const = 0;

    /// Attempt to place this feature at the given context
    [[nodiscard]] virtual FeatureResult place(FeaturePlacementContext& ctx) = 0;

    /// Maximum extent this feature can reach from its origin (for cross-column checks)
    [[nodiscard]] virtual BlockPos maxExtent() const { return BlockPos(1, 1, 1); }
};

}  // namespace finevox
