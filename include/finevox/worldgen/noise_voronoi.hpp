/**
 * @file noise_voronoi.hpp
 * @brief Voronoi/Worley cell-based noise for biome regions
 *
 * Design: [27-world-generation.md] Section 27.2.6
 */

#pragma once

#include <cstdint>
#include <glm/glm.hpp>

namespace finevox::worldgen {

/// Result from Voronoi noise evaluation
struct VoronoiResult {
    float distance1 = 0.0f;       ///< Distance to nearest cell center
    float distance2 = 0.0f;       ///< Distance to second-nearest cell center
    glm::vec2 cellCenter{0.0f};   ///< Position of nearest cell center
    uint32_t cellId = 0;          ///< Deterministic ID for nearest cell
};

/// Voronoi (Worley) cell noise in 2D
///
/// Divides the plane into irregular cells. Each evaluation returns
/// distances to nearest cell centers and a deterministic cell ID.
/// Used for biome region generation.
class VoronoiNoise2D {
public:
    /// @param seed Deterministic seed
    /// @param cellSize Approximate distance between cell centers (in world units)
    explicit VoronoiNoise2D(uint64_t seed, float cellSize = 256.0f);

    /// Full evaluation: distances, cell center, and cell ID
    [[nodiscard]] VoronoiResult evaluate(float x, float z) const;

    /// Distance to nearest cell center only (F1)
    [[nodiscard]] float evaluateF1(float x, float z) const;

    /// Edge detection: F2 - F1 (small at cell borders, large at centers)
    [[nodiscard]] float evaluateF2MinusF1(float x, float z) const;

    /// Cell size getter
    [[nodiscard]] float cellSize() const { return cellSize_; }

private:
    uint64_t seed_;
    float cellSize_;
    float invCellSize_;

    /// Get the jittered cell center for grid cell (ix, iz)
    [[nodiscard]] glm::vec2 cellPoint(int32_t ix, int32_t iz) const;

    /// Get deterministic ID for grid cell
    [[nodiscard]] uint32_t cellHash(int32_t ix, int32_t iz) const;
};

}  // namespace finevox::worldgen
