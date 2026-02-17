#pragma once

/**
 * @file pathfinder.hpp
 * @brief A* pathfinding over the block grid
 *
 * Used by AI goals for mob movement. Finds walkable paths considering
 * entity dimensions, block collision, and step height.
 */

#include "finevox/core/position.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace finevox {

class World;

class Pathfinder {
public:
    struct PathNode {
        BlockCoord pos;
    };
    using Path = std::vector<PathNode>;

    /**
     * @brief Find a path from start to goal
     *
     * @param world World for block collision queries
     * @param start Starting position (world coordinates)
     * @param goal Target position (world coordinates)
     * @param entityWidth Entity width (for clearance checks)
     * @param entityHeight Entity height (for clearance checks)
     * @param maxDistance Maximum search radius in blocks
     * @param maxIterations Maximum A* iterations (prevents runaway)
     * @return Path if found, nullopt if no path exists or limit reached
     */
    [[nodiscard]] static std::optional<Path> findPath(
        const World& world,
        const glm::dvec3& start,
        const glm::dvec3& goal,
        float entityWidth = 0.6f,
        float entityHeight = 1.8f,
        int maxDistance = 32,
        int maxIterations = 200
    );

    /**
     * @brief Check if a block position is walkable for an entity
     *
     * @param world World for block queries
     * @param pos Position to check (feet position)
     * @param entityWidth Entity width
     * @param entityHeight Entity height
     * @return true if the entity can stand at this position
     */
    [[nodiscard]] static bool isWalkable(
        const World& world,
        BlockCoord pos,
        float entityWidth = 0.6f,
        float entityHeight = 1.8f
    );
};

}  // namespace finevox
