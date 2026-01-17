#include "finevox/physics.hpp"

namespace finevox {

// ============================================================================
// AABB implementation
// ============================================================================

float AABB::sweepCollision(const AABB& other, const Vec3& velocity, Vec3* entryNormal) const {
    // Minkowski sum approach: expand 'other' by our half-extents and treat us as a point

    // If already overlapping, return 0
    if (intersects(other)) {
        if (entryNormal) {
            // Determine push-out direction based on overlap
            Vec3 overlap(
                glm::min(max.x - other.min.x, other.max.x - min.x),
                glm::min(max.y - other.min.y, other.max.y - min.y),
                glm::min(max.z - other.min.z, other.max.z - min.z)
            );

            if (overlap.x <= overlap.y && overlap.x <= overlap.z) {
                *entryNormal = (center().x < other.center().x) ? Vec3(-1, 0, 0) : Vec3(1, 0, 0);
            } else if (overlap.y <= overlap.z) {
                *entryNormal = (center().y < other.center().y) ? Vec3(0, -1, 0) : Vec3(0, 1, 0);
            } else {
                *entryNormal = (center().z < other.center().z) ? Vec3(0, 0, -1) : Vec3(0, 0, 1);
            }
        }
        return 0.0f;
    }

    // No movement = no collision
    if (velocity.x == 0.0f && velocity.y == 0.0f && velocity.z == 0.0f) {
        return std::numeric_limits<float>::infinity();
    }

    // Expand other box by our size (Minkowski difference)
    AABB expandedOther(
        other.min.x - (max.x - min.x),
        other.min.y - (max.y - min.y),
        other.min.z - (max.z - min.z),
        other.max.x,
        other.max.y,
        other.max.z
    );

    // Ray-box intersection from our min corner
    Vec3 origin = min;

    // Calculate entry and exit times for each axis
    float tEntryX, tExitX, tEntryY, tExitY, tEntryZ, tExitZ;

    // X axis
    if (velocity.x == 0.0f) {
        if (origin.x < expandedOther.min.x || origin.x > expandedOther.max.x) {
            return std::numeric_limits<float>::infinity();
        }
        tEntryX = -std::numeric_limits<float>::infinity();
        tExitX = std::numeric_limits<float>::infinity();
    } else {
        float invVelX = 1.0f / velocity.x;
        tEntryX = (expandedOther.min.x - origin.x) * invVelX;
        tExitX = (expandedOther.max.x - origin.x) * invVelX;
        if (tEntryX > tExitX) std::swap(tEntryX, tExitX);
    }

    // Y axis
    if (velocity.y == 0.0f) {
        if (origin.y < expandedOther.min.y || origin.y > expandedOther.max.y) {
            return std::numeric_limits<float>::infinity();
        }
        tEntryY = -std::numeric_limits<float>::infinity();
        tExitY = std::numeric_limits<float>::infinity();
    } else {
        float invVelY = 1.0f / velocity.y;
        tEntryY = (expandedOther.min.y - origin.y) * invVelY;
        tExitY = (expandedOther.max.y - origin.y) * invVelY;
        if (tEntryY > tExitY) std::swap(tEntryY, tExitY);
    }

    // Z axis
    if (velocity.z == 0.0f) {
        if (origin.z < expandedOther.min.z || origin.z > expandedOther.max.z) {
            return std::numeric_limits<float>::infinity();
        }
        tEntryZ = -std::numeric_limits<float>::infinity();
        tExitZ = std::numeric_limits<float>::infinity();
    } else {
        float invVelZ = 1.0f / velocity.z;
        tEntryZ = (expandedOther.min.z - origin.z) * invVelZ;
        tExitZ = (expandedOther.max.z - origin.z) * invVelZ;
        if (tEntryZ > tExitZ) std::swap(tEntryZ, tExitZ);
    }

    // Find latest entry time and earliest exit time
    float tEntry = glm::max(glm::max(tEntryX, tEntryY), tEntryZ);
    float tExit = glm::min(glm::min(tExitX, tExitY), tExitZ);

    // No collision if entry is after exit, or if collision is behind us or too far
    if (tEntry > tExit || tExit < 0.0f || tEntry > 1.0f) {
        return std::numeric_limits<float>::infinity();
    }

    // Determine collision normal
    if (entryNormal) {
        if (tEntryX > tEntryY && tEntryX > tEntryZ) {
            *entryNormal = (velocity.x < 0.0f) ? Vec3(1, 0, 0) : Vec3(-1, 0, 0);
        } else if (tEntryY > tEntryZ) {
            *entryNormal = (velocity.y < 0.0f) ? Vec3(0, 1, 0) : Vec3(0, -1, 0);
        } else {
            *entryNormal = (velocity.z < 0.0f) ? Vec3(0, 0, 1) : Vec3(0, 0, -1);
        }
    }

    return glm::max(0.0f, tEntry);
}

// ============================================================================
// CollisionShape implementation
// ============================================================================

void CollisionShape::addBox(const AABB& box) {
    boxes_.push_back(box);
}

AABB CollisionShape::bounds() const {
    if (boxes_.empty()) {
        return AABB(0, 0, 0, 0, 0, 0);
    }

    AABB result = boxes_[0];
    for (size_t i = 1; i < boxes_.size(); ++i) {
        result = result.merged(boxes_[i]);
    }
    return result;
}

std::vector<AABB> CollisionShape::atPosition(const BlockPos& pos) const {
    return atPosition(pos.x, pos.y, pos.z);
}

std::vector<AABB> CollisionShape::atPosition(int32_t x, int32_t y, int32_t z) const {
    std::vector<AABB> result;
    result.reserve(boxes_.size());

    Vec3 offset(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    for (const auto& box : boxes_) {
        result.push_back(box.translated(offset));
    }
    return result;
}

CollisionShape CollisionShape::transformed(const Rotation& rotation) const {
    CollisionShape result;

    for (const auto& box : boxes_) {
        // Transform corners relative to center [0.5, 0.5, 0.5]
        Vec3 center(0.5f, 0.5f, 0.5f);
        Vec3 localMin = box.min - center;
        Vec3 localMax = box.max - center;

        // Get all 8 corners
        Vec3 corners[8] = {
            {localMin.x, localMin.y, localMin.z},
            {localMax.x, localMin.y, localMin.z},
            {localMin.x, localMax.y, localMin.z},
            {localMax.x, localMax.y, localMin.z},
            {localMin.x, localMin.y, localMax.z},
            {localMax.x, localMin.y, localMax.z},
            {localMin.x, localMax.y, localMax.z},
            {localMax.x, localMax.y, localMax.z}
        };

        // Transform all corners
        const auto& matrix = rotation.matrix();
        Vec3 transformedMin(std::numeric_limits<float>::max());
        Vec3 transformedMax(-std::numeric_limits<float>::max());

        for (const auto& corner : corners) {
            Vec3 transformed(
                matrix[0][0] * corner.x + matrix[0][1] * corner.y + matrix[0][2] * corner.z,
                matrix[1][0] * corner.x + matrix[1][1] * corner.y + matrix[1][2] * corner.z,
                matrix[2][0] * corner.x + matrix[2][1] * corner.y + matrix[2][2] * corner.z
            );

            transformedMin = glm::min(transformedMin, transformed);
            transformedMax = glm::max(transformedMax, transformed);
        }

        // Convert back to [0,1] space
        result.addBox(AABB(transformedMin + center, transformedMax + center));
    }

    return result;
}

std::array<CollisionShape, 24> CollisionShape::computeRotations(const CollisionShape& base) {
    std::array<CollisionShape, 24> result;
    for (uint8_t i = 0; i < 24; ++i) {
        result[i] = base.transformed(Rotation::byIndex(i));
    }
    return result;
}

// ============================================================================
// Standard shapes
// ============================================================================

const CollisionShape CollisionShape::NONE = []() {
    return CollisionShape();
}();

const CollisionShape CollisionShape::FULL_BLOCK = []() {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));
    return shape;
}();

const CollisionShape CollisionShape::HALF_SLAB_BOTTOM = []() {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f));
    return shape;
}();

const CollisionShape CollisionShape::HALF_SLAB_TOP = []() {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f));
    return shape;
}();

const CollisionShape CollisionShape::FENCE_POST = []() {
    CollisionShape shape;
    // Center post: 6/16 to 10/16 on X and Z, full height
    shape.addBox(AABB(0.375f, 0.0f, 0.375f, 0.625f, 1.0f, 0.625f));
    return shape;
}();

const CollisionShape CollisionShape::THIN_FLOOR = []() {
    CollisionShape shape;
    // 1/16 block height
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 1.0f, 0.0625f, 1.0f));
    return shape;
}();

}  // namespace finevox
