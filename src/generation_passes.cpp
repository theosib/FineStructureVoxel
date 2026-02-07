/**
 * @file generation_passes.cpp
 * @brief Standard generation pass implementations
 *
 * Design: [27-world-generation.md] Section 27.4.4
 */

#include "finevox/generation_passes.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/noise_ops.hpp"
#include "finevox/world.hpp"
#include "finevox/feature.hpp"
#include "finevox/feature_ore.hpp"
#include "finevox/feature_tree.hpp"

#include <cmath>

namespace finevox {

// ============================================================================
// TerrainPass
// ============================================================================

TerrainPass::TerrainPass(uint64_t worldSeed) {
    // Continental shape: low-frequency noise
    continentNoise_ = NoiseFactory::simplexFBM(
        NoiseHash::deriveSeed(worldSeed, 100), 6, 0.002f);

    // Detail noise: higher frequency
    detailNoise_ = NoiseFactory::simplexFBM(
        NoiseHash::deriveSeed(worldSeed, 200), 4, 0.01f);

    // 3D density for overhangs (optional enrichment)
    densityNoise_ = NoiseFactory::simplexFBM3D(
        NoiseHash::deriveSeed(worldSeed, 300), 4, 0.02f);
}

void TerrainPass::generate(GenerationContext& ctx) {
    BlockTypeId stoneId = BlockTypeId::fromName("stone");
    int32_t worldX = ctx.pos.x * 16;
    int32_t worldZ = ctx.pos.z * 16;

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            float wx = static_cast<float>(worldX + lx);
            float wz = static_cast<float>(worldZ + lz);

            // Get biome-blended terrain parameters
            auto [baseHeight, heightVar] = ctx.biomeMap.getTerrainParams(wx, wz);

            // Sample noise for height
            float continent = continentNoise_->evaluate(wx, wz);
            float detail = detailNoise_->evaluate(wx, wz);

            int32_t surfaceY = static_cast<int32_t>(
                baseHeight + continent * heightVar + detail * 4.0f);

            // Clamp to reasonable range
            if (surfaceY < 1) surfaceY = 1;
            if (surfaceY > 255) surfaceY = 255;

            int32_t idx = GenerationContext::hmIndex(lx, lz);
            ctx.heightmap[idx] = surfaceY;
            ctx.biomes[idx] = ctx.biomeMap.getBiome(wx, wz);

            // Fill stone from y=0 up to surface
            for (int32_t y = 0; y <= surfaceY; ++y) {
                ctx.column.setBlock(lx, y, lz, stoneId);
            }
        }
    }
}

// ============================================================================
// SurfacePass
// ============================================================================

void SurfacePass::generate(GenerationContext& ctx) {
    const BiomeRegistry& registry = BiomeRegistry::global();

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            int32_t idx = GenerationContext::hmIndex(lx, lz);
            int32_t surfaceY = ctx.heightmap[idx];
            BiomeId biome = ctx.biomes[idx];

            const BiomeProperties* props = registry.getBiome(biome);
            if (!props) continue;

            BlockTypeId surfaceBlock = BlockTypeId::fromName(props->surfaceBlock);
            BlockTypeId fillerBlock = BlockTypeId::fromName(props->fillerBlock);
            BlockTypeId stoneBlock = BlockTypeId::fromName(props->stoneBlock);

            // Surface block at top
            ctx.column.setBlock(lx, surfaceY, lz, surfaceBlock);

            // Filler layers below surface
            for (int32_t d = 1; d <= props->fillerDepth && surfaceY - d >= 0; ++d) {
                ctx.column.setBlock(lx, surfaceY - d, lz, fillerBlock);
            }

            // Stone layer below filler (if different from default stone)
            if (stoneBlock != BlockTypeId::fromName("stone")) {
                for (int32_t y = 0; y < surfaceY - props->fillerDepth; ++y) {
                    ctx.column.setBlock(lx, y, lz, stoneBlock);
                }
            }
        }
    }
}

// ============================================================================
// CavePass
// ============================================================================

