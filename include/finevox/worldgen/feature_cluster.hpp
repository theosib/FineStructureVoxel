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

#include <string>

namespace finevox::worldgen {

struct ClusterConfig {
    BlockTypeId block;
    BlockTypeId requiredBelow;   ///< Default (air) = any solid block
    int32_t attempts = 12;       ///< Columns tried around the origin
    int32_t radius = 3;          ///< Horizontal spread
    int32_t minHeight = 1;       ///< Stack height range
    int32_t maxHeight = 1;
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
