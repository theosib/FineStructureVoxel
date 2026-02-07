/**
 * @file feature_ore.cpp
 * @brief Ore vein feature using random walk
 *
 * Design: [27-world-generation.md] Section 27.5.2
 */

#include "finevox/worldgen/feature_ore.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/core/world.hpp"

namespace finevox::worldgen {

OreFeature::OreFeature(std::string featureName, OreConfig config)
    : name_(std::move(featureName)), config_(config) {}

std::string_view OreFeature::name() const {
    return name_;
}

FeatureResult OreFeature::place(FeaturePlacementContext& ctx) {
    int32_t placed = 0;
    int32_t cx = ctx.origin.x;
    int32_t cy = ctx.origin.y;
    int32_t cz = ctx.origin.z;

    // Check height range
    if (cy < config_.minHeight || cy > config_.maxHeight) {
        return FeatureResult::Skipped;
    }

    uint64_t rng = ctx.seed;

    for (int32_t i = 0; i < config_.veinSize; ++i) {
        // Place ore if current position has the replaceable block
        BlockTypeId current = ctx.world.getBlock(cx, cy, cz);
        if (current == config_.replaceBlock) {
            ctx.world.setBlock(cx, cy, cz, config_.oreBlock);
            ++placed;
        }

        // Random walk to next position
        // SplitMix64-style step for the RNG
        rng += 0x9e3779b97f4a7c15ULL;
        uint64_t z = rng;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z = z ^ (z >> 31);

        // Pick a direction (6 faces)
        switch (z % 6) {
            case 0: ++cx; break;
            case 1: --cx; break;
            case 2: ++cy; break;
            case 3: --cy; break;
            case 4: ++cz; break;
            case 5: --cz; break;
        }
    }

    return placed > 0 ? FeatureResult::Placed : FeatureResult::Skipped;
}

BlockPos OreFeature::maxExtent() const {
    // Ore veins are small and stay near origin
    int32_t r = config_.veinSize;
    return BlockPos(r, r, r);
}

}  // namespace finevox::worldgen
