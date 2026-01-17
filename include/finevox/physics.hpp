#pragma once

#include "finevox/position.hpp"
#include "finevox/rotation.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <array>
#include <vector>
#include <cmath>
#include <algorithm>
#include <limits>

namespace finevox {

// ============================================================================
// GLM type aliases for convenience
// ============================================================================

using Vec3 = glm::vec3;
using Vec2 = glm::vec2;
using IVec3 = glm::ivec3;

// ============================================================================
// Utility functions for Vec3
// ============================================================================

// Convert BlockPos to Vec3 (corner of block)
inline Vec3 toVec3(const BlockPos& pos) {
    return Vec3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z));
}

// Convert BlockPos to Vec3 (center of block)
inline Vec3 toVec3Center(const BlockPos& pos) {
    return Vec3(static_cast<float>(pos.x) + 0.5f,
                static_cast<float>(pos.y) + 0.5f,
                static_cast<float>(pos.z) + 0.5f);
}

// Convert Vec3 to BlockPos (floor)
inline BlockPos toBlockPos(const Vec3& v) {
    return BlockPos(
        static_cast<int32_t>(std::floor(v.x)),
        static_cast<int32_t>(std::floor(v.y)),
        static_cast<int32_t>(std::floor(v.z))
    );
}

// ============================================================================
// AABB - Axis-Aligned Bounding Box
// ============================================================================

struct AABB {
    Vec3 min{0.0f};
    Vec3 max{0.0f};

    AABB() = default;
    AABB(const Vec3& min_, const Vec3& max_) : min(min_), max(max_) {}
    AABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
        : min(minX, minY, minZ), max(maxX, maxY, maxZ) {}

    // Create AABB for a full block at given position
    [[nodiscard]] static AABB forBlock(int32_t x, int32_t y, int32_t z) {
        return AABB(
            static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
            static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1)
        );
    }

    [[nodiscard]] static AABB forBlock(const BlockPos& pos) {
        return forBlock(pos.x, pos.y, pos.z);
    }

    // Create AABB centered at origin with given half-extents
    [[nodiscard]] static AABB fromHalfExtents(const Vec3& center, const Vec3& halfExtents) {
        return AABB(center - halfExtents, center + halfExtents);
    }

    // Properties
    [[nodiscard]] Vec3 center() const {
        return (min + max) * 0.5f;
    }

    [[nodiscard]] Vec3 size() const {
        return max - min;
    }

    [[nodiscard]] Vec3 halfExtents() const {
        return (max - min) * 0.5f;
    }

    [[nodiscard]] float width() const { return max.x - min.x; }
    [[nodiscard]] float height() const { return max.y - min.y; }
    [[nodiscard]] float depth() const { return max.z - min.z; }

    // Test if this AABB intersects another (inclusive boundaries)
    [[nodiscard]] bool intersects(const AABB& other) const {
        return min.x <= other.max.x && max.x >= other.min.x &&
               min.y <= other.max.y && max.y >= other.min.y &&
               min.z <= other.max.z && max.z >= other.min.z;
    }

    // Test if this AABB contains a point
    [[nodiscard]] bool contains(const Vec3& point) const {
        return point.x >= min.x && point.x <= max.x &&
               point.y >= min.y && point.y <= max.y &&
               point.z >= min.z && point.z <= max.z;
    }

    // Test if this AABB fully contains another
    [[nodiscard]] bool contains(const AABB& other) const {
        return other.min.x >= min.x && other.max.x <= max.x &&
               other.min.y >= min.y && other.max.y <= max.y &&
               other.min.z >= min.z && other.max.z <= max.z;
    }

    // Swept collision: returns time of first impact (0-1) when moving this AABB
    // by velocity toward a stationary AABB. Returns >1 if no collision.
    // entryNormal is set to the normal of the face hit (pointing away from other)
    [[nodiscard]] float sweepCollision(const AABB& other, const Vec3& velocity, Vec3* entryNormal = nullptr) const;

    // Expand AABB by amount in all directions
    [[nodiscard]] AABB expanded(const Vec3& amount) const {
        return AABB(min - amount, max + amount);
    }

    [[nodiscard]] AABB expanded(float amount) const {
        return expanded(Vec3(amount, amount, amount));
    }

    // Translate AABB by offset
    [[nodiscard]] AABB translated(const Vec3& offset) const {
        return AABB(min + offset, max + offset);
    }

    // Union of two AABBs (smallest AABB containing both)
    [[nodiscard]] AABB merged(const AABB& other) const {
        return AABB(glm::min(min, other.min), glm::max(max, other.max));
    }

    // Intersection of two AABBs (may be empty/invalid if no overlap)
    [[nodiscard]] AABB intersection(const AABB& other) const {
        return AABB(glm::max(min, other.min), glm::min(max, other.max));
    }

    // Check if AABB is valid (min <= max on all axes)
    [[nodiscard]] bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    bool operator==(const AABB& other) const {
        return min == other.min && max == other.max;
    }
};

