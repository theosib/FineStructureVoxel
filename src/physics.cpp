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

bool AABB::rayIntersect(const Vec3& origin, const Vec3& direction,
                        float* tMin, float* tMax, Face* hitFace) const {
    // Slab method for ray-AABB intersection
    float t1 = -std::numeric_limits<float>::infinity();
    float t2 = std::numeric_limits<float>::infinity();
    int hitAxis = -1;
    bool hitNeg = false;

    for (int axis = 0; axis < 3; ++axis) {
        float rayOrigin = origin[axis];
        float rayDir = direction[axis];
        float boxMin = min[axis];
        float boxMax = max[axis];

        if (std::abs(rayDir) < 1e-8f) {
            // Ray is parallel to slab - check if origin is within slab
            if (rayOrigin < boxMin || rayOrigin > boxMax) {
                return false;  // No intersection
            }
            // Otherwise this axis doesn't constrain t
        } else {
            float invDir = 1.0f / rayDir;
            float tNear = (boxMin - rayOrigin) * invDir;
            float tFar = (boxMax - rayOrigin) * invDir;

            bool enteredNeg = true;  // Entering from negative side
            if (tNear > tFar) {
                std::swap(tNear, tFar);
                enteredNeg = false;
            }

            if (tNear > t1) {
                t1 = tNear;
                hitAxis = axis;
                hitNeg = enteredNeg;
            }
            if (tFar < t2) {
                t2 = tFar;
            }

            if (t1 > t2) {
                return false;  // No intersection
            }
        }
    }

    // Check if intersection is in front of ray (t1 >= 0) or we're inside box
    if (t2 < 0.0f) {
        return false;  // Box is behind ray
    }

    if (tMin) *tMin = t1;
    if (tMax) *tMax = t2;

    if (hitFace && hitAxis >= 0) {
        // Determine which face was hit based on axis and direction
        if (t1 < 0.0f) {
            // Ray starts inside box - no entry face
            *hitFace = Face::PosY;  // Default
        } else {
            switch (hitAxis) {
                case 0: *hitFace = hitNeg ? Face::NegX : Face::PosX; break;
                case 1: *hitFace = hitNeg ? Face::NegY : Face::PosY; break;
                case 2: *hitFace = hitNeg ? Face::NegZ : Face::PosZ; break;
            }
        }
    }

    return true;
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

// ============================================================================
// Raycasting implementation
// ============================================================================

RaycastResult raycastBlocks(
    const Vec3& origin,
    const Vec3& direction,
    float maxDistance,
    RaycastMode mode,
    const BlockShapeProvider& shapeProvider
) {
    RaycastResult result;
    result.hit = false;

    // Normalize direction for consistent distance calculations
    Vec3 dir = glm::normalize(direction);

    // Current block position (floor of origin)
    int32_t x = static_cast<int32_t>(std::floor(origin.x));
    int32_t y = static_cast<int32_t>(std::floor(origin.y));
    int32_t z = static_cast<int32_t>(std::floor(origin.z));

    // Step direction for each axis (+1 or -1)
    int32_t stepX = (dir.x >= 0.0f) ? 1 : -1;
    int32_t stepY = (dir.y >= 0.0f) ? 1 : -1;
    int32_t stepZ = (dir.z >= 0.0f) ? 1 : -1;

    // Distance along ray to next block boundary for each axis
    // tMaxX = distance to reach the next X boundary
    float tMaxX, tMaxY, tMaxZ;

    // How far along ray we must travel to cross one block in each axis
    float tDeltaX = (std::abs(dir.x) < 1e-8f) ? std::numeric_limits<float>::infinity() : std::abs(1.0f / dir.x);
    float tDeltaY = (std::abs(dir.y) < 1e-8f) ? std::numeric_limits<float>::infinity() : std::abs(1.0f / dir.y);
    float tDeltaZ = (std::abs(dir.z) < 1e-8f) ? std::numeric_limits<float>::infinity() : std::abs(1.0f / dir.z);

    // Initial tMax values
    if (std::abs(dir.x) < 1e-8f) {
        tMaxX = std::numeric_limits<float>::infinity();
    } else if (dir.x > 0.0f) {
        tMaxX = (static_cast<float>(x + 1) - origin.x) / dir.x;
    } else {
        tMaxX = (static_cast<float>(x) - origin.x) / dir.x;
    }

    if (std::abs(dir.y) < 1e-8f) {
        tMaxY = std::numeric_limits<float>::infinity();
    } else if (dir.y > 0.0f) {
        tMaxY = (static_cast<float>(y + 1) - origin.y) / dir.y;
    } else {
        tMaxY = (static_cast<float>(y) - origin.y) / dir.y;
    }

    if (std::abs(dir.z) < 1e-8f) {
        tMaxZ = std::numeric_limits<float>::infinity();
    } else if (dir.z > 0.0f) {
        tMaxZ = (static_cast<float>(z + 1) - origin.z) / dir.z;
    } else {
        tMaxZ = (static_cast<float>(z) - origin.z) / dir.z;
    }

    // Track which face we entered through
    Face entryFace = Face::PosY;  // Default
    float currentT = 0.0f;

    // Check starting block first (we might be inside a block)
    BlockPos startPos(x, y, z);
    const CollisionShape* startShape = shapeProvider(startPos, mode);
    if (startShape && !startShape->isEmpty()) {
        auto boxes = startShape->atPosition(startPos);
        for (const auto& box : boxes) {
            float tMin, tMax;
            Face hitFace;
            if (box.rayIntersect(origin, dir, &tMin, &tMax, &hitFace)) {
                // We're starting inside or hitting this box
                if (tMin <= 0.0f && tMax >= 0.0f) {
                    // Inside the box
                    result.hit = true;
                    result.blockPos = startPos;
                    result.face = hitFace;
                    result.hitPoint = origin;
                    result.distance = 0.0f;
                    return result;
                } else if (tMin > 0.0f && tMin <= maxDistance) {
                    result.hit = true;
                    result.blockPos = startPos;
                    result.face = hitFace;
                    result.hitPoint = origin + dir * tMin;
                    result.distance = tMin;
                    return result;
                }
            }
        }
    }

    // DDA traversal
    while (currentT <= maxDistance) {
        // Step to next block boundary
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                currentT = tMaxX;
                x += stepX;
                tMaxX += tDeltaX;
                entryFace = (stepX > 0) ? Face::NegX : Face::PosX;
            } else {
                currentT = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
                entryFace = (stepZ > 0) ? Face::NegZ : Face::PosZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                currentT = tMaxY;
                y += stepY;
                tMaxY += tDeltaY;
                entryFace = (stepY > 0) ? Face::NegY : Face::PosY;
            } else {
                currentT = tMaxZ;
                z += stepZ;
                tMaxZ += tDeltaZ;
                entryFace = (stepZ > 0) ? Face::NegZ : Face::PosZ;
            }
        }

        if (currentT > maxDistance) {
            break;
        }

        // Check this block for collision
        BlockPos blockPos(x, y, z);
        const CollisionShape* shape = shapeProvider(blockPos, mode);

        if (shape && !shape->isEmpty()) {
            auto boxes = shape->atPosition(blockPos);
            float bestT = std::numeric_limits<float>::infinity();
            Face bestFace = entryFace;

            for (const auto& box : boxes) {
                float tMin, tMax;
                Face hitFace;
                if (box.rayIntersect(origin, dir, &tMin, &tMax, &hitFace)) {
                    if (tMin > 0.0f && tMin < bestT && tMin <= maxDistance) {
                        bestT = tMin;
                        bestFace = hitFace;
                    }
                }
            }

            if (bestT < std::numeric_limits<float>::infinity()) {
                result.hit = true;
                result.blockPos = blockPos;
                result.face = bestFace;
                result.hitPoint = origin + dir * bestT;
                result.distance = bestT;
                return result;
            }
        }
    }

    return result;  // No hit
}

