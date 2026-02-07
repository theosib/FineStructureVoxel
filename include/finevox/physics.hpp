#pragma once

/**
 * @file physics.hpp
 * @brief AABB collision detection, raycasting, and step-climbing
 *
 * Design: [08-physics.md] §8.1-8.7
 */

#include "finevox/position.hpp"
#include "finevox/rotation.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <array>
#include <vector>
#include <functional>
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

    // Ray intersection: returns true if ray hits this AABB.
    // tMin/tMax are set to entry/exit distances along ray.
    // hitFace is set to the face that was hit (entry face).
    [[nodiscard]] bool rayIntersect(const Vec3& origin, const Vec3& direction,
                                     float* tMin = nullptr, float* tMax = nullptr,
                                     Face* hitFace = nullptr) const;

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

// Maximum step height for step-climbing (slightly over half a block)
constexpr float MAX_STEP_HEIGHT = 0.625f;

// Default gravity in blocks per second squared
constexpr float DEFAULT_GRAVITY = 20.0f;

// ============================================================================
// Block shape provider callback
// ============================================================================

// Callback type for getting collision shape at a block position
// Returns the collision shape for the block, or nullptr/empty if no collision
using BlockShapeProvider = std::function<const CollisionShape*(const BlockPos& pos, RaycastMode mode)>;

// ============================================================================
// PhysicsBody - Interface for entities that participate in physics
// ============================================================================

// A minimal interface that entities must implement for physics interaction.
// This allows PhysicsSystem to work without depending on a specific Entity class.
class PhysicsBody {
public:
    virtual ~PhysicsBody() = default;

    // Position (bottom-center of bounding box)
    [[nodiscard]] virtual Vec3 position() const = 0;
    virtual void setPosition(const Vec3& pos) = 0;

    // Velocity
    [[nodiscard]] virtual Vec3 velocity() const = 0;
    virtual void setVelocity(const Vec3& vel) = 0;

    // Bounding box (in world coordinates, derived from position + half-extents)
    [[nodiscard]] virtual AABB boundingBox() const = 0;

    // Half-extents of the bounding box (fixed size, not affected by position)
    [[nodiscard]] virtual Vec3 halfExtents() const = 0;

    // Ground state
    [[nodiscard]] virtual bool isOnGround() const = 0;
    virtual void setOnGround(bool onGround) = 0;

    // Whether this body is affected by gravity
    [[nodiscard]] virtual bool hasGravity() const { return true; }

    // Whether this body can step up (climb stairs)
    [[nodiscard]] virtual bool canStepUp() const { return true; }

    // Maximum step height for this body (can be overridden per-entity)
    // Different games use different step heights (e.g., Hytale steps full blocks,
    // Minecraft steps ~0.625 blocks). This can also depend on entity enhancements
    // like special armor or abilities.
    [[nodiscard]] virtual float maxStepHeight() const { return MAX_STEP_HEIGHT; }
};

// ============================================================================
// SimplePhysicsBody - Basic implementation of PhysicsBody for testing
// ============================================================================

class SimplePhysicsBody : public PhysicsBody {
public:
    SimplePhysicsBody(const Vec3& pos, const Vec3& halfExt)
        : position_(pos), halfExtents_(halfExt) {}

    [[nodiscard]] Vec3 position() const override { return position_; }
    void setPosition(const Vec3& pos) override { position_ = pos; }

    [[nodiscard]] Vec3 velocity() const override { return velocity_; }
    void setVelocity(const Vec3& vel) override { velocity_ = vel; }

    [[nodiscard]] AABB boundingBox() const override {
        // Position is bottom-center, so min.y = position.y
        return AABB(
            position_.x - halfExtents_.x,
            position_.y,
            position_.z - halfExtents_.z,
            position_.x + halfExtents_.x,
            position_.y + halfExtents_.y * 2.0f,
            position_.z + halfExtents_.z
        );
    }

    [[nodiscard]] Vec3 halfExtents() const override { return halfExtents_; }

    [[nodiscard]] bool isOnGround() const override { return onGround_; }
    void setOnGround(bool onGround) override { onGround_ = onGround; }

    [[nodiscard]] float maxStepHeight() const override { return maxStepHeight_; }
    void setMaxStepHeight(float h) { maxStepHeight_ = h; }

private:
    Vec3 position_{0.0f};
    Vec3 velocity_{0.0f};
    Vec3 halfExtents_{0.3f, 0.9f, 0.3f};  // Default player-like size
    bool onGround_ = false;
    float maxStepHeight_ = MAX_STEP_HEIGHT;
};

// ============================================================================
// PhysicsSystem - Handles entity movement and collision
// ============================================================================

