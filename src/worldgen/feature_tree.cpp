/**
 * @file feature_tree.cpp
 * @brief Configurable tree feature implementation
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#include "finevox/worldgen/feature_tree.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/core/world.hpp"

namespace finevox::worldgen {

TreeFeature::TreeFeature(std::string featureName, TreeConfig config)
    : name_(std::move(featureName)), config_(config) {}

std::string_view TreeFeature::name() const {
    return name_;
}

FeatureResult TreeFeature::place(FeaturePlacementContext& ctx) {
    if (config_.requiresSoil && !checkSoil(ctx.world, ctx.origin)) {
        return FeatureResult::Skipped;
    }

    int32_t height = trunkHeight(ctx.seed);

    if (!checkClearance(ctx.world, ctx.origin, height)) {
        return FeatureResult::Skipped;
    }

    // Place trunk
    for (int32_t y = 0; y < height; ++y) {
        ctx.world.setBlock(
            ctx.origin.x, ctx.origin.y + y, ctx.origin.z,
            config_.trunkBlock);
    }

    // Place leaf canopy
    // Leaves form a sphere-ish shape around the top of the trunk
    int32_t leafBase = height - config_.leafRadius - 1;
    if (leafBase < 1) leafBase = 1;
    int32_t leafTop = height + 1;
    int32_t r = config_.leafRadius;

    for (int32_t dy = leafBase; dy <= leafTop; ++dy) {
        // Narrower radius at top and bottom of canopy
        int32_t layerR = r;
        if (dy == leafBase || dy == leafTop) {
            layerR = std::max(1, r - 1);
        }

        for (int32_t dx = -layerR; dx <= layerR; ++dx) {
            for (int32_t dz = -layerR; dz <= layerR; ++dz) {
                // Skip corners for a more natural shape
                if (std::abs(dx) == layerR && std::abs(dz) == layerR) {
                    // Use seed-based hash to randomly keep/remove corners
                    uint64_t cornerHash = NoiseHash::hash2D(
                        ctx.seed, dx + ctx.origin.x, dz + ctx.origin.z);
                    if (cornerHash % 3 != 0) continue;
                }

                // Don't replace trunk
                if (dx == 0 && dz == 0 && dy < height) continue;

                int32_t wx = ctx.origin.x + dx;
                int32_t wy = ctx.origin.y + dy;
                int32_t wz = ctx.origin.z + dz;

                // Only place leaves in air
                if (ctx.world.getBlock(wx, wy, wz).isAir()) {
                    ctx.world.setBlock(wx, wy, wz, config_.leavesBlock);
                }
            }
        }
    }

    return FeatureResult::Placed;
}

BlockPos TreeFeature::maxExtent() const {
    int32_t r = config_.leafRadius;
    return BlockPos(r, config_.maxTrunkHeight + 2, r);
}

int32_t TreeFeature::trunkHeight(uint64_t seed) const {
    int32_t range = config_.maxTrunkHeight - config_.minTrunkHeight;
    if (range <= 0) return config_.minTrunkHeight;
    return config_.minTrunkHeight +
           static_cast<int32_t>(seed % static_cast<uint64_t>(range + 1));
}

bool TreeFeature::checkSoil(World& world, BlockPos origin) const {
    // Check that the block below origin is a solid (non-air) block
    BlockTypeId below = world.getBlock(origin.x, origin.y - 1, origin.z);
    return !below.isAir();
}

bool TreeFeature::checkClearance(World& world, BlockPos origin, int32_t height) const {
    // Check trunk column is clear
    for (int32_t y = 0; y < height; ++y) {
        BlockTypeId block = world.getBlock(origin.x, origin.y + y, origin.z);
        if (!block.isAir()) return false;
    }
    return true;
}

}  // namespace finevox::worldgen
