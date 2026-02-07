/**
 * @file feature_schematic.hpp
 * @brief Schematic-based feature for stamping loaded structures
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#pragma once

#include "finevox/worldgen/feature.hpp"
#include "finevox/worldgen/schematic.hpp"

#include <memory>
#include <string>

namespace finevox::worldgen {

/// Stamps a loaded Schematic at the placement origin
class SchematicFeature : public Feature {
public:
    /// @param featureName Logical name for this feature
    /// @param schematic The schematic to place (shared ownership)
    /// @param ignoreAir If true, air blocks in the schematic don't overwrite world blocks
    SchematicFeature(std::string featureName,
                     std::shared_ptr<const Schematic> schematic,
                     bool ignoreAir = true);

    [[nodiscard]] std::string_view name() const override;
    [[nodiscard]] FeatureResult place(FeaturePlacementContext& ctx) override;
    [[nodiscard]] BlockPos maxExtent() const override;

private:
    std::string name_;
    std::shared_ptr<const Schematic> schematic_;
    bool ignoreAir_;
};

}  // namespace finevox::worldgen