class PhysicsSystem {
public:
    // Constructor takes a block shape provider for collision detection
    explicit PhysicsSystem(BlockShapeProvider shapeProvider);

    // Move a physics body, handling collisions
    // Returns actual movement vector after collision resolution
    Vec3 moveBody(PhysicsBody& body, const Vec3& desiredMovement);

    // Apply gravity to body velocity (call before moveBody)
    void applyGravity(PhysicsBody& body, float deltaTime);

    // Combined update: apply gravity and move
    Vec3 update(PhysicsBody& body, float deltaTime);

    // Check if body is on ground (touching surface below)
    [[nodiscard]] bool checkOnGround(const PhysicsBody& body) const;

    // Raycast through world
    [[nodiscard]] RaycastResult raycast(const Vec3& origin, const Vec3& direction,
                                         float maxDistance,
                                         RaycastMode mode = RaycastMode::Interaction) const;

    // Configuration
    void setGravity(float g) { gravity_ = g; }
    [[nodiscard]] float gravity() const { return gravity_; }

    void setMaxStepHeight(float h) { maxStepHeight_ = h; }
    [[nodiscard]] float maxStepHeight() const { return maxStepHeight_; }

private:
    BlockShapeProvider shapeProvider_;
    float gravity_ = DEFAULT_GRAVITY;
    float maxStepHeight_ = MAX_STEP_HEIGHT;

    // Collect all collision AABBs in a region
    [[nodiscard]] std::vector<AABB> collectColliders(const AABB& region) const;

    // Resolve collision along one axis
    // Returns the actual movement possible while maintaining COLLISION_MARGIN
    [[nodiscard]] float resolveAxisCollision(
        const AABB& entityBox,
        const std::vector<AABB>& colliders,
        int axis,
        float movement
    ) const;

    // Try step-climbing (returns true if step-up improved movement)
    // maxStepHeight is provided by the body being moved
    [[nodiscard]] Vec3 tryStepClimbing(
        const AABB& entityBox,
        const std::vector<AABB>& colliders,
        const Vec3& desiredMovement,
        float maxStepHeight
    ) const;
};

// ============================================================================
// Block placement collision utilities
// ============================================================================

/**
 * @brief Policy for block placement when it would intersect an entity
 */
enum class BlockPlacementMode {
    BlockIfIntersects,  // Default: prevent placement if it would intersect
    PushEntity          // Allow placement and push entity out of the way
};

/**
 * @brief Check if placing a block would intersect an entity's bounding box
 *
 * Uses COLLISION_MARGIN to account for floating point precision.
 * The block AABB is shrunk by COLLISION_MARGIN so entities can stand
 * exactly at block boundaries without triggering false intersections.
 *
 * @param blockPos Position where block would be placed
 * @param entityBox Entity's current bounding box
 * @return true if the block would intersect the entity
 */
[[nodiscard]] inline bool wouldBlockIntersectEntity(const BlockPos& blockPos, const AABB& entityBox) {
    // Block AABB shrunk by margin to allow entities at exact boundaries
    AABB blockBox(
        static_cast<float>(blockPos.x) + COLLISION_MARGIN,
        static_cast<float>(blockPos.y) + COLLISION_MARGIN,
        static_cast<float>(blockPos.z) + COLLISION_MARGIN,
        static_cast<float>(blockPos.x + 1) - COLLISION_MARGIN,
        static_cast<float>(blockPos.y + 1) - COLLISION_MARGIN,
        static_cast<float>(blockPos.z + 1) - COLLISION_MARGIN
    );
    return blockBox.intersects(entityBox);
}

/**
 * @brief Check if placing a block would intersect a physics body
 *
 * Convenience overload that extracts the bounding box from a PhysicsBody.
 */
[[nodiscard]] inline bool wouldBlockIntersectBody(const BlockPos& blockPos, const PhysicsBody& body) {
    return wouldBlockIntersectEntity(blockPos, body.boundingBox());
}

// ============================================================================
// Raycasting utilities
// ============================================================================

// Raycast through world using DDA algorithm
// Calls shapeProvider for each block along the ray to get its collision shape
// Returns first hit within maxDistance, or empty result if no hit
[[nodiscard]] RaycastResult raycastBlocks(
    const Vec3& origin,
    const Vec3& direction,
    float maxDistance,
    RaycastMode mode,
    const BlockShapeProvider& shapeProvider
);

// ============================================================================
// Camera collision utilities
// ============================================================================

// Minimum distance camera should maintain from walls to avoid near-plane clipping.
// This should be larger than the camera's near plane (typically 0.1) plus the
// half-width of the near plane at grazing angles. With 70° FOV and 16:9 aspect,
// the corner of the near plane is ~0.14 from center. At 45° to a wall, we need
// extra margin because the frustum edge extends further toward the wall.
// Using 0.4 to ensure the entire near plane stays outside walls at all angles.
constexpr float CAMERA_COLLISION_RADIUS = 0.4f;