// ============================================================================
// PhysicsSystem implementation
// ============================================================================

PhysicsSystem::PhysicsSystem(BlockShapeProvider shapeProvider)
    : shapeProvider_(std::move(shapeProvider)) {}

std::vector<AABB> PhysicsSystem::collectColliders(const AABB& region) const {
    std::vector<AABB> colliders;

    // Get the block range that overlaps with region
    int32_t minX = static_cast<int32_t>(std::floor(region.min.x));
    int32_t minY = static_cast<int32_t>(std::floor(region.min.y));
    int32_t minZ = static_cast<int32_t>(std::floor(region.min.z));
    int32_t maxX = static_cast<int32_t>(std::floor(region.max.x));
    int32_t maxY = static_cast<int32_t>(std::floor(region.max.y));
    int32_t maxZ = static_cast<int32_t>(std::floor(region.max.z));

    for (int32_t y = minY; y <= maxY; ++y) {
        for (int32_t z = minZ; z <= maxZ; ++z) {
            for (int32_t x = minX; x <= maxX; ++x) {
                BlockPos pos(x, y, z);
                const CollisionShape* shape = shapeProvider_(pos, RaycastMode::Collision);

                if (shape && !shape->isEmpty()) {
                    auto boxes = shape->atPosition(pos);
                    for (const auto& box : boxes) {
                        if (box.intersects(region)) {
                            colliders.push_back(box);
                        }
                    }
                }
            }
        }
    }

    return colliders;
}

