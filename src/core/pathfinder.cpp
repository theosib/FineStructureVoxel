#include "finevox/core/pathfinder.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <cmath>

namespace finevox {

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

struct PathState {
    BlockCoord pos;
    float gCost = 0.0f;
    float fCost = 0.0f;

    bool operator>(const PathState& other) const {
        return fCost > other.fCost;
    }
};

struct BlockCoordHash {
    size_t operator()(const BlockCoord& p) const {
        auto h1 = std::hash<int32_t>{}(p.x);
        auto h2 = std::hash<int32_t>{}(p.y);
        auto h3 = std::hash<int32_t>{}(p.z);
        return h1 ^ (h2 * 2654435761u) ^ (h3 * 40503u);
    }
};

struct BlockCoordEqual {
    bool operator()(const BlockCoord& a, const BlockCoord& b) const {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

float heuristic(const BlockCoord& a, const BlockCoord& b) {
    float dx = static_cast<float>(std::abs(a.x - b.x));
    float dy = static_cast<float>(std::abs(a.y - b.y));
    float dz = static_cast<float>(std::abs(a.z - b.z));
    return dx + dy + dz;  // Manhattan distance
}

bool isSolid(const World& world, BlockCoord pos) {
    auto blockId = world.getBlock(pos);
    if (blockId.isAir()) return false;
    const auto& type = BlockRegistry::global().getType(blockId);
    return type.hasCollision();
}

bool isAirLike(const World& world, BlockCoord pos) {
    auto blockId = world.getBlock(pos);
    if (blockId.isAir()) return true;
    const auto& type = BlockRegistry::global().getType(blockId);
    return !type.hasCollision();
}

// Neighbor offsets for horizontal movement (including diagonal)
constexpr int DX[] = {1, -1, 0, 0};
constexpr int DZ[] = {0, 0, 1, -1};

}  // anonymous namespace

// ============================================================================
// isWalkable
// ============================================================================

bool Pathfinder::isWalkable(
    const World& world,
    BlockCoord pos,
    float /*entityWidth*/,
    float entityHeight
) {
    // The block at pos must have a solid block below (to stand on)
    BlockCoord below{pos.x, pos.y - 1, pos.z};
    if (!isSolid(world, below)) return false;

    // The blocks at pos and above must be clear (for the entity height)
    int clearanceNeeded = static_cast<int>(std::ceil(entityHeight));
    for (int dy = 0; dy < clearanceNeeded; ++dy) {
        BlockCoord check{pos.x, pos.y + dy, pos.z};
        if (!isAirLike(world, check)) return false;
    }

    return true;
}

// ============================================================================
// findPath — A* implementation
// ============================================================================

std::optional<Pathfinder::Path> Pathfinder::findPath(
    const World& world,
    const glm::dvec3& start,
    const glm::dvec3& goal,
    float entityWidth,
    float entityHeight,
    int maxDistance,
    int maxIterations
) {
    BlockCoord startBlock{
        static_cast<int32_t>(std::floor(start.x)),
        static_cast<int32_t>(std::floor(start.y)),
        static_cast<int32_t>(std::floor(start.z))
    };
    BlockCoord goalBlock{
        static_cast<int32_t>(std::floor(goal.x)),
        static_cast<int32_t>(std::floor(goal.y)),
        static_cast<int32_t>(std::floor(goal.z))
    };

    // Quick check: if goal is unreachable
    if (!isWalkable(world, goalBlock, entityWidth, entityHeight)) {
        return std::nullopt;
    }

    // Distance check
    float dist = heuristic(startBlock, goalBlock);
    if (dist > static_cast<float>(maxDistance)) {
        return std::nullopt;
    }

    // A* search
    std::priority_queue<PathState, std::vector<PathState>, std::greater<>> open;
    std::unordered_map<BlockCoord, BlockCoord, BlockCoordHash, BlockCoordEqual> cameFrom;
    std::unordered_map<BlockCoord, float, BlockCoordHash, BlockCoordEqual> gScore;

    gScore[startBlock] = 0.0f;
    open.push({startBlock, 0.0f, heuristic(startBlock, goalBlock)});

    int iterations = 0;
    while (!open.empty() && iterations < maxIterations) {
        ++iterations;

        auto current = open.top();
        open.pop();

        if (current.pos.x == goalBlock.x &&
            current.pos.y == goalBlock.y &&
            current.pos.z == goalBlock.z) {
            // Reconstruct path
            Path path;
            BlockCoord pos = goalBlock;
            while (!(pos.x == startBlock.x && pos.y == startBlock.y && pos.z == startBlock.z)) {
                path.push_back({pos});
                auto it = cameFrom.find(pos);
                if (it == cameFrom.end()) break;
                pos = it->second;
            }
            std::reverse(path.begin(), path.end());
            return path;
        }

        // Check 4 horizontal neighbors + up/down step
        for (int i = 0; i < 4; ++i) {
            BlockCoord neighbor{
                current.pos.x + DX[i],
                current.pos.y,
                current.pos.z + DZ[i]
            };

            // Try same level
            if (isWalkable(world, neighbor, entityWidth, entityHeight)) {
                float tentG = current.gCost + 1.0f;
                auto it = gScore.find(neighbor);
                if (it == gScore.end() || tentG < it->second) {
                    gScore[neighbor] = tentG;
                    cameFrom[neighbor] = current.pos;
                    open.push({neighbor, tentG, tentG + heuristic(neighbor, goalBlock)});
                }
                continue;
            }

            // Try step up (1 block)
            BlockCoord stepUp{neighbor.x, neighbor.y + 1, neighbor.z};
            if (isWalkable(world, stepUp, entityWidth, entityHeight)) {
                // Also need clearance above current position for step-up
                BlockCoord headRoom{current.pos.x, current.pos.y + static_cast<int32_t>(std::ceil(entityHeight)), current.pos.z};
                if (isAirLike(world, headRoom)) {
                    float tentG = current.gCost + 1.5f;  // Slightly more expensive
                    auto it = gScore.find(stepUp);
                    if (it == gScore.end() || tentG < it->second) {
                        gScore[stepUp] = tentG;
                        cameFrom[stepUp] = current.pos;
                        open.push({stepUp, tentG, tentG + heuristic(stepUp, goalBlock)});
                    }
                }
            }

            // Try step down (1 block)
            BlockCoord stepDown{neighbor.x, neighbor.y - 1, neighbor.z};
            if (isWalkable(world, stepDown, entityWidth, entityHeight)) {
                float tentG = current.gCost + 1.5f;
                auto it = gScore.find(stepDown);
                if (it == gScore.end() || tentG < it->second) {
                    gScore[stepDown] = tentG;
                    cameFrom[stepDown] = current.pos;
                    open.push({stepDown, tentG, tentG + heuristic(stepDown, goalBlock)});
                }
            }
        }
    }

    return std::nullopt;  // No path found
}

}  // namespace finevox
