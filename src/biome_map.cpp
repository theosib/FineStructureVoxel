/**
 * @file biome_map.cpp
 * @brief Spatial biome assignment using Voronoi + climate noise
 *
 * Design: [27-world-generation.md] Section 27.3.4
 */

#include "finevox/biome_map.hpp"
#include "finevox/noise.hpp"
#include "finevox/noise_ops.hpp"

namespace finevox {

BiomeMap::BiomeMap(uint64_t worldSeed, const BiomeRegistry& registry, float cellSize)
    : registry_(registry),
      voronoi_(worldSeed, cellSize) {
    // Temperature noise: very low frequency, covers large regions
    temperatureNoise_ = NoiseFactory::simplexFBM(
        NoiseHash::deriveSeed(worldSeed, 1000), 4, 0.0005f);

    // Humidity noise: different seed, similar frequency
    humidityNoise_ = NoiseFactory::simplexFBM(
        NoiseHash::deriveSeed(worldSeed, 2000), 4, 0.0006f);
}

std::pair<float, float> BiomeMap::cellClimate(float cellCenterX, float cellCenterZ) const {
    // Evaluate climate noise at the cell center
    float temp = temperatureNoise_->evaluate(cellCenterX, cellCenterZ);
    float hum = humidityNoise_->evaluate(cellCenterX, cellCenterZ);

    // Map from [-1, 1] to [0, 1]
    temp = temp * 0.5f + 0.5f;
    hum = hum * 0.5f + 0.5f;

    return {temp, hum};
}

BiomeId BiomeMap::getBiome(float x, float z) const {
    auto result = voronoi_.evaluate(x, z);
    auto [temp, hum] = cellClimate(result.cellCenter.x, result.cellCenter.y);
    return registry_.selectBiome(temp, hum);
}

BiomeBlend BiomeMap::getBlendedBiome(float x, float z) const {
    auto result = voronoi_.evaluate(x, z);

    // Primary biome from nearest cell
    auto [temp1, hum1] = cellClimate(result.cellCenter.x, result.cellCenter.y);
    BiomeId primary = registry_.selectBiome(temp1, hum1);

    // Blend weight from F2-F1 relative to cell size
    // Small F2-F1 = near border = more blending
    float edgeDistance = result.distance2 - result.distance1;
    float blendZone = voronoi_.cellSize() * 0.1f;  // Blend within 10% of cell size
    float blendWeight = 1.0f - std::min(edgeDistance / blendZone, 1.0f);

    // For secondary biome, we'd need the second cell's center
    // Approximation: use slightly different climate for secondary
    // (This is acceptable since exact second cell center isn't stored in VoronoiResult)
    BiomeId secondary = primary;
    if (blendWeight > 0.0f) {
        // Perturb climate slightly toward a different biome
        float temp2 = temp1 + 0.1f;
        float hum2 = hum1 + 0.1f;
        if (temp2 > 1.0f) temp2 -= 0.2f;
        if (hum2 > 1.0f) hum2 -= 0.2f;
        secondary = registry_.selectBiome(temp2, hum2);
        if (secondary == primary) {
            blendWeight = 0.0f;
        }
    }

    return {primary, secondary, blendWeight};
}

float BiomeMap::getTemperature(float x, float z) const {
    float v = temperatureNoise_->evaluate(x, z);
    return v * 0.5f + 0.5f;
}

float BiomeMap::getHumidity(float x, float z) const {
    float v = humidityNoise_->evaluate(x, z);
    return v * 0.5f + 0.5f;
}

std::pair<float, float> BiomeMap::getTerrainParams(float x, float z) const {
    auto blend = getBlendedBiome(x, z);

    const BiomeProperties* primaryProps = registry_.getBiome(blend.primary);
    if (!primaryProps) {
        return {64.0f, 16.0f};  // Fallback defaults
    }

    if (blend.blendWeight <= 0.0f || blend.primary == blend.secondary) {
        return {primaryProps->baseHeight, primaryProps->heightVariation};
    }

    const BiomeProperties* secondaryProps = registry_.getBiome(blend.secondary);
    if (!secondaryProps) {
        return {primaryProps->baseHeight, primaryProps->heightVariation};
    }

    float w = blend.blendWeight;
    float baseHeight = primaryProps->baseHeight * (1.0f - w) + secondaryProps->baseHeight * w;
    float heightVar = primaryProps->heightVariation * (1.0f - w) + secondaryProps->heightVariation * w;

    return {baseHeight, heightVar};
}

}  // namespace finevox
