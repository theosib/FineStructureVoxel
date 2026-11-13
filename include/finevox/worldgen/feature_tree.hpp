/**
 * @file feature_tree.hpp
 * @brief Configurable tree feature for world generation
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#pragma once

#include <vector>
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

    /// A crown given layer by layer, bottom to top, in place of the default box: each a radius,
    /// with `~` dropping its corners at random or `+` cutting them (a plus at radius 1). The
    /// top layer sits crownTop blocks above the trunk's last block, so 1 caps it.
    struct CrownLayer {
        int32_t radius = 1;
        enum class Corners : uint8_t { Keep, Random, Cut } corners = Corners::Keep;
    };
    std::vector<CrownLayer> crown;
    int32_t crownTop = 1;
};

/// Generates simple trees with configurable trunk height and leaf canopy
class TreeFeature : public Feature {
public:
    explicit TreeFeature(std::string featureName, TreeConfig config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FeatureResult place(FeaturePlacementContext& ctx) override;
    [[nodiscard]] BlockCoord maxExtent() const override;

private:
    std::string name_;
    TreeConfig config_;

    /// Deterministic trunk height from placement seed
    [[nodiscard]] int32_t trunkHeight(uint64_t seed) const;

    /// Check that the origin has suitable ground below
    [[nodiscard]] bool checkSoil(World& world, BlockCoord origin) const;

    /// Check that the trunk area is clear (air)
    [[nodiscard]] bool checkClearance(World& world, BlockCoord origin, int32_t height) const;
};

}  // namespace finevox::worldgen
