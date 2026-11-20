/**
 * @file generation_passes.cpp
 * @brief Standard generation pass implementations
 *
 * Design: [27-world-generation.md] Section 27.4.4
 */

#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/worldgen/noise_ops.hpp"
#include "finevox/core/world.hpp"
#include "finevox/worldgen/feature.hpp"
#include "finevox/worldgen/feature_ore.hpp"
#include "finevox/worldgen/feature_tree.hpp"
#include "finevox/core/fluid_type_id.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
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
    auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
    for (int32_t lz = 0; lz < 16; ++lz) {
        for (int32_t lx = 0; lx < 16; ++lx) {
            const int32_t ix = lx / across, iz = lz / across;
            const float fx = static_cast<float>(lx % across) / static_cast<float>(across);
            const float fz = static_cast<float>(lz % across) / static_cast<float>(across);
            auto sampleSlice = [&](int32_t iy) {
                float d0 = lerp(at(ix, iy, iz), at(ix + 1, iy, iz), fx);
                float d1 = lerp(at(ix, iy, iz + 1), at(ix + 1, iy, iz + 1), fx);
                return lerp(d0, d1, fz);
            };
            int32_t highest = bottom - 1;
            int32_t curChunkY = std::numeric_limits<int32_t>::min();
            SubChunk* curSub = nullptr;
            float dLow = sampleSlice(0);
            for (int32_t iy = 0; iy < ny - 1; ++iy) {
                float dHigh = sampleSlice(iy + 1);
                int32_t y0 = bottom + iy * up;
                int32_t y1 = std::min(top, y0 + up);
                if (dLow <= 0.0f && dHigh <= 0.0f && (floor.isAir() || y0 - bottom >= g.floorDepth())) {
                    dLow = dHigh;
                    continue;
                }
                for (int32_t y = y0; y < y1; ++y) {
                    const float fy = static_cast<float>(y - y0) / static_cast<float>(up);
                    float d = lerp(dLow, dHigh, fy);

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
                        int32_t chunkY = ChunkColumn::worldYToChunkY(y);
                        if (chunkY != curChunkY) {
                            curSub = &ctx.column.getOrCreateSubChunk(chunkY);
                            curChunkY = chunkY;
                        }
                        curSub->setBlock(lx, ChunkColumn::worldYToLocalY(y), lz, block);
                        highest = y;
                    }
                }
                dLow = dHigh;
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
            if (seaLevel_ && surfaceY < *seaLevel_) {
                surfaceBlock = fillerBlock = BlockTypeId::fromName(props->underwaterBlock);
            } else if (seaLevel_ && surfaceY < *seaLevel_ + shoreHeight_) {
                const std::string& shore = props->shoreBlock.empty() ? props->underwaterBlock : props->shoreBlock;
                surfaceBlock = fillerBlock = BlockTypeId::fromName(shore);
            }
            if (waterways_) {
                const WaterwaySettings& w = waterways_->settings();
                const float wx = static_cast<float>(ctx.pos.x * 16 + lx), wz = static_cast<float>(ctx.pos.z * 16 + lz);
                if (!w.marshBlock.empty() && waterways_->marshAt(wx, wz)) {
                    surfaceBlock = BlockTypeId::fromName(w.marshBlock);
                } else if (!w.fordBlock.empty() && surfaceY < *seaLevel_ && waterways_->fordAt(wx, wz)) {
                    surfaceBlock = BlockTypeId::fromName(w.fordBlock);
                }
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
// WaterwayPass
// ============================================================================

namespace {

struct ContourSample {
    float value;
    float gradient;  ///< How fast value changes per block, at least a little above zero
};

/// A noise's value and the length of its gradient, by central differences. value / gradient
/// is about how many blocks it is to where the noise crosses zero.
ContourSample sampleContour(const Noise2D& noise, float x, float z) {
    float v = noise.evaluate(x, z);
    float gx = (noise.evaluate(x + 1.0f, z) - noise.evaluate(x - 1.0f, z)) * 0.5f;
    float gz = (noise.evaluate(x, z + 1.0f) - noise.evaluate(x, z - 1.0f)) * 0.5f;
    return {v, std::max(std::sqrt(gx * gx + gz * gz), 1e-5f)};
}

}  // namespace

Waterways::Waterways(uint64_t worldSeed, int32_t seaLevel, WaterwaySettings settings)
    : seaLevel_(seaLevel), settings_(std::move(settings)) {
    // Few octaves: the finer ones would make the gradient, and so a river's width, jitter
    if (settings_.riverScale > 0.0f) {
        river_ = NoiseFactory::simplexFBM(NoiseHash::deriveSeed(worldSeed, 700), 2, settings_.riverScale);
        marsh_ = NoiseFactory::simplexFBM(NoiseHash::deriveSeed(worldSeed, 710), 2, settings_.riverScale * 4.0f);
        tussocks_ = NoiseFactory::simplexFBM(NoiseHash::deriveSeed(worldSeed, 715), 2, 0.08f);
    }
    if (settings_.lakeScale > 0.0f) {
        lake_ = NoiseFactory::simplexFBM(NoiseHash::deriveSeed(worldSeed, 720), 3, settings_.lakeScale);
    }
}

Waterways::River Waterways::riverAt(float x, float z) const {
    ContourSample r = sampleContour(*river_, x, z);
    float stretch = marsh_->evaluate(x, z);
    float marsh = std::clamp((settings_.riverMarshes - stretch) / 0.1f, 0.0f, 1.0f);
    float half = std::max(settings_.riverWidth * 0.5f, 0.5f) * (1.0f + (std::max(settings_.marshWidth, 1.0f) - 1.0f) * marsh);
    return {std::fabs(r.value) / r.gradient, half, marsh, stretch > settings_.riverFords};
}

bool Waterways::fordAt(float x, float z) const {
    if (!river_) return false;
    River r = riverAt(x, z);
    return r.fromMiddle < r.half && r.ford;
}

bool Waterways::marshAt(float x, float z) const {
    if (!river_) return false;
    River r = riverAt(x, z);
    return r.fromMiddle < r.half && r.marsh >= 0.5f;
}

std::optional<int32_t> Waterways::ceilingAt(float x, float z) const {
    std::optional<int32_t> ceiling;
    auto lower = [&](float h) {
        int32_t y = static_cast<int32_t>(std::floor(std::min(h, 4096.0f)));
        if (!ceiling || y < *ceiling) ceiling = y;
    };
    const float sea = static_cast<float>(seaLevel_);
    const float slope = std::max(settings_.bankSlope, 0.01f);

    if (river_) {
        River r = riverAt(x, z);
        if (r.fromMiddle >= r.half) {
            lower(sea + (r.fromMiddle - r.half) * slope);
        } else if (r.marsh >= 0.5f) {
            // A block deep, so it can be waded, with tussocks standing out of it
            lower(tussocks_->evaluate(x, z) > 0.35f ? sea : sea - 1.0f);
        } else if (r.ford) {
            lower(sea - 1.0f);
        } else {
            float across = r.fromMiddle / r.half;
            float depth = static_cast<float>(std::max(settings_.riverDepth, 1));
            lower(sea - std::max(1.0f, std::ceil(depth * (1.0f - across * across))));
        }
    }
    if (lake_) {
        ContourSample l = sampleContour(*lake_, x, z);
        float past = (l.value - settings_.lakeThreshold) / l.gradient;  // blocks inside the shore
        if (past > 0.0f) {
            float depth = std::min(1.0f + std::floor(past * 0.25f), static_cast<float>(std::max(settings_.lakeDepth, 1)));
            lower(sea - depth);
        } else {
            lower(sea - past * slope);
        }
    }
    return ceiling;
}

void WaterwayPass::generate(GenerationContext& ctx) {
    const int32_t baseX = ctx.pos.x * 16, baseZ = ctx.pos.z * 16;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            const int32_t idx = GenerationContext::hmIndex(lx, lz);
            std::optional<int32_t> ceiling =
                waterways_->ceilingAt(static_cast<float>(baseX + lx), static_cast<float>(baseZ + lz));
            if (!ceiling) continue;
            int32_t top = std::max(*ceiling, 1);
            if (top >= ctx.heightmap[idx]) continue;
            for (int32_t y = ctx.heightmap[idx]; y > top; --y) {
                ctx.column.setBlock(lx, y, lz, AIR_BLOCK_TYPE);
            }
            ctx.heightmap[idx] = top;
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
    const int32_t baseX = ctx.pos.x * 16;
    const int32_t baseZ = ctx.pos.z * 16;
    std::queue<BlockCoord> flood;

    auto canFill = [&](ChunkColumn& col, int32_t lx, int32_t y, int32_t lz) {
        BlockTypeId existing = col.getBlock(lx, y, lz);
        if (!existing.isAir()) {
            if (BlockRegistry::global().getType(existing).isOpaque()) return false;
            col.setBlock(lx, y, lz, AIR_BLOCK_TYPE);
        }
        int32_t chunkY = ChunkColumn::worldYToChunkY(y);
        int32_t localY = ChunkColumn::worldYToLocalY(y);
        SubChunk& sc = col.getOrCreateSubChunk(chunkY);
        if (!sc.getFluid(lx, localY, lz).isEmpty()) return false;
        sc.setFluid(lx, localY, lz, waterId, FLUID_SOURCE_LEVEL);
        return true;
    };

    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            // Walk down from sea level through open air to the sea floor, then flood sideways
            // and down under overhangs and coastal arches while leaving sealed caves dry.
            for (int32_t y = seaLevel_; y >= 0; --y) {
                if (!canFill(ctx.column, lx, y, lz)) break;
                flood.emplace(baseX + lx, y, baseZ + lz);
            }
        }
    }

    // Also seed from water already placed along the borders of adjacent columns
    for (int32_t i = 0; i < 16; ++i) {
        const BlockCoord borders[4] = {
            {baseX - 1, 0, baseZ + i},
            {baseX + 16, 0, baseZ + i},
            {baseX + i, 0, baseZ - 1},
            {baseX + i, 0, baseZ + 16},
        };
        for (const BlockCoord& b : borders) {
            ChunkColumn* adj = ctx.world.getColumn(ColumnPos::fromBlock(b));
            if (!adj) continue;
            int32_t alx = b.x & 15, alz = b.z & 15;
            for (int32_t y = seaLevel_; y >= 0; --y) {
                const SubChunk* sc = adj->getSubChunk(ChunkColumn::worldYToChunkY(y));
                if (sc && sc->getFluid(alx, ChunkColumn::worldYToLocalY(y), alz) == waterId) {
                    flood.emplace(b.x, y, b.z);
                }
            }
        }
    }

    static constexpr int32_t kDirs[5][3] = {
        {-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, -1, 0}
    };
    while (!flood.empty()) {
        BlockCoord p = flood.front();
        flood.pop();
        for (const auto& d : kDirs) {
            BlockCoord n(p.x + d[0], p.y + d[1], p.z + d[2]);
            if (n.y < 0 || n.y > seaLevel_) continue;
            if (std::abs(n.x - (baseX + 8)) > 32 || std::abs(n.z - (baseZ + 8)) > 32) continue;
            ColumnPos cpos = ColumnPos::fromBlock(n);
            ChunkColumn* col = (cpos == ctx.pos) ? &ctx.column : ctx.world.getColumn(cpos);
            if (!col) continue;
            if (canFill(*col, n.x & 15, n.y, n.z & 15)) {
                flood.push(n);
            }
        }
    }
}

}  // namespace finevox::worldgen