CavePass::CavePass(uint64_t worldSeed) {
    // Cheese caves: large, blobby open areas
    cheeseNoise_ = NoiseFactory::simplexFBM3D(
        NoiseHash::deriveSeed(worldSeed, 400), 3, 0.015f);

    // Spaghetti caves: winding tunnels
    spaghettiNoise_ = NoiseFactory::simplexFBM3D(
        NoiseHash::deriveSeed(worldSeed, 500), 3, 0.025f);
}

void CavePass::generate(GenerationContext& ctx) {
    int32_t worldX = ctx.pos.x * 16;
    int32_t worldZ = ctx.pos.z * 16;

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            int32_t idx = GenerationContext::hmIndex(lx, lz);
            int32_t surfaceY = ctx.heightmap[idx];

            float wx = static_cast<float>(worldX + lx);
            float wz = static_cast<float>(worldZ + lz);

            // Don't carve above surface - 2
            int32_t maxCarveY = surfaceY - 2;
            if (maxCarveY < 1) continue;

            for (int32_t y = 1; y < maxCarveY; ++y) {
                float wy = static_cast<float>(y);

                // Cheese caves: open when noise > threshold
                float cheese = cheeseNoise_->evaluate(wx, wy, wz);
                if (cheese > 0.5f) {
                    ctx.column.setBlock(lx, y, lz, AIR_BLOCK_TYPE);
                    continue;
                }

                // Spaghetti caves: open when |noise| is near zero
                float spaghetti = spaghettiNoise_->evaluate(wx, wy, wz);
                if (std::abs(spaghetti) < 0.08f) {
                    ctx.column.setBlock(lx, y, lz, AIR_BLOCK_TYPE);
                }
            }

            // Update heightmap if caves opened the surface
            // Walk down from current surface to find new highest solid
            int32_t newSurface = surfaceY;
            while (newSurface > 0 &&
                   ctx.column.getBlock(lx, newSurface, lz).isAir()) {
                --newSurface;
            }
            ctx.heightmap[idx] = newSurface;
        }
    }
}

// ============================================================================
// OrePass
// ============================================================================

void OrePass::generate(GenerationContext& ctx) {
    auto& featureReg = FeatureRegistry::global();
    auto allPlacements = featureReg.allPlacements();

    int32_t worldX = ctx.pos.x * 16;
    int32_t worldZ = ctx.pos.z * 16;

    // Derive per-column ore seed
    uint64_t oreSeed = NoiseHash::deriveSeed(ctx.worldSeed,
        static_cast<uint64_t>(ctx.pos.x) * 341873128712ULL +
        static_cast<uint64_t>(ctx.pos.z) * 132897987541ULL + 4000);

    for (const auto& placement : allPlacements) {
        Feature* feature = featureReg.getFeature(placement.featureName);
        if (!feature) continue;

        // Only process ore features (check if it has ore-like config)
        auto* oreFeature = dynamic_cast<OreFeature*>(feature);
        if (!oreFeature) continue;

        // Check biome filter
        if (!placement.biomes.empty()) {
            // Use center column biome for simplicity
            BiomeId centerBiome = ctx.biomes[GenerationContext::hmIndex(8, 8)];
            bool matched = false;
            for (const auto& b : placement.biomes) {
                if (b == centerBiome) { matched = true; break; }
            }
            if (!matched) continue;
        }

        // Determine how many veins for this chunk
        // Use the OreConfig's veinsPerChunk as base, modulated by biome ore density
        BiomeId centerBiome = ctx.biomes[GenerationContext::hmIndex(8, 8)];
        const BiomeProperties* biomeProps = BiomeRegistry::global().getBiome(centerBiome);
        float densityMul = biomeProps ? biomeProps->oreDensity : 1.0f;

        // SplitMix64-style RNG for vein count and positions
        uint64_t rng = NoiseHash::deriveSeed(oreSeed,
            NoiseHash::hash2D(oreSeed,
                static_cast<int32_t>(placement.featureName.size()),
                static_cast<int32_t>(placement.featureName[0])));

        int32_t veinCount = static_cast<int32_t>(
            placement.density * 256.0f * densityMul);  // density per 16x16 area
        if (veinCount < 1) veinCount = 1;

        for (int32_t v = 0; v < veinCount; ++v) {
            // Advance RNG
            rng += 0x9e3779b97f4a7c15ULL;
            uint64_t z = rng;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            z = z ^ (z >> 31);

            int32_t lx = static_cast<int32_t>(z % 16);
            int32_t lz = static_cast<int32_t>((z >> 8) % 16);
            int32_t ly = placement.minHeight +
                static_cast<int32_t>((z >> 16) %
                    static_cast<uint64_t>(placement.maxHeight - placement.minHeight + 1));

            FeaturePlacementContext fctx{
                ctx.world,
                BlockPos(worldX + lx, ly, worldZ + lz),
                centerBiome,
                z,  // per-vein seed
                &ctx
            };

            (void)feature->place(fctx);
        }
    }
}

