/**
 * @file feature_tree.hpp
 * @brief Configurable tree feature for world generation
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#pragma once

#include "finevox/worldgen/feature.hpp"
#include "finevox/core/string_interner.hpp"

#include <string>

namespace finevox::worldgen {

/// Configuration for tree generation
struct TreeConfig {
    BlockTypeId trunkBlock;
    BlockTypeId leavesBlock;
    int32_t minTrunkHeight = 4;
    int32_t maxTrunkHeight = 7;
    int32_t leafRadius = 2;
    bool requiresSoil = true;       ///< Require solid block below origin
};

/// Generates simple trees with configurable trunk height and leaf canopy
class TreeFeature : public Feature {
public:
    explicit TreeFeature(std::string featureName, TreeConfig config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FeatureResult place(FeaturePlacementContext& ctx) override;
    [[nodiscard]] BlockPos maxExtent() const override;

private:
    std::string name_;
    TreeConfig config_;

    /// Deterministic trunk height from placement seed
    [[nodiscard]] int32_t trunkHeight(uint64_t seed) const;

    /// Check that the origin has suitable ground below
    [[nodiscard]] bool checkSoil(World& world, BlockPos origin) const;

    /// Check that the trunk area is clear (air)
    [[nodiscard]] bool checkClearance(World& world, BlockPos origin, int32_t height) const;
};

}  // namespace finevox::worldgen
