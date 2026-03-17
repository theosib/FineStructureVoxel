#pragma once

/**
 * @file entity_spatial_index.hpp
 * @brief Grid-based spatial index for fast entity proximity queries
 *
 * Cell size matches chunk width (16 blocks). O(cells_in_range) lookups
 * instead of O(total_entities).
 */

#include "finevox/core/block_event.hpp"  // EntityId
#include "finevox/core/physics.hpp"      // Vec3, AABB
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>

namespace finevox {

class EntitySpatialIndex {
public:
    static constexpr int CELL_SIZE = 16;

    void insert(EntityId id, const Vec3& position);
    void remove(EntityId id);
    /// Update position (uses internally stored old position for cell change detection)
    void update(EntityId id, const Vec3& newPos);

    /// Find all entities within a sphere
    std::vector<EntityId> queryRadius(const Vec3& center, float radius) const;

    /// Find all entities within an AABB
    std::vector<EntityId> queryAABB(const Vec3& min, const Vec3& max) const;

    /// Find the nearest entity within radius, optionally filtering by predicate
    EntityId findNearest(const Vec3& center, float radius,
                         const std::function<bool(EntityId)>& filter = nullptr) const;

    /// Clear all entries
    void clear();

    /// Number of tracked entities
    [[nodiscard]] size_t size() const { return entityCells_.size(); }

private:
    struct CellKey {
        int x, y, z;
        bool operator==(const CellKey& o) const {
            return x == o.x && y == o.y && z == o.z;
        }
    };

    struct CellKeyHash {
        size_t operator()(const CellKey& k) const {
            // Simple spatial hash
            size_t h = 0;
            h ^= std::hash<int>()(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int>()(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };

    static CellKey toCell(const Vec3& pos);

    // Cell → set of entity IDs in that cell
    std::unordered_map<CellKey, std::unordered_set<EntityId>, CellKeyHash> cells_;

    // Entity → which cell it's in (for fast removal/update)
    std::unordered_map<EntityId, CellKey> entityCells_;

    // Entity → exact position (for distance filtering)
    std::unordered_map<EntityId, Vec3> entityPositions_;
};

}  // namespace finevox
