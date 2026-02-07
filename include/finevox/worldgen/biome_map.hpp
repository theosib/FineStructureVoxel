/**
 * @file biome_map.hpp
 * @brief Spatial biome assignment using Voronoi + climate noise
 *
 * Design: [27-world-generation.md] Section 27.3.4
 *
 * BiomeMap combines Voronoi tessellation with climate noise to assign
 * biomes to world positions. Supports blended queries for smooth
 * transitions at biome borders.
 */

#pragma once

#include "finevox/worldgen/biome.hpp"
#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/noise_voronoi.hpp"

#include <cstdint>
#include <memory>

namespace finevox::worldgen {

/// Biome query result with blending weights
struct BiomeBlend {
    BiomeId primary;            ///< Dominant biome
    BiomeId secondary;          ///< Second-nearest biome (for blending)
    float blendWeight = 0.0f;   ///< 0.0 = all primary, 1.0 = all secondary
};

/// Spatial biome assignment from Voronoi cells + climate noise
class BiomeMap {
public:
    /// @param worldSeed Deterministic seed for all noise
    /// @param registry Biome registry to select from (must outlive BiomeMap)
    /// @param cellSize Voronoi cell size in blocks (default 256)
    BiomeMap(uint64_t worldSeed, const BiomeRegistry& registry,
             float cellSize = 256.0f);

    /// Get the primary biome at world position (x, z)
    [[nodiscard]] BiomeId getBiome(float x, float z) const;

    /// Get biome with blending information for smooth transitions
    [[nodiscard]] BiomeBlend getBlendedBiome(float x, float z) const;

    /// Get temperature at world position
    [[nodiscard]] float getTemperature(float x, float z) const;

    /// Get humidity at world position
    [[nodiscard]] float getHumidity(float x, float z) const;

    /// Get blended terrain height parameters at position
    /// Returns (baseHeight, heightVariation) blended by nearby biomes
    [[nodiscard]] std::pair<float, float> getTerrainParams(float x, float z) const;

private:
    const BiomeRegistry& registry_;
    VoronoiNoise2D voronoi_;
    std::unique_ptr<Noise2D> temperatureNoise_;
    std::unique_ptr<Noise2D> humidityNoise_;

    /// Get climate values at a Voronoi cell center
    [[nodiscard]] std::pair<float, float> cellClimate(
        float cellCenterX, float cellCenterZ) const;
};

}  // namespace finevox::worldgen