// ============================================================================
// StructurePass
// ============================================================================

void StructurePass::generate(GenerationContext& ctx) {
    auto& featureReg = FeatureRegistry::global();
    auto allPlacements = featureReg.allPlacements();

    int32_t worldX = ctx.pos.x * 16;
    int32_t worldZ = ctx.pos.z * 16;

    uint64_t structSeed = NoiseHash::deriveSeed(ctx.worldSeed,
        static_cast<uint64_t>(ctx.pos.x) * 341873128712ULL +
        static_cast<uint64_t>(ctx.pos.z) * 132897987541ULL + 5000);

    for (const auto& placement : allPlacements) {
        Feature* feature = featureReg.getFeature(placement.featureName);
        if (!feature) continue;

        // Skip ore features (handled by OrePass)
        if (dynamic_cast<OreFeature*>(feature)) continue;

        if (!placement.requiresSurface) continue;

        // Iterate surface positions with density-based probability
        uint64_t rng = NoiseHash::deriveSeed(structSeed,
            NoiseHash::hash2D(structSeed,
                static_cast<int32_t>(placement.featureName.size()),
                static_cast<int32_t>(placement.featureName[0])));

        for (int32_t lx = 0; lx < 16; ++lx) {
            for (int32_t lz = 0; lz < 16; ++lz) {
                // Advance RNG per position
                rng += 0x9e3779b97f4a7c15ULL;
                uint64_t z = rng;
                z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
                z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
                z = z ^ (z >> 31);

                // Density check
                float roll = static_cast<float>(z & 0xFFFF) / 65536.0f;
                if (roll >= placement.density) continue;

                // Check biome filter
                int32_t idx = GenerationContext::hmIndex(lx, lz);
                BiomeId biome = ctx.biomes[idx];
                if (!placement.biomes.empty()) {
                    bool matched = false;
                    for (const auto& b : placement.biomes) {
                        if (b == biome) { matched = true; break; }
                    }
                    if (!matched) continue;
                }

                int32_t surfaceY = ctx.heightmap[idx];

                // Height range check
                if (surfaceY < placement.minHeight || surfaceY > placement.maxHeight) continue;

                FeaturePlacementContext fctx{
                    ctx.world,
                    BlockPos(worldX + lx, surfaceY + 1, worldZ + lz),
                    biome,
                    z,
                    &ctx
                };

                (void)feature->place(fctx);
            }
        }
    }
}

// ============================================================================
// DecorationPass
// ============================================================================

void DecorationPass::generate(GenerationContext& /*ctx*/) {
    // Decoration pass: place single-block decorations on surface.
    // For now this is a stub — decoration features are registered like any
    // other feature and handled by StructurePass. Games add decoration
    // features (flowers, tall grass) as Feature implementations with
    // appropriate placement rules. This pass exists as an extension point
    // at a lower priority than structures.

    // A real implementation could use a separate FeaturePlacement category
    // or decoration-specific logic here.
}

}  // namespace finevox
