/**
 * @file noise_voronoi.cpp
 * @brief Voronoi (Worley) cell noise implementation
 *
 * Design: [27-world-generation.md] Section 27.2.6
 *
 * Produces cell-based noise where each point belongs to the nearest
 * randomly-placed cell center. Used for biome region boundaries.
 */

#include "finevox/worldgen/noise_voronoi.hpp"
#include "finevox/worldgen/noise.hpp"

#include <cmath>
#include <limits>

namespace finevox::worldgen {

VoronoiNoise2D::VoronoiNoise2D(uint64_t seed, float cellSize)
    : seed_(seed), cellSize_(cellSize), invCellSize_(1.0f / cellSize) {
}

glm::vec2 VoronoiNoise2D::cellPoint(int32_t ix, int32_t iz) const {
    // Hash the grid cell to get a deterministic jitter
    uint32_t h = NoiseHash::hash2D(ix, iz, seed_);

    // Convert hash to [0, 1) range for jitter
    float jx = static_cast<float>(h & 0xFFFF) / 65536.0f;
    float jz = static_cast<float>((h >> 16) & 0xFFFF) / 65536.0f;

    // Cell center = grid origin + jitter within cell
    float cx = (static_cast<float>(ix) + jx) * cellSize_;
    float cz = (static_cast<float>(iz) + jz) * cellSize_;

    return {cx, cz};
}

uint32_t VoronoiNoise2D::cellHash(int32_t ix, int32_t iz) const {
    return NoiseHash::hash2D(ix, iz, NoiseHash::deriveSeed(seed_, 0xCAFE));
}

VoronoiResult VoronoiNoise2D::evaluate(float x, float z) const {
    // Find which grid cell we're in
    int32_t cellX = static_cast<int32_t>(std::floor(x * invCellSize_));
    int32_t cellZ = static_cast<int32_t>(std::floor(z * invCellSize_));

    float dist1 = std::numeric_limits<float>::max();
    float dist2 = std::numeric_limits<float>::max();
    glm::vec2 closestCenter{0.0f};
    int32_t closestCellX = 0;
    int32_t closestCellZ = 0;

    // Check 3x3 neighborhood of grid cells
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            int32_t nx = cellX + dx;
            int32_t nz = cellZ + dz;

            glm::vec2 center = cellPoint(nx, nz);
            float ddx = x - center.x;
            float ddz = z - center.y;
            float dist = ddx * ddx + ddz * ddz;

            if (dist < dist1) {
                dist2 = dist1;
                dist1 = dist;
                closestCenter = center;
                closestCellX = nx;
                closestCellZ = nz;
            } else if (dist < dist2) {
                dist2 = dist;
            }
        }
    }

    VoronoiResult result;
    result.distance1 = std::sqrt(dist1);
    result.distance2 = std::sqrt(dist2);
    result.cellCenter = closestCenter;
    result.cellId = cellHash(closestCellX, closestCellZ);
    return result;
}

float VoronoiNoise2D::evaluateF1(float x, float z) const {
    return evaluate(x, z).distance1;
}

float VoronoiNoise2D::evaluateF2MinusF1(float x, float z) const {
    auto r = evaluate(x, z);
    return r.distance2 - r.distance1;
}

}  // namespace finevox::worldgen