float PhysicsSystem::resolveAxisCollision(
    const AABB& entityBox,
    const std::vector<AABB>& colliders,
    int axis,
    float movement
) const {
    if (std::abs(movement) < 1e-8f) {
        return 0.0f;
    }

    float result = movement;

    for (const auto& collider : colliders) {
        // Check if we overlap on the other two axes
        bool overlapsOtherAxes = true;
        for (int i = 0; i < 3; ++i) {
            if (i != axis) {
                if (entityBox.max[i] <= collider.min[i] || entityBox.min[i] >= collider.max[i]) {
                    overlapsOtherAxes = false;
                    break;
                }
            }
        }

        if (!overlapsOtherAxes) {
            continue;
        }

        if (movement > 0.0f) {
            // Moving in positive direction
            float maxAllowed = collider.min[axis] - entityBox.max[axis] - COLLISION_MARGIN;
            if (maxAllowed < result && maxAllowed > -COLLISION_MARGIN) {
                result = glm::max(0.0f, maxAllowed);
            }
        } else {
            // Moving in negative direction
            float maxAllowed = collider.max[axis] - entityBox.min[axis] + COLLISION_MARGIN;
            if (maxAllowed > result && maxAllowed < COLLISION_MARGIN) {
                result = glm::min(0.0f, maxAllowed);
            }
        }
    }

    return result;
}

Vec3 PhysicsSystem::tryStepClimbing(
    const AABB& entityBox,
    const std::vector<AABB>& colliders,
    const Vec3& desiredMovement,
    float maxStepHeight
) const {
    Vec3 bestMovement(0.0f);
    float bestHorizDist = 0.0f;

    // Try different step heights
    for (float stepHeight = 0.0f; stepHeight <= maxStepHeight; stepHeight += 0.0625f) {
        AABB steppedBox = entityBox.translated(Vec3(0.0f, stepHeight, 0.0f));

        // Check if we can move up to this height
        float upMove = resolveAxisCollision(entityBox, colliders, 1, stepHeight);
        if (upMove < stepHeight - COLLISION_MARGIN) {
            // Can't step up this high
            continue;
        }

        // Check if we can move horizontally from this height
        float moveX = resolveAxisCollision(steppedBox, colliders, 0, desiredMovement.x);
        steppedBox = steppedBox.translated(Vec3(moveX, 0.0f, 0.0f));

        float moveZ = resolveAxisCollision(steppedBox, colliders, 2, desiredMovement.z);
        steppedBox = steppedBox.translated(Vec3(0.0f, 0.0f, moveZ));

        // Check if we can step down (or stay at this height)
        float moveY = resolveAxisCollision(steppedBox, colliders, 1, -stepHeight + desiredMovement.y);

        Vec3 totalMove(moveX, moveY + stepHeight, moveZ);

        // Keep best result (most horizontal movement)
        float horizDist = moveX * moveX + moveZ * moveZ;
        if (horizDist > bestHorizDist + 0.001f) {
            bestHorizDist = horizDist;
            bestMovement = totalMove;
        }
    }

    return bestMovement;
}

