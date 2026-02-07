/**
 * @file feature_ore.hpp
 * @brief Ore vein feature for world generation
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#pragma once

#include "finevox/feature.hpp"
#include "finevox/string_interner.hpp"

#include <string>

namespace finevox {

/// Configuration for ore vein generation
struct OreConfig {
    BlockTypeId oreBlock;
    BlockTypeId replaceBlock;       ///< Block to replace (e.g., stone)
    int32_t veinSize = 8;           ///< Max blocks per vein
    int32_t minHeight = 0;
    int32_t maxHeight = 64;
    int32_t veinsPerChunk = 8;      ///< Used by placement rules, not by Feature itself
};

/// Places ore veins using random walk from a center point
class OreFeature : public Feature {
public:
    explicit OreFeature(std::string featureName, OreConfig config);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FeatureResult place(FeaturePlacementContext& ctx) override;
    [[nodiscard]] BlockPos maxExtent() const override;

private:
    std::string name_;
    OreConfig config_;
};

}  // namespace finevox
