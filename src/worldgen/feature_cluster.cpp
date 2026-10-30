/**
 * @file feature_cluster.cpp
 * @brief Surface scatter feature
 */

#include "finevox/worldgen/feature_cluster.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/world.hpp"

namespace finevox::worldgen {

namespace {

// splitmix64 step: placement must be deterministic for a given world seed
uint64_t nextRandom(uint64_t& state) {
    state += 0x9e3779b97f4a7c15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

bool isOpen(const World& world, const BlockCoord& pos) {
    return world.getBlock(pos).isAir();
}

}  // namespace

ClusterFeature::ClusterFeature(std::string featureName, ClusterConfig config)
    : name_(std::move(featureName)), config_(config) {}

std::string_view ClusterFeature::name() const {
    return name_;
}

BlockCoord ClusterFeature::maxExtent() const {
    return BlockCoord(config_.radius, config_.maxHeight + 2, config_.radius);
}

FeatureResult ClusterFeature::place(FeaturePlacementContext& ctx) {
    uint64_t rng = ctx.seed;
    int32_t span = config_.radius * 2 + 1;
    int32_t heightSpan = std::max(1, config_.maxHeight - config_.minHeight + 1);
    int32_t placed = 0;

    for (int32_t attempt = 0; attempt < config_.attempts; ++attempt) {
        int32_t dx = static_cast<int32_t>(nextRandom(rng) % span) - config_.radius;
        int32_t dz = static_cast<int32_t>(nextRandom(rng) % span) - config_.radius;
        int32_t height = config_.minHeight + static_cast<int32_t>(nextRandom(rng) % heightSpan);

        // Settle onto the local surface: terrain near the origin may differ by a
        // block or two, and a cluster should hug it rather than float.
        BlockCoord pos(ctx.origin.x + dx, ctx.origin.y + 2, ctx.origin.z + dz);
        for (int32_t drop = 0; drop < 5 && isOpen(ctx.world, BlockCoord(pos.x, pos.y - 1, pos.z));
             ++drop) {
            --pos.y;
        }
        BlockCoord below(pos.x, pos.y - 1, pos.z);
        if (isOpen(ctx.world, below) || !isOpen(ctx.world, pos)) continue;
        if (!config_.requiredBelow.isAir() && ctx.world.getBlock(below) != config_.requiredBelow) {
            continue;
        }

        for (int32_t y = 0; y < height; ++y) {
            BlockCoord cell(pos.x, pos.y + y, pos.z);
            if (!isOpen(ctx.world, cell)) break;
            ctx.world.setBlock(cell, config_.block);
            ++placed;
        }
    }

    return placed > 0 ? FeatureResult::Placed : FeatureResult::Skipped;
}

}  // namespace finevox::worldgen
