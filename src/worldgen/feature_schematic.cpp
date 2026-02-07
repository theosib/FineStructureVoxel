/**
 * @file feature_schematic.cpp
 * @brief Schematic-based feature for stamping loaded structures
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#include "finevox/worldgen/feature_schematic.hpp"
#include "finevox/core/world.hpp"

namespace finevox::worldgen {

SchematicFeature::SchematicFeature(std::string featureName,
                                   std::shared_ptr<const Schematic> schematic,
                                   bool ignoreAir)
    : name_(std::move(featureName)),
      schematic_(std::move(schematic)),
      ignoreAir_(ignoreAir) {}

std::string_view SchematicFeature::name() const {
    return name_;
}

FeatureResult SchematicFeature::place(FeaturePlacementContext& ctx) {
    if (!schematic_) return FeatureResult::Failed;

    int32_t placed = 0;

    schematic_->forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        if (ignoreAir_ && snap.isAir()) return;

        int32_t wx = ctx.origin.x + pos.x;
        int32_t wy = ctx.origin.y + pos.y;
        int32_t wz = ctx.origin.z + pos.z;

        BlockTypeId blockType = BlockTypeId::fromName(snap.typeName);
        ctx.world.setBlock(wx, wy, wz, blockType);
        ++placed;
    });

    return placed > 0 ? FeatureResult::Placed : FeatureResult::Skipped;
}

BlockPos SchematicFeature::maxExtent() const {
    if (!schematic_) return BlockPos(0, 0, 0);
    return BlockPos(schematic_->sizeX(), schematic_->sizeY(), schematic_->sizeZ());
}

}  // namespace finevox::worldgen
