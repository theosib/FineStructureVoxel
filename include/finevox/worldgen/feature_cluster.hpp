/**
 * @file feature_cluster.hpp
 * @brief Scatter of a single block type across the surface around an origin
 *
 * Covers forage patches, boulders, and similar surface decoration. Unlike a
 * tree there is no structure to it: each attempt picks a nearby column and, if
 * that column's surface can hold the block, places a short stack there.
 */

#pragma once

#include "finevox/worldgen/feature.hpp"
#include "finevox/core/string_interner.hpp"

#include <limits>
#include <string>

namespace finevox::worldgen {

struct ClusterConfig {
    /// A different block at the top of a stack of two or more (`top:`), for a plant whose
    /// upper half isn't its lower
    BlockTypeId topBlock;
    BlockTypeId block;
    BlockTypeId requiredBelow;   ///< Default (air) = any solid block
    int32_t attempts = 12;       ///< Columns tried around the origin
    int32_t radius = 3;          ///< Horizontal spread
    int32_t minHeight = 1;       ///< Stack height range
    int32_t maxHeight = 1;
    /// Lowest ground any of it stands on (`place_min_height`, which also limits where the
    /// cluster's middle goes). Its blocks spread and drop to the ground, so without this one
    /// at a shore ends up on the bed, where the water takes all but the top of a stack.
    int32_t lowestGround = std::numeric_limits<int32_t>::min();
};

class ClusterFeature : public Feature {
public:
    ClusterFeature(std::string featureName, ClusterConfig config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FeatureResult place(FeaturePlacementContext& ctx) override;
    [[nodiscard]] BlockCoord maxExtent() const override;

private:
    std::string name_;
    ClusterConfig config_;
};

}  // namespace finevox::worldgen
