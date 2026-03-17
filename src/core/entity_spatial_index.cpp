#include "finevox/core/entity_spatial_index.hpp"
#include <cmath>
#include <limits>

namespace finevox {

EntitySpatialIndex::CellKey EntitySpatialIndex::toCell(const Vec3& pos) {
    return {
        static_cast<int>(std::floor(pos.x / CELL_SIZE)),
        static_cast<int>(std::floor(pos.y / CELL_SIZE)),
        static_cast<int>(std::floor(pos.z / CELL_SIZE))
    };
}

void EntitySpatialIndex::insert(EntityId id, const Vec3& position) {
    auto cell = toCell(position);
    cells_[cell].insert(id);
    entityCells_[id] = cell;
    entityPositions_[id] = position;
}

void EntitySpatialIndex::remove(EntityId id) {
    auto it = entityCells_.find(id);
    if (it == entityCells_.end()) return;

    auto& cellSet = cells_[it->second];
    cellSet.erase(id);
    if (cellSet.empty()) {
        cells_.erase(it->second);
    }
    entityCells_.erase(it);
    entityPositions_.erase(id);
}

void EntitySpatialIndex::update(EntityId id, const Vec3& newPos) {
    auto cellIt = entityCells_.find(id);
    if (cellIt == entityCells_.end()) return;

    auto oldCell = cellIt->second;
    auto newCell = toCell(newPos);

    entityPositions_[id] = newPos;

    if (oldCell == newCell) return;

    // Move between cells
    auto& oldSet = cells_[oldCell];
    oldSet.erase(id);
    if (oldSet.empty()) {
        cells_.erase(oldCell);
    }

    cells_[newCell].insert(id);
    entityCells_[id] = newCell;
}

std::vector<EntityId> EntitySpatialIndex::queryRadius(const Vec3& center, float radius) const {
    std::vector<EntityId> result;
    float radiusSq = radius * radius;

    // Determine cell range to scan
    int cellRadius = static_cast<int>(std::ceil(radius / CELL_SIZE));
    auto centerCell = toCell(center);

    for (int cx = centerCell.x - cellRadius; cx <= centerCell.x + cellRadius; ++cx) {
        for (int cy = centerCell.y - cellRadius; cy <= centerCell.y + cellRadius; ++cy) {
            for (int cz = centerCell.z - cellRadius; cz <= centerCell.z + cellRadius; ++cz) {
                auto it = cells_.find({cx, cy, cz});
                if (it == cells_.end()) continue;

                for (EntityId id : it->second) {
                    auto posIt = entityPositions_.find(id);
                    if (posIt == entityPositions_.end()) continue;

                    float dx = posIt->second.x - center.x;
                    float dy = posIt->second.y - center.y;
                    float dz = posIt->second.z - center.z;
                    if (dx * dx + dy * dy + dz * dz <= radiusSq) {
                        result.push_back(id);
                    }
                }
            }
        }
    }

    return result;
}

std::vector<EntityId> EntitySpatialIndex::queryAABB(const Vec3& min, const Vec3& max) const {
    std::vector<EntityId> result;

    auto minCell = toCell(min);
    auto maxCell = toCell(max);

    for (int cx = minCell.x; cx <= maxCell.x; ++cx) {
        for (int cy = minCell.y; cy <= maxCell.y; ++cy) {
            for (int cz = minCell.z; cz <= maxCell.z; ++cz) {
                auto it = cells_.find({cx, cy, cz});
                if (it == cells_.end()) continue;

                for (EntityId id : it->second) {
                    auto posIt = entityPositions_.find(id);
                    if (posIt == entityPositions_.end()) continue;

                    const auto& p = posIt->second;
                    if (p.x >= min.x && p.x <= max.x &&
                        p.y >= min.y && p.y <= max.y &&
                        p.z >= min.z && p.z <= max.z) {
                        result.push_back(id);
                    }
                }
            }
        }
    }

    return result;
}

EntityId EntitySpatialIndex::findNearest(const Vec3& center, float radius,
                                          const std::function<bool(EntityId)>& filter) const {
    EntityId nearest = INVALID_ENTITY_ID;
    float bestDistSq = std::numeric_limits<float>::max();
    float radiusSq = radius * radius;

    int cellRadius = static_cast<int>(std::ceil(radius / CELL_SIZE));
    auto centerCell = toCell(center);

    for (int cx = centerCell.x - cellRadius; cx <= centerCell.x + cellRadius; ++cx) {
        for (int cy = centerCell.y - cellRadius; cy <= centerCell.y + cellRadius; ++cy) {
            for (int cz = centerCell.z - cellRadius; cz <= centerCell.z + cellRadius; ++cz) {
                auto it = cells_.find({cx, cy, cz});
                if (it == cells_.end()) continue;

                for (EntityId id : it->second) {
                    if (filter && !filter(id)) continue;

                    auto posIt = entityPositions_.find(id);
                    if (posIt == entityPositions_.end()) continue;

                    float dx = posIt->second.x - center.x;
                    float dy = posIt->second.y - center.y;
                    float dz = posIt->second.z - center.z;
                    float distSq = dx * dx + dy * dy + dz * dz;
                    if (distSq <= radiusSq && distSq < bestDistSq) {
                        bestDistSq = distSq;
                        nearest = id;
                    }
                }
            }
        }
    }

    return nearest;
}

void EntitySpatialIndex::clear() {
    cells_.clear();
    entityCells_.clear();
    entityPositions_.clear();
}

}  // namespace finevox
