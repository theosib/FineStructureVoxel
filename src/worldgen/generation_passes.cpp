/**
 * @file generation_passes.cpp
 * @brief Standard generation pass implementations
 *
 * Design: [27-world-generation.md] Section 27.4.4
 */

#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/worldgen/noise_ops.hpp"
#include "finevox/core/world.hpp"
#include "finevox/worldgen/feature.hpp"
#include "finevox/worldgen/feature_ore.hpp"
#include "finevox/worldgen/feature_tree.hpp"
#include "finevox/core/fluid_type_id.hpp"

#include <cmath>
#include <limits>
#include "finevox/worldgen/noise.hpp"

namespace finevox::worldgen {

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
// DensityTerrainPass
// ============================================================================

DensityTerrainPass::DensityTerrainPass(std::shared_ptr<const DensityGraph> graph)
    : graph_(std::move(graph)) {
    const auto& registry = BiomeRegistry::global();
    for (BiomeId id : registry.allowedBiomes()) {
        const BiomeProperties* props = registry.getBiome(id);
        if (!props || props->ranges.empty()) continue;
        Candidate c;
        c.id = id;
        for (const auto& [field, range] : props->ranges) {
            for (const auto& [name, node] : graph_->fields()) {
                if (name == field) c.ranges.emplace_back(node, range);
            }
        }
        candidates_.push_back(std::move(c));
    }
}

BiomeId DensityTerrainPass::chooseBiome(const std::vector<float>& values) const {
    BiomeId best{};
    float bestOutside = std::numeric_limits<float>::max();
    float bestCentre = std::numeric_limits<float>::max();
    for (const auto& c : candidates_) {
        // How far outside its ranges, then how far from their middles: inside all of them
        // beats near, and of those inside, the one this is most typical of
        float outside = 0.0f, centre = 0.0f;
        for (const auto& [node, range] : c.ranges) {
            float v = values[static_cast<size_t>(node)];
            float lo = range.first, hi = range.second;
            if (v < lo) outside += lo - v;
            if (v > hi) outside += v - hi;
            float half = std::max((hi - lo) * 0.5f, 1e-6f);
            centre += std::fabs(v - (lo + hi) * 0.5f) / half;
        }
        if (outside < bestOutside || (outside == bestOutside && centre < bestCentre)) {
            best = c.id;
            bestOutside = outside;
            bestCentre = centre;
        }
    }
    return best;
}

void DensityTerrainPass::generate(GenerationContext& ctx) {
    const DensityGraph& g = *graph_;
    const BlockTypeId fill = BlockTypeId::fromName(g.fillBlock());
    const BlockTypeId floor = g.floorBlock().empty() ? BlockTypeId() : BlockTypeId::fromName(g.floorBlock());
    const int32_t across = g.cellAcross(), up = g.cellUp();
    const int32_t bottom = g.bottom(), top = g.top();
    const int32_t nx = 16 / across + 1;
    const int32_t ny = (top - bottom + up - 1) / up + 1;
    const int32_t baseX = ctx.pos.x * 16, baseZ = ctx.pos.z * 16;
    const size_t terrain = static_cast<size_t>(g.terrainNode());
    std::vector<std::pair<BlockTypeId, const DensityGraph::Layer*>> layers;
    for (const auto& layer : g.layers()) layers.emplace_back(BlockTypeId::fromName(layer.block), &layer);

    // Density at the corners of the cells, blended within them
    std::vector<float> corners(static_cast<size_t>(nx * nx * ny));
    std::vector<float> values;
    auto at = [&](int32_t ix, int32_t iy, int32_t iz) -> float& {
        return corners[static_cast<size_t>((iy * nx + iz) * nx + ix)];
    };
    for (int32_t iz = 0; iz < nx; ++iz) {
        for (int32_t ix = 0; ix < nx; ++ix) {
            float wx = static_cast<float>(baseX + ix * across), wz = static_cast<float>(baseZ + iz * across);
            g.evaluateColumn(wx, wz, values);
            for (int32_t iy = 0; iy < ny; ++iy) {
                g.evaluateHeight(wx, static_cast<float>(bottom + iy * up), wz, values);
                at(ix, iy, iz) = values[terrain];
            }
        }
    }

    const bool byFields = !candidates_.empty();
    for (int32_t lz = 0; lz < 16; ++lz) {
        for (int32_t lx = 0; lx < 16; ++lx) {
            const int32_t ix = lx / across, iz = lz / across;
            const float fx = static_cast<float>(lx % across) / static_cast<float>(across);
            const float fz = static_cast<float>(lz % across) / static_cast<float>(across);
            int32_t highest = bottom - 1;
            for (int32_t y = bottom; y < top; ++y) {
                const int32_t iy = (y - bottom) / up;
                const float fy = static_cast<float>((y - bottom) % up) / static_cast<float>(up);
                auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
                float d00 = lerp(at(ix, iy, iz), at(ix + 1, iy, iz), fx);
                float d10 = lerp(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), fx);
                float d01 = lerp(at(ix, iy + 1, iz), at(ix + 1, iy + 1, iz), fx);
                float d11 = lerp(at(ix, iy + 1, iz + 1), at(ix + 1, iy + 1, iz + 1), fx);
                float d = lerp(lerp(d00, d10, fz), lerp(d01, d11, fz), fy);

                BlockTypeId block = d > 0.0f ? fill : BlockTypeId();
                if (!block.isAir()) {
                    for (const auto& [layerBlock, layer] : layers) {
                        if (y >= layer->below + layer->blend) continue;
                        bool in = y < layer->below;
                        if (!in) {
                            // Thinning upward through the blend: certain at the bottom of it
                            uint32_t h = NoiseHash::hash3D(baseX + lx, y, baseZ + lz, ctx.worldSeed ^ 0x51u);
                            in = static_cast<int32_t>(h % static_cast<uint32_t>(layer->blend)) >= y - layer->below;
                        }
                        if (in) {
                            block = layerBlock;
                            break;
                        }
                    }
                }
                // A floor, solid at the bottom and ragged above it
                if (!floor.isAir() && y - bottom < g.floorDepth()) {
                    uint32_t h = NoiseHash::hash3D(baseX + lx, y, baseZ + lz, ctx.worldSeed);
                    if (static_cast<int32_t>(h % static_cast<uint32_t>(g.floorDepth())) >= y - bottom) block = floor;
                }
                if (!block.isAir()) {
                    ctx.column.setBlock(lx, y, lz, block);
                    highest = y;
                }
            }
            const int32_t idx = GenerationContext::hmIndex(lx, lz);
            ctx.heightmap[idx] = highest;

            const float wx = static_cast<float>(baseX + lx), wz = static_cast<float>(baseZ + lz);
            if (byFields) {
                g.evaluateColumn(wx, wz, values);
                ctx.biomes[idx] = chooseBiome(values);
            } else {
                ctx.biomes[idx] = ctx.biomeMap.getBiome(wx, wz);
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
            if (seaLevel_ && surfaceY < *seaLevel_ - 1) {
                surfaceBlock = fillerBlock = BlockTypeId::fromName(props->underwaterBlock);
            }
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

        // density per 16x16 area; what's left over is the chance of one more, so a rare ore
        // can average less than a vein a column
        float expected = placement.density * 256.0f * densityMul;
        int32_t veinCount = static_cast<int32_t>(expected);
        float leftover = expected - static_cast<float>(veinCount);
        if (leftover > 0.0f && static_cast<float>(NoiseHash::hash2D(ctx.pos.x, ctx.pos.z, static_cast<uint32_t>(rng)) % 10000) <
                                   leftover * 10000.0f) {
            ++veinCount;
        }

        for (int32_t v = 0; v < veinCount; ++v) {
            // Advance RNG
            rng += 0x9e3779b97f4a7c15ULL;
            uint64_t z = rng;
            z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
            z = z ^ (z >> 31);

            int32_t lx = static_cast<int32_t>(z % 16);
            int32_t lz = static_cast<int32_t>((z >> 8) % 16);
            const uint64_t span = static_cast<uint64_t>(placement.maxHeight - placement.minHeight + 1);
            int32_t ly = placement.minHeight + static_cast<int32_t>((z >> 16) % span);
            if (placement.peaked) {
                // The sum of two even draws over half the range: likeliest in the middle
                uint64_t half = span / 2 + 1;
                ly = placement.minHeight + static_cast<int32_t>(((z >> 16) % half) + ((z >> 40) % half));
            }

            FeaturePlacementContext fctx{
                ctx.world,
                BlockCoord(worldX + lx, ly, worldZ + lz),
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

                if (!placement.requiredSurface.isAir() &&
                    ctx.world.getBlock(BlockCoord(worldX + lx, surfaceY, worldZ + lz)) !=
                        placement.requiredSurface) {
                    continue;
                }

                FeaturePlacementContext fctx{
                    ctx.world,
                    BlockCoord(worldX + lx, surfaceY + 1, worldZ + lz),
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

// ============================================================================
// FluidPass — fills water/lava in terrain gaps
// ============================================================================

FluidPass::FluidPass(int32_t seaLevel) : seaLevel_(seaLevel) {}

void FluidPass::generate(GenerationContext& ctx) {
    FluidTypeId waterId = FluidTypeId::fromName("water");
    if (waterId.isEmpty()) return;  // No water type registered

    const int32_t FLUID_SOURCE_LEVEL = 15;

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            int32_t idx = GenerationContext::hmIndex(lx, lz);
            int32_t surfaceY = ctx.heightmap[idx];

            // Fill water from surface+1 up to sea level (inclusive)
            if (surfaceY < seaLevel_) {
                for (int32_t y = surfaceY + 1; y <= seaLevel_; ++y) {
                    // Only place water in air blocks (caves may exist)
                    BlockTypeId existing = ctx.column.getBlock(lx, y, lz);
                    if (existing.isAir()) {
                        int32_t chunkY = ChunkColumn::worldYToChunkY(y);
                        int32_t localY = ChunkColumn::worldYToLocalY(y);
                        SubChunk& sc = ctx.column.getOrCreateSubChunk(chunkY);
                        sc.setFluid(lx, localY, lz, waterId, FLUID_SOURCE_LEVEL);
                    }
                }
            }
        }
    }
}

}  // namespace finevox::worldgen