// ============================================================================
// CollisionShape - Collection of AABBs for complex block shapes
// ============================================================================

class CollisionShape {
public:
    CollisionShape() = default;

    // Add a box to the shape (in local [0,1] coordinates)
    void addBox(const AABB& box);

    // Get all boxes
    [[nodiscard]] const std::vector<AABB>& boxes() const { return boxes_; }

    // Check if empty (no collision)
    [[nodiscard]] bool isEmpty() const { return boxes_.empty(); }

    // Get bounding box of entire shape
    [[nodiscard]] AABB bounds() const;

    // Transform shape to world coordinates at given block position
    [[nodiscard]] std::vector<AABB> atPosition(const BlockPos& pos) const;
    [[nodiscard]] std::vector<AABB> atPosition(int32_t x, int32_t y, int32_t z) const;

    // Transform shape by rotation (rotates around [0.5, 0.5, 0.5] center)
    [[nodiscard]] CollisionShape transformed(const Rotation& rotation) const;

    // Precompute all 24 rotations
    [[nodiscard]] static std::array<CollisionShape, 24> computeRotations(const CollisionShape& base);

    // ========================================================================
    // Standard shapes
    // ========================================================================

    // Empty shape (no collision) - for pass-through blocks like air, tall grass
    static const CollisionShape NONE;

    // Full 1x1x1 block
    static const CollisionShape FULL_BLOCK;

    // Half slab (bottom half)
    static const CollisionShape HALF_SLAB_BOTTOM;

    // Half slab (top half)
    static const CollisionShape HALF_SLAB_TOP;

    // Fence post (thin center column plus connections handled separately)
    static const CollisionShape FENCE_POST;

    // Thin floor (like carpet or pressure plate)
    static const CollisionShape THIN_FLOOR;

private:
    std::vector<AABB> boxes_;
};

// ============================================================================
// RaycastMode - What to check during raycast
// ============================================================================

enum class RaycastMode {
    Collision,   // Check collision boxes (for physics queries)
    Interaction, // Check hit boxes (for player clicks/attacks)
    Both         // Check either (for general queries)
};

// ============================================================================
// RaycastResult - Result of a raycast operation
// ============================================================================

struct RaycastResult {
    bool hit = false;
    BlockPos blockPos;        // Block that was hit
    Face face = Face::PosY;   // Face of the block that was hit
    Vec3 hitPoint{0.0f};      // Exact hit point in world coordinates
    float distance = 0.0f;    // Distance from origin to hit point

    // For entity hits (future use)
    // EntityId entity = EntityId::INVALID;

    explicit operator bool() const { return hit; }
};

// ============================================================================
// Physics constants
// ============================================================================

// Minimum margin between entities and blocks to prevent floating-point glitching
// See docs/08-physics.md section 8.4 for explanation
constexpr float COLLISION_MARGIN = 0.001f;

}  // namespace finevox
