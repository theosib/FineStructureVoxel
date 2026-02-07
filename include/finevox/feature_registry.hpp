/**
 * @file feature_registry.hpp
 * @brief Feature registration and placement rules
 *
 * Design: [27-world-generation.md] Sections 27.5.3-27.5.4
 *
 * FeatureRegistry is a global singleton populated during module init.
 * It stores Feature instances and their placement rules (density, height
 * range, biome filters).
 */

#pragma once

#include "finevox/biome.hpp"
#include "finevox/feature.hpp"
#include "finevox/string_interner.hpp"

#include <memory>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

// ============================================================================
// FeaturePlacement
// ============================================================================

/// Rules for how a feature is distributed during generation
struct FeaturePlacement {
    std::string featureName;
    float density = 0.01f;              ///< Probability per surface block
    int32_t minHeight = 0;
    int32_t maxHeight = 256;
    std::vector<BiomeId> biomes;        ///< Empty = all biomes
    bool requiresSurface = true;
    BlockTypeId requiredSurface;        ///< Default (air) = any solid block
};

// ============================================================================
// FeatureRegistry
// ============================================================================

/// Thread-safe global registry of features and their placement rules
class FeatureRegistry {
public:
    static FeatureRegistry& global();

    /// Register a feature (takes ownership)
    void registerFeature(std::shared_ptr<Feature> feature);

    /// Add a placement rule for a registered feature
    void addPlacement(FeaturePlacement placement);

    /// Get a feature by name
    [[nodiscard]] Feature* getFeature(std::string_view name) const;

    /// Get all placement rules
    [[nodiscard]] std::vector<FeaturePlacement> allPlacements() const;

    /// Get placement rules for a specific biome
    [[nodiscard]] std::vector<const FeaturePlacement*> placementsForBiome(BiomeId biome) const;

    /// Number of registered features
    [[nodiscard]] size_t featureCount() const;

    /// Number of placement rules
    [[nodiscard]] size_t placementCount() const;

    /// Clear all registrations (for testing)
    void clear();

private:
    FeatureRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<Feature>> features_;
    std::vector<FeaturePlacement> placements_;
};

}  // namespace finevox