/**
 * @brief Adjust camera position to prevent clipping through walls
 *
 * Uses a two-phase approach:
 * 1. Raycast from safe origin to desired camera to handle walls between body and camera
 * 2. Probe outward from the camera position in 6 directions to ensure minimum clearance
 *
 * This prevents the camera's near plane from clipping through walls when
 * the player is pressed against surfaces or in tight corners.
 *
 * @param safeOrigin A point known to be in open space (e.g., body center)
 * @param desiredCameraPos Where the camera would ideally be positioned
 * @param cameraRadius Minimum distance from walls (default: CAMERA_COLLISION_RADIUS)
 * @param shapeProvider Function to get collision shapes for blocks
 * @return Adjusted camera position that maintains minimum wall distance
 */
[[nodiscard]] inline Vec3 adjustCameraForWallCollision(
    const Vec3& safeOrigin,
    const Vec3& desiredCameraPos,
    float cameraRadius,
    const BlockShapeProvider& shapeProvider
) {
    Vec3 toCamera = desiredCameraPos - safeOrigin;
    float distance = toCamera.length();

    Vec3 adjustedPos = desiredCameraPos;

    // Phase 1: Check path from safe origin to camera
    if (distance > 0.001f) {
        Vec3 direction = toCamera / distance;
        RaycastResult hit = raycastBlocks(safeOrigin, direction, distance, RaycastMode::Collision, shapeProvider);

        if (hit.hit) {
            // Hit a wall - position camera at hit point pulled back by radius
            float adjustedDistance = hit.distance - cameraRadius;
            if (adjustedDistance < 0.0f) {
                adjustedDistance = 0.0f;
            }
            adjustedPos = safeOrigin + direction * adjustedDistance;
        }
    }

    // Phase 2: Probe in 26 directions (6 cardinal + 12 edge + 8 corner) to ensure clearance
    // This catches walls at any angle, including diagonals when looking along a wall
    static const float d = 0.577350269f;  // 1/sqrt(3) for normalized diagonals
    static const float e = 0.707106781f;  // 1/sqrt(2) for edge diagonals
    static const Vec3 probeDirections[26] = {
        // 6 cardinal (faces)
        Vec3(1, 0, 0), Vec3(-1, 0, 0),
        Vec3(0, 1, 0), Vec3(0, -1, 0),
        Vec3(0, 0, 1), Vec3(0, 0, -1),
        // 12 edge diagonals
        Vec3(e, e, 0), Vec3(e, -e, 0), Vec3(-e, e, 0), Vec3(-e, -e, 0),
        Vec3(e, 0, e), Vec3(e, 0, -e), Vec3(-e, 0, e), Vec3(-e, 0, -e),
        Vec3(0, e, e), Vec3(0, e, -e), Vec3(0, -e, e), Vec3(0, -e, -e),
        // 8 corner diagonals
        Vec3(d, d, d), Vec3(d, d, -d), Vec3(d, -d, d), Vec3(d, -d, -d),
        Vec3(-d, d, d), Vec3(-d, d, -d), Vec3(-d, -d, d), Vec3(-d, -d, -d)
    };

    Vec3 pushback(0, 0, 0);
    for (const Vec3& dir : probeDirections) {
        RaycastResult hit = raycastBlocks(adjustedPos, dir, cameraRadius, RaycastMode::Collision, shapeProvider);
        if (hit.hit) {
            // Wall is closer than cameraRadius - need to push back
            float penetration = cameraRadius - hit.distance;
            if (penetration > 0.0f) {
                pushback = pushback - dir * penetration;
            }
        }
    }

    // Apply pushback, but don't push past safe origin
    adjustedPos = adjustedPos + pushback;

    // Ensure we don't end up behind the safe origin (inside body)
    Vec3 toAdjusted = adjustedPos - safeOrigin;
    if (glm::dot(toAdjusted, toCamera) < 0.0f && distance > 0.001f) {
        // Pushed behind origin, clamp to origin
        adjustedPos = safeOrigin;
    }

    return adjustedPos;
}

/**
 * @brief Convenience overload using default camera radius
 */
[[nodiscard]] inline Vec3 adjustCameraForWallCollision(
    const Vec3& safeOrigin,
    const Vec3& desiredCameraPos,
    const BlockShapeProvider& shapeProvider
) {
    return adjustCameraForWallCollision(safeOrigin, desiredCameraPos, CAMERA_COLLISION_RADIUS, shapeProvider);
}

}  // namespace finevox