Vec3 PhysicsSystem::moveBody(PhysicsBody& body, const Vec3& desiredMovement) {
    AABB entityBox = body.boundingBox();
    Vec3 finalMovement = desiredMovement;

    // Collect nearby colliders (expand search region by movement + 1 block margin)
    Vec3 absMove(std::abs(desiredMovement.x), std::abs(desiredMovement.y), std::abs(desiredMovement.z));
    AABB searchRegion = entityBox.expanded(absMove + Vec3(1.0f));
    auto colliders = collectColliders(searchRegion);

    // Try step-climbing for horizontal movement when on ground
    if (body.isOnGround() && body.canStepUp() &&
        (std::abs(desiredMovement.x) > 1e-6f || std::abs(desiredMovement.z) > 1e-6f)) {

        Vec3 stepMovement = tryStepClimbing(entityBox, colliders, desiredMovement, body.maxStepHeight());

        // Compare with standard collision resolution
        // Standard Y first, then X, then Z
        AABB tempBox = entityBox;
        Vec3 standardMovement;

        standardMovement.y = resolveAxisCollision(tempBox, colliders, 1, desiredMovement.y);
        tempBox = tempBox.translated(Vec3(0.0f, standardMovement.y, 0.0f));

        standardMovement.x = resolveAxisCollision(tempBox, colliders, 0, desiredMovement.x);
        tempBox = tempBox.translated(Vec3(standardMovement.x, 0.0f, 0.0f));

        standardMovement.z = resolveAxisCollision(tempBox, colliders, 2, desiredMovement.z);

        // Use step-climbing if it allows more horizontal movement
        float stepHorizDist = stepMovement.x * stepMovement.x + stepMovement.z * stepMovement.z;
        float stdHorizDist = standardMovement.x * standardMovement.x + standardMovement.z * standardMovement.z;

        if (stepHorizDist > stdHorizDist + 0.001f) {
            finalMovement = stepMovement;
        } else {
            finalMovement = standardMovement;
        }
    } else {
        // Standard XYZ collision resolution (Y first to handle landing)
        finalMovement.y = resolveAxisCollision(entityBox, colliders, 1, desiredMovement.y);
        entityBox = entityBox.translated(Vec3(0.0f, finalMovement.y, 0.0f));

        finalMovement.x = resolveAxisCollision(entityBox, colliders, 0, desiredMovement.x);
        entityBox = entityBox.translated(Vec3(finalMovement.x, 0.0f, 0.0f));

        finalMovement.z = resolveAxisCollision(entityBox, colliders, 2, desiredMovement.z);
    }

    // Apply movement
    body.setPosition(body.position() + finalMovement);

    // Update ground state: we're on ground if we wanted to move down but couldn't
    bool wasMovingDown = desiredMovement.y < -COLLISION_MARGIN;
    bool stoppedByGround = finalMovement.y > desiredMovement.y + COLLISION_MARGIN;
    body.setOnGround(wasMovingDown && stoppedByGround);

    return finalMovement;
}

void PhysicsSystem::applyGravity(PhysicsBody& body, float deltaTime) {
    if (!body.hasGravity()) {
        return;
    }

    Vec3 vel = body.velocity();
    vel.y -= gravity_ * deltaTime;
    body.setVelocity(vel);
}

Vec3 PhysicsSystem::update(PhysicsBody& body, float deltaTime) {
    applyGravity(body, deltaTime);
    Vec3 desiredMovement = body.velocity() * deltaTime;
    Vec3 actualMovement = moveBody(body, desiredMovement);

    // If we hit something vertically, zero out vertical velocity
    if (std::abs(actualMovement.y - desiredMovement.y) > COLLISION_MARGIN) {
        Vec3 vel = body.velocity();
        vel.y = 0.0f;
        body.setVelocity(vel);
    }

    return actualMovement;
}

bool PhysicsSystem::checkOnGround(const PhysicsBody& body) const {
    AABB entityBox = body.boundingBox();

    // Check slightly below the entity
    AABB checkRegion = entityBox.translated(Vec3(0.0f, -COLLISION_MARGIN * 2.0f, 0.0f));
    checkRegion.max.y = entityBox.min.y + COLLISION_MARGIN;  // Only check the bottom

    auto colliders = collectColliders(checkRegion);

    for (const auto& collider : colliders) {
        // Check if collider is directly below us (within margin)
        if (collider.max.y >= entityBox.min.y - COLLISION_MARGIN * 2.0f &&
            collider.max.y <= entityBox.min.y + COLLISION_MARGIN) {
            // Check horizontal overlap
            if (entityBox.max.x > collider.min.x && entityBox.min.x < collider.max.x &&
                entityBox.max.z > collider.min.z && entityBox.min.z < collider.max.z) {
                return true;
            }
        }
    }

    return false;
}

RaycastResult PhysicsSystem::raycast(const Vec3& origin, const Vec3& direction,
                                      float maxDistance, RaycastMode mode) const {
    return raycastBlocks(origin, direction, maxDistance, mode, shapeProvider_);
}

}  // namespace finevox
