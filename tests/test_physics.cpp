#include <gtest/gtest.h>
#include "finevox/physics.hpp"
#include <cmath>
#include <unordered_set>

using namespace finevox;

// ============================================================================
// Vec3 (GLM) utility tests
// ============================================================================

TEST(Vec3Test, DefaultConstruction) {
    Vec3 v{};
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vec3Test, ValueConstruction) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vec3Test, ToVec3Center) {
    BlockPos pos(10, 20, 30);
    Vec3 v = toVec3Center(pos);
    // Center of block
    EXPECT_FLOAT_EQ(v.x, 10.5f);
    EXPECT_FLOAT_EQ(v.y, 20.5f);
    EXPECT_FLOAT_EQ(v.z, 30.5f);
}

TEST(Vec3Test, ToVec3Corner) {
    BlockPos pos(10, 20, 30);
    Vec3 v = toVec3(pos);
    // Corner of block
    EXPECT_FLOAT_EQ(v.x, 10.0f);
    EXPECT_FLOAT_EQ(v.y, 20.0f);
    EXPECT_FLOAT_EQ(v.z, 30.0f);
}

TEST(Vec3Test, Arithmetic) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);

    Vec3 sum = a + b;
    EXPECT_FLOAT_EQ(sum.x, 5.0f);
    EXPECT_FLOAT_EQ(sum.y, 7.0f);
    EXPECT_FLOAT_EQ(sum.z, 9.0f);

    Vec3 diff = b - a;
    EXPECT_FLOAT_EQ(diff.x, 3.0f);
    EXPECT_FLOAT_EQ(diff.y, 3.0f);
    EXPECT_FLOAT_EQ(diff.z, 3.0f);

    Vec3 scaled = a * 2.0f;
    EXPECT_FLOAT_EQ(scaled.x, 2.0f);
    EXPECT_FLOAT_EQ(scaled.y, 4.0f);
    EXPECT_FLOAT_EQ(scaled.z, 6.0f);

    Vec3 divided = b / 2.0f;
    EXPECT_FLOAT_EQ(divided.x, 2.0f);
    EXPECT_FLOAT_EQ(divided.y, 2.5f);
    EXPECT_FLOAT_EQ(divided.z, 3.0f);
}

TEST(Vec3Test, DotProduct) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(glm::dot(a, b), 1*4 + 2*5 + 3*6);  // 32
}

TEST(Vec3Test, CrossProduct) {
    Vec3 x(1.0f, 0.0f, 0.0f);
    Vec3 y(0.0f, 1.0f, 0.0f);
    Vec3 z = glm::cross(x, y);
    EXPECT_FLOAT_EQ(z.x, 0.0f);
    EXPECT_FLOAT_EQ(z.y, 0.0f);
    EXPECT_FLOAT_EQ(z.z, 1.0f);
}

TEST(Vec3Test, Length) {
    Vec3 v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(glm::length(v), 5.0f);
}

TEST(Vec3Test, Normalized) {
    Vec3 v(3.0f, 4.0f, 0.0f);
    Vec3 n = glm::normalize(v);
    EXPECT_FLOAT_EQ(n.x, 0.6f);
    EXPECT_FLOAT_EQ(n.y, 0.8f);
    EXPECT_FLOAT_EQ(n.z, 0.0f);
    EXPECT_NEAR(glm::length(n), 1.0f, 1e-6f);
}

TEST(Vec3Test, MinMax) {
    Vec3 a(1.0f, 5.0f, 3.0f);
    Vec3 b(4.0f, 2.0f, 3.0f);

    Vec3 minV = glm::min(a, b);
    EXPECT_FLOAT_EQ(minV.x, 1.0f);
    EXPECT_FLOAT_EQ(minV.y, 2.0f);
    EXPECT_FLOAT_EQ(minV.z, 3.0f);

    Vec3 maxV = glm::max(a, b);
    EXPECT_FLOAT_EQ(maxV.x, 4.0f);
    EXPECT_FLOAT_EQ(maxV.y, 5.0f);
    EXPECT_FLOAT_EQ(maxV.z, 3.0f);
}

TEST(Vec3Test, ToBlockPos) {
    Vec3 v(1.5f, 2.9f, -0.1f);
    BlockPos pos = toBlockPos(v);
    EXPECT_EQ(pos.x, 1);
    EXPECT_EQ(pos.y, 2);
    EXPECT_EQ(pos.z, -1);  // floor(-0.1) = -1
}

TEST(Vec3Test, IndexAccess) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v[0], 1.0f);
    EXPECT_FLOAT_EQ(v[1], 2.0f);
    EXPECT_FLOAT_EQ(v[2], 3.0f);

    v[1] = 5.0f;
    EXPECT_FLOAT_EQ(v.y, 5.0f);
}

// ============================================================================
// AABB tests
// ============================================================================

TEST(AABBTest, DefaultConstruction) {
    AABB box;
    EXPECT_FLOAT_EQ(box.min.x, 0.0f);
    EXPECT_FLOAT_EQ(box.max.x, 0.0f);
}

TEST(AABBTest, ValueConstruction) {
    AABB box(1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(box.min.x, 1.0f);
    EXPECT_FLOAT_EQ(box.min.y, 2.0f);
    EXPECT_FLOAT_EQ(box.min.z, 3.0f);
    EXPECT_FLOAT_EQ(box.max.x, 4.0f);
    EXPECT_FLOAT_EQ(box.max.y, 5.0f);
    EXPECT_FLOAT_EQ(box.max.z, 6.0f);
}

TEST(AABBTest, ForBlock) {
    AABB box = AABB::forBlock(5, 10, 15);
    EXPECT_FLOAT_EQ(box.min.x, 5.0f);
    EXPECT_FLOAT_EQ(box.min.y, 10.0f);
    EXPECT_FLOAT_EQ(box.min.z, 15.0f);
    EXPECT_FLOAT_EQ(box.max.x, 6.0f);
    EXPECT_FLOAT_EQ(box.max.y, 11.0f);
    EXPECT_FLOAT_EQ(box.max.z, 16.0f);
}

TEST(AABBTest, ForBlockNegative) {
    AABB box = AABB::forBlock(-5, -10, -15);
    EXPECT_FLOAT_EQ(box.min.x, -5.0f);
    EXPECT_FLOAT_EQ(box.min.y, -10.0f);
    EXPECT_FLOAT_EQ(box.min.z, -15.0f);
    EXPECT_FLOAT_EQ(box.max.x, -4.0f);
    EXPECT_FLOAT_EQ(box.max.y, -9.0f);
    EXPECT_FLOAT_EQ(box.max.z, -14.0f);
}

TEST(AABBTest, Properties) {
    AABB box(0.0f, 0.0f, 0.0f, 2.0f, 4.0f, 6.0f);

    Vec3 center = box.center();
    EXPECT_FLOAT_EQ(center.x, 1.0f);
    EXPECT_FLOAT_EQ(center.y, 2.0f);
    EXPECT_FLOAT_EQ(center.z, 3.0f);

    Vec3 size = box.size();
    EXPECT_FLOAT_EQ(size.x, 2.0f);
    EXPECT_FLOAT_EQ(size.y, 4.0f);
    EXPECT_FLOAT_EQ(size.z, 6.0f);

    EXPECT_FLOAT_EQ(box.width(), 2.0f);
    EXPECT_FLOAT_EQ(box.height(), 4.0f);
    EXPECT_FLOAT_EQ(box.depth(), 6.0f);
}

TEST(AABBTest, IntersectsOverlapping) {
    AABB a(0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f);
    AABB b(1.0f, 1.0f, 1.0f, 3.0f, 3.0f, 3.0f);
    EXPECT_TRUE(a.intersects(b));
    EXPECT_TRUE(b.intersects(a));
}

TEST(AABBTest, IntersectsTouching) {
    AABB a(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB b(1.0f, 0.0f, 0.0f, 2.0f, 1.0f, 1.0f);
    EXPECT_TRUE(a.intersects(b));  // Touching at face is intersection
}

TEST(AABBTest, IntersectsNoOverlap) {
    AABB a(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB b(2.0f, 0.0f, 0.0f, 3.0f, 1.0f, 1.0f);
    EXPECT_FALSE(a.intersects(b));
    EXPECT_FALSE(b.intersects(a));
}

TEST(AABBTest, ContainsPoint) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);

    EXPECT_TRUE(box.contains(Vec3(0.5f, 0.5f, 0.5f)));  // Center
    EXPECT_TRUE(box.contains(Vec3(0.0f, 0.0f, 0.0f)));  // Corner (inclusive)
    EXPECT_TRUE(box.contains(Vec3(1.0f, 1.0f, 1.0f)));  // Opposite corner
    EXPECT_FALSE(box.contains(Vec3(1.5f, 0.5f, 0.5f)));  // Outside
    EXPECT_FALSE(box.contains(Vec3(-0.1f, 0.5f, 0.5f)));  // Outside
}

TEST(AABBTest, ContainsAABB) {
    AABB outer(0.0f, 0.0f, 0.0f, 4.0f, 4.0f, 4.0f);
    AABB inner(1.0f, 1.0f, 1.0f, 3.0f, 3.0f, 3.0f);
    AABB partial(2.0f, 2.0f, 2.0f, 5.0f, 5.0f, 5.0f);

    EXPECT_TRUE(outer.contains(inner));
    EXPECT_FALSE(inner.contains(outer));
    EXPECT_FALSE(outer.contains(partial));
}

TEST(AABBTest, Expanded) {
    AABB box(1.0f, 1.0f, 1.0f, 2.0f, 2.0f, 2.0f);
    AABB expanded = box.expanded(0.5f);

    EXPECT_FLOAT_EQ(expanded.min.x, 0.5f);
    EXPECT_FLOAT_EQ(expanded.min.y, 0.5f);
    EXPECT_FLOAT_EQ(expanded.min.z, 0.5f);
    EXPECT_FLOAT_EQ(expanded.max.x, 2.5f);
    EXPECT_FLOAT_EQ(expanded.max.y, 2.5f);
    EXPECT_FLOAT_EQ(expanded.max.z, 2.5f);
}

TEST(AABBTest, Translated) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB translated = box.translated(Vec3(5.0f, 10.0f, 15.0f));

    EXPECT_FLOAT_EQ(translated.min.x, 5.0f);
    EXPECT_FLOAT_EQ(translated.min.y, 10.0f);
    EXPECT_FLOAT_EQ(translated.min.z, 15.0f);
    EXPECT_FLOAT_EQ(translated.max.x, 6.0f);
    EXPECT_FLOAT_EQ(translated.max.y, 11.0f);
    EXPECT_FLOAT_EQ(translated.max.z, 16.0f);
}

TEST(AABBTest, Merged) {
    AABB a(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB b(2.0f, 2.0f, 2.0f, 3.0f, 3.0f, 3.0f);
    AABB merged = a.merged(b);

    EXPECT_FLOAT_EQ(merged.min.x, 0.0f);
    EXPECT_FLOAT_EQ(merged.min.y, 0.0f);
    EXPECT_FLOAT_EQ(merged.min.z, 0.0f);
    EXPECT_FLOAT_EQ(merged.max.x, 3.0f);
    EXPECT_FLOAT_EQ(merged.max.y, 3.0f);
    EXPECT_FLOAT_EQ(merged.max.z, 3.0f);
}

TEST(AABBTest, IsValid) {
    AABB valid(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB invalid(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);  // min.x > max.x

    EXPECT_TRUE(valid.isValid());
    EXPECT_FALSE(invalid.isValid());
}

// ============================================================================
// AABB Swept Collision tests
// ============================================================================

TEST(AABBSweepTest, NoMovement) {
    AABB moving(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB stationary(3.0f, 0.0f, 0.0f, 4.0f, 1.0f, 1.0f);

    float t = moving.sweepCollision(stationary, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_GT(t, 1.0f);  // No collision
}

TEST(AABBSweepTest, MovingTowardCollision) {
    AABB moving(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB stationary(2.0f, 0.0f, 0.0f, 3.0f, 1.0f, 1.0f);

    Vec3 normal;
    float t = moving.sweepCollision(stationary, Vec3(4.0f, 0.0f, 0.0f), &normal);

    // Moving 4 units, gap is 1 unit, so collision at t=0.25
    EXPECT_NEAR(t, 0.25f, 0.001f);
    EXPECT_FLOAT_EQ(normal.x, -1.0f);  // Hit from left
    EXPECT_FLOAT_EQ(normal.y, 0.0f);
    EXPECT_FLOAT_EQ(normal.z, 0.0f);
}

TEST(AABBSweepTest, MovingAwayNoCollision) {
    AABB moving(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB stationary(2.0f, 0.0f, 0.0f, 3.0f, 1.0f, 1.0f);

    float t = moving.sweepCollision(stationary, Vec3(-4.0f, 0.0f, 0.0f));
    EXPECT_GT(t, 1.0f);  // No collision (moving away)
}

TEST(AABBSweepTest, AlreadyOverlapping) {
    AABB moving(0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f);
    AABB stationary(1.0f, 1.0f, 1.0f, 3.0f, 3.0f, 3.0f);

    float t = moving.sweepCollision(stationary, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(t, 0.0f);  // Already colliding
}

TEST(AABBSweepTest, MovingYAxis) {
    AABB moving(0.0f, 5.0f, 0.0f, 1.0f, 6.0f, 1.0f);  // Above
    AABB stationary(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);  // On ground

    Vec3 normal;
    float t = moving.sweepCollision(stationary, Vec3(0.0f, -8.0f, 0.0f), &normal);

    // Gap is 4 units, moving 8 units down, collision at t=0.5
    EXPECT_NEAR(t, 0.5f, 0.001f);
    EXPECT_FLOAT_EQ(normal.x, 0.0f);
    EXPECT_FLOAT_EQ(normal.y, 1.0f);  // Hit from above
    EXPECT_FLOAT_EQ(normal.z, 0.0f);
}

TEST(AABBSweepTest, MissParallel) {
    AABB moving(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB stationary(0.0f, 2.0f, 0.0f, 1.0f, 3.0f, 1.0f);  // Above with gap

    float t = moving.sweepCollision(stationary, Vec3(10.0f, 0.0f, 0.0f));
    EXPECT_GT(t, 1.0f);  // No collision (moving parallel)
}

TEST(AABBSweepTest, DiagonalCollision) {
    AABB moving(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    AABB stationary(3.0f, 2.0f, 0.0f, 4.0f, 3.0f, 1.0f);

    Vec3 normal;
    float t = moving.sweepCollision(stationary, Vec3(4.0f, 4.0f, 0.0f), &normal);

    // Should hit - diagonal motion
    EXPECT_LE(t, 1.0f);
    EXPECT_GE(t, 0.0f);
}

// ============================================================================
// CollisionShape tests
// ============================================================================

TEST(CollisionShapeTest, EmptyShape) {
    CollisionShape shape;
    EXPECT_TRUE(shape.isEmpty());
    EXPECT_TRUE(shape.boxes().empty());
}

TEST(CollisionShapeTest, AddBox) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f));

    EXPECT_FALSE(shape.isEmpty());
    EXPECT_EQ(shape.boxes().size(), 1);
}

TEST(CollisionShapeTest, Bounds) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 0.5f, 0.5f, 0.5f));
    shape.addBox(AABB(0.5f, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f));

    AABB bounds = shape.bounds();
    EXPECT_FLOAT_EQ(bounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 1.0f);
}

TEST(CollisionShapeTest, AtPosition) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 1.0f, 0.5f, 1.0f));

    auto boxes = shape.atPosition(10, 20, 30);
    ASSERT_EQ(boxes.size(), 1);
    EXPECT_FLOAT_EQ(boxes[0].min.x, 10.0f);
    EXPECT_FLOAT_EQ(boxes[0].min.y, 20.0f);
    EXPECT_FLOAT_EQ(boxes[0].min.z, 30.0f);
    EXPECT_FLOAT_EQ(boxes[0].max.x, 11.0f);
    EXPECT_FLOAT_EQ(boxes[0].max.y, 20.5f);
    EXPECT_FLOAT_EQ(boxes[0].max.z, 31.0f);
}

TEST(CollisionShapeTest, StandardShapeNone) {
    const auto& shape = CollisionShape::NONE;
    EXPECT_TRUE(shape.isEmpty());
}

TEST(CollisionShapeTest, StandardShapeFullBlock) {
    const auto& shape = CollisionShape::FULL_BLOCK;
    EXPECT_FALSE(shape.isEmpty());
    EXPECT_EQ(shape.boxes().size(), 1);

    AABB bounds = shape.bounds();
    EXPECT_FLOAT_EQ(bounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 1.0f);
}

TEST(CollisionShapeTest, StandardShapeHalfSlabBottom) {
    const auto& shape = CollisionShape::HALF_SLAB_BOTTOM;
    EXPECT_EQ(shape.boxes().size(), 1);

    AABB bounds = shape.bounds();
    EXPECT_FLOAT_EQ(bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 0.5f);
}

TEST(CollisionShapeTest, StandardShapeHalfSlabTop) {
    const auto& shape = CollisionShape::HALF_SLAB_TOP;
    EXPECT_EQ(shape.boxes().size(), 1);

    AABB bounds = shape.bounds();
    EXPECT_FLOAT_EQ(bounds.min.y, 0.5f);
    EXPECT_FLOAT_EQ(bounds.max.y, 1.0f);
}

TEST(CollisionShapeTest, StandardShapeFencePost) {
    const auto& shape = CollisionShape::FENCE_POST;
    EXPECT_EQ(shape.boxes().size(), 1);

    AABB bounds = shape.bounds();
    // Fence post is centered, narrower than full block
    EXPECT_GT(bounds.min.x, 0.0f);
    EXPECT_LT(bounds.max.x, 1.0f);
}

TEST(CollisionShapeTest, StandardShapeThinFloor) {
    const auto& shape = CollisionShape::THIN_FLOOR;
    EXPECT_EQ(shape.boxes().size(), 1);

    AABB bounds = shape.bounds();
    EXPECT_FLOAT_EQ(bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 0.0625f);  // 1/16
}

// ============================================================================
// CollisionShape rotation tests
// ============================================================================

TEST(CollisionShapeRotationTest, IdentityRotation) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f));  // Half block on -X side

    CollisionShape rotated = shape.transformed(Rotation::IDENTITY);
    EXPECT_EQ(rotated.boxes().size(), 1);

    AABB bounds = rotated.bounds();
    EXPECT_NEAR(bounds.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(bounds.max.x, 0.5f, 0.001f);
}

TEST(CollisionShapeRotationTest, Rotate180Y) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f));  // Half block on -X side

    CollisionShape rotated = shape.transformed(Rotation::ROTATE_Y_180);
    AABB bounds = rotated.bounds();

    // After 180 degree Y rotation, should be on +X side
    EXPECT_NEAR(bounds.min.x, 0.5f, 0.001f);
    EXPECT_NEAR(bounds.max.x, 1.0f, 0.001f);
}

TEST(CollisionShapeRotationTest, Rotate90Y) {
    CollisionShape shape;
    shape.addBox(AABB(0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f));  // Half block on -X side

    CollisionShape rotated = shape.transformed(Rotation::ROTATE_Y_90);
    AABB bounds = rotated.bounds();

    // After 90 degree Y rotation (counterclockwise looking down Y):
    // X -> -Z, Z -> X
    // The -X half (0 to 0.5 on X) should become +Z half (0.5 to 1 on Z)
    EXPECT_NEAR(bounds.min.z, 0.5f, 0.001f);
    EXPECT_NEAR(bounds.max.z, 1.0f, 0.001f);
    // X should now span full width (was full Z)
    EXPECT_NEAR(bounds.min.x, 0.0f, 0.001f);
    EXPECT_NEAR(bounds.max.x, 1.0f, 0.001f);
}

TEST(CollisionShapeRotationTest, ComputeAllRotations) {
    auto rotations = CollisionShape::computeRotations(CollisionShape::HALF_SLAB_BOTTOM);

    EXPECT_EQ(rotations.size(), 24);

    // Each rotation should have 1 box
    for (const auto& shape : rotations) {
        EXPECT_EQ(shape.boxes().size(), 1);
    }
}

TEST(CollisionShapeRotationTest, RotatedSlabPositions) {
    // A bottom slab rotated in various ways should end up in different positions
    auto rotations = CollisionShape::computeRotations(CollisionShape::HALF_SLAB_BOTTOM);

    // Bottom slab: y from 0 to 0.5
    // Rotating around X by 180 should put it at top: y from 0.5 to 1.0
    // Find this rotation
    bool foundTopSlab = false;
    for (const auto& shape : rotations) {
        AABB bounds = shape.bounds();
        if (std::abs(bounds.min.y - 0.5f) < 0.01f && std::abs(bounds.max.y - 1.0f) < 0.01f) {
            foundTopSlab = true;
            break;
        }
    }
    EXPECT_TRUE(foundTopSlab);
}

// ============================================================================
// RaycastResult tests
// ============================================================================

TEST(RaycastResultTest, DefaultConstruction) {
    RaycastResult result;
    EXPECT_FALSE(result.hit);
    EXPECT_FALSE(result);  // bool conversion
}

TEST(RaycastResultTest, BoolConversion) {
    RaycastResult miss;
    miss.hit = false;
    EXPECT_FALSE(miss);

    RaycastResult hit;
    hit.hit = true;
    EXPECT_TRUE(hit);
}

// ============================================================================
// Physics constants tests
// ============================================================================

TEST(PhysicsConstantsTest, CollisionMargin) {
    // Verify margin is reasonable
    EXPECT_GT(COLLISION_MARGIN, 0.0f);
    EXPECT_LT(COLLISION_MARGIN, 0.01f);  // Less than 1cm
    EXPECT_GT(COLLISION_MARGIN, 1e-6f);  // Much larger than float epsilon
}

// ============================================================================
// Ray-AABB intersection tests
// ============================================================================

TEST(RayAABBTest, HitFromFront) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(-1.0f, 0.5f, 0.5f);  // In front of box
    Vec3 dir(1.0f, 0.0f, 0.0f);       // Toward box

    float tMin, tMax;
    Face hitFace;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, &tMax, &hitFace));
    EXPECT_NEAR(tMin, 1.0f, 0.001f);   // Hit at x=0, which is 1 unit away
    EXPECT_NEAR(tMax, 2.0f, 0.001f);   // Exit at x=1, which is 2 units away
    EXPECT_EQ(hitFace, Face::NegX);    // Hit the -X face of the box
}

TEST(RayAABBTest, HitFromBehind) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(2.0f, 0.5f, 0.5f);  // Behind box
    Vec3 dir(-1.0f, 0.0f, 0.0f);    // Toward box

    float tMin, tMax;
    Face hitFace;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, &tMax, &hitFace));
    EXPECT_NEAR(tMin, 1.0f, 0.001f);   // Hit at x=1, which is 1 unit away
    EXPECT_EQ(hitFace, Face::PosX);    // Hit the +X face of the box
}

TEST(RayAABBTest, Miss) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(-1.0f, 2.0f, 0.5f);  // Above and in front of box
    Vec3 dir(1.0f, 0.0f, 0.0f);      // Parallel, misses

    EXPECT_FALSE(box.rayIntersect(origin, dir));
}

TEST(RayAABBTest, InsideBox) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(0.5f, 0.5f, 0.5f);  // Inside box
    Vec3 dir(1.0f, 0.0f, 0.0f);

    float tMin, tMax;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, &tMax));
    EXPECT_LT(tMin, 0.0f);            // Entry is behind us
    EXPECT_GT(tMax, 0.0f);            // Exit is in front
}

TEST(RayAABBTest, BoxBehindRay) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(2.0f, 0.5f, 0.5f);  // Past box
    Vec3 dir(1.0f, 0.0f, 0.0f);     // Moving away

    EXPECT_FALSE(box.rayIntersect(origin, dir));
}

TEST(RayAABBTest, DiagonalHit) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(-1.0f, -1.0f, -1.0f);
    Vec3 dir = glm::normalize(Vec3(1.0f, 1.0f, 1.0f));

    float tMin, tMax;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, &tMax));
    EXPECT_GT(tMin, 0.0f);
    EXPECT_GT(tMax, tMin);
}

TEST(RayAABBTest, GrazingEdge) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(-1.0f, 0.0f, 0.0f);  // At edge level
    Vec3 dir(1.0f, 0.0f, 0.0f);

    float tMin, tMax;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, &tMax));
}

TEST(RayAABBTest, HitTopFace) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(0.5f, 2.0f, 0.5f);  // Above box
    Vec3 dir(0.0f, -1.0f, 0.0f);    // Straight down

    float tMin;
    Face hitFace;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, nullptr, &hitFace));
    EXPECT_NEAR(tMin, 1.0f, 0.001f);  // Hit at y=1, which is 1 unit away
    EXPECT_EQ(hitFace, Face::PosY);   // Hit top face
}

TEST(RayAABBTest, HitBottomFace) {
    AABB box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f);
    Vec3 origin(0.5f, -1.0f, 0.5f);  // Below box
    Vec3 dir(0.0f, 1.0f, 0.0f);      // Straight up

    float tMin;
    Face hitFace;
    EXPECT_TRUE(box.rayIntersect(origin, dir, &tMin, nullptr, &hitFace));
    EXPECT_NEAR(tMin, 1.0f, 0.001f);
    EXPECT_EQ(hitFace, Face::NegY);
}

// ============================================================================
// Raycast through blocks tests
// ============================================================================

// Helper: simple shape provider that returns FULL_BLOCK for specific positions
class SimpleBlockWorld {
public:
    void setBlock(const BlockPos& pos, bool solid) {
        if (solid) {
            solidBlocks_.insert(pos.pack());
        } else {
            solidBlocks_.erase(pos.pack());
        }
    }

    const CollisionShape* getShape(const BlockPos& pos, RaycastMode /*mode*/) const {
        if (solidBlocks_.count(pos.pack())) {
            return &CollisionShape::FULL_BLOCK;
        }
        return nullptr;
    }

private:
    std::unordered_set<uint64_t> solidBlocks_;
};

TEST(RaycastBlocksTest, HitSingleBlock) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(5, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, 5);
    EXPECT_EQ(result.blockPos.y, 0);
    EXPECT_EQ(result.blockPos.z, 0);
    EXPECT_EQ(result.face, Face::NegX);  // Hit the -X face of the block
    EXPECT_NEAR(result.distance, 4.5f, 0.01f);  // From 0.5 to 5.0
}

TEST(RaycastBlocksTest, MissEmptyWorld) {
    SimpleBlockWorld world;  // No blocks

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_FALSE(result.hit);
}

TEST(RaycastBlocksTest, MaxDistanceRespected) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(50, 0, 0), true);  // Far block

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 10.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_FALSE(result.hit);  // Block is beyond max distance
}

TEST(RaycastBlocksTest, HitClosestBlock) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(5, 0, 0), true);
    world.setBlock(BlockPos(10, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, 5);  // Should hit closer block
}

TEST(RaycastBlocksTest, DiagonalRay) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(5, 5, 5), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir = glm::normalize(Vec3(1.0f, 1.0f, 1.0f));

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, 5);
    EXPECT_EQ(result.blockPos.y, 5);
    EXPECT_EQ(result.blockPos.z, 5);
}

TEST(RaycastBlocksTest, DownwardRay) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(0, 0, 0), true);  // Ground block

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 5.0f, 0.5f);  // Above the block
    Vec3 dir(0.0f, -1.0f, 0.0f);    // Looking down

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, 0);
    EXPECT_EQ(result.blockPos.y, 0);
    EXPECT_EQ(result.blockPos.z, 0);
    EXPECT_EQ(result.face, Face::PosY);  // Hit top face
    EXPECT_NEAR(result.distance, 4.0f, 0.01f);  // From y=5 to y=1 (top of block)
}

TEST(RaycastBlocksTest, NegativeCoordinates) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(-5, -3, -2), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir = glm::normalize(Vec3(-5.0f, -3.0f, -2.0f));

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, -5);
    EXPECT_EQ(result.blockPos.y, -3);
    EXPECT_EQ(result.blockPos.z, -2);
}

TEST(RaycastBlocksTest, StartInsideBlock) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(0, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);  // Inside the block
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.blockPos.x, 0);
    EXPECT_EQ(result.blockPos.y, 0);
    EXPECT_EQ(result.blockPos.z, 0);
    EXPECT_NEAR(result.distance, 0.0f, 0.01f);  // Immediate hit
}

TEST(RaycastBlocksTest, HalfSlabTop) {
    // Test with non-full block shape
    SimpleBlockWorld world;
    // Use a custom provider for half slabs
    auto shapeProvider = [](const BlockPos& pos, RaycastMode /*mode*/) -> const CollisionShape* {
        if (pos.x == 5 && pos.y == 0 && pos.z == 0) {
            return &CollisionShape::HALF_SLAB_TOP;  // y: 0.5 to 1.0
        }
        return nullptr;
    };

    // Ray that would hit a full block but misses the top half
    Vec3 origin(0.5f, 0.25f, 0.5f);  // In the lower half
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    // Should miss because the slab is only in the top half
    EXPECT_FALSE(result.hit);

    // Ray that hits the top half
    Vec3 origin2(0.5f, 0.75f, 0.5f);  // In the upper half
    auto result2 = raycastBlocks(origin2, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result2.hit);
    EXPECT_EQ(result2.blockPos.x, 5);
}

TEST(RaycastBlocksTest, HitPointAccuracy) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(5, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    Vec3 origin(0.5f, 0.5f, 0.5f);
    Vec3 dir(1.0f, 0.0f, 0.0f);

    auto result = raycastBlocks(origin, dir, 100.0f, RaycastMode::Collision, shapeProvider);

    EXPECT_TRUE(result.hit);
    // Hit point should be on the face of the block
    EXPECT_NEAR(result.hitPoint.x, 5.0f, 0.01f);  // On the -X face at x=5
    EXPECT_NEAR(result.hitPoint.y, 0.5f, 0.01f);
    EXPECT_NEAR(result.hitPoint.z, 0.5f, 0.01f);
}

// ============================================================================
// PhysicsBody tests
// ============================================================================

TEST(PhysicsBodyTest, SimplePhysicsBodyConstruction) {
    SimplePhysicsBody body(Vec3(0.5f, 0.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));

    EXPECT_NEAR(body.position().x, 0.5f, 0.001f);
    EXPECT_NEAR(body.position().y, 0.0f, 0.001f);
    EXPECT_NEAR(body.position().z, 0.5f, 0.001f);

    EXPECT_NEAR(body.halfExtents().x, 0.3f, 0.001f);
    EXPECT_NEAR(body.halfExtents().y, 0.9f, 0.001f);
    EXPECT_NEAR(body.halfExtents().z, 0.3f, 0.001f);
}

TEST(PhysicsBodyTest, BoundingBoxCalculation) {
    SimplePhysicsBody body(Vec3(5.0f, 10.0f, 5.0f), Vec3(0.3f, 0.9f, 0.3f));

    AABB box = body.boundingBox();
    // Position is bottom-center, so:
    // min.x = 5.0 - 0.3 = 4.7
    // min.y = 10.0 (bottom)
    // min.z = 5.0 - 0.3 = 4.7
    // max.x = 5.0 + 0.3 = 5.3
    // max.y = 10.0 + 1.8 = 11.8 (height = halfExtents.y * 2)
    // max.z = 5.0 + 0.3 = 5.3

    EXPECT_NEAR(box.min.x, 4.7f, 0.001f);
    EXPECT_NEAR(box.min.y, 10.0f, 0.001f);
    EXPECT_NEAR(box.min.z, 4.7f, 0.001f);
    EXPECT_NEAR(box.max.x, 5.3f, 0.001f);
    EXPECT_NEAR(box.max.y, 11.8f, 0.001f);
    EXPECT_NEAR(box.max.z, 5.3f, 0.001f);
}

TEST(PhysicsBodyTest, VelocityAndPosition) {
    SimplePhysicsBody body(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.3f, 0.9f, 0.3f));

    body.setVelocity(Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(body.velocity().x, 1.0f, 0.001f);
    EXPECT_NEAR(body.velocity().y, 2.0f, 0.001f);
    EXPECT_NEAR(body.velocity().z, 3.0f, 0.001f);

    body.setPosition(Vec3(10.0f, 20.0f, 30.0f));
    EXPECT_NEAR(body.position().x, 10.0f, 0.001f);
    EXPECT_NEAR(body.position().y, 20.0f, 0.001f);
    EXPECT_NEAR(body.position().z, 30.0f, 0.001f);
}

TEST(PhysicsBodyTest, GroundState) {
    SimplePhysicsBody body(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.3f, 0.9f, 0.3f));

    EXPECT_FALSE(body.isOnGround());
    body.setOnGround(true);
    EXPECT_TRUE(body.isOnGround());
    body.setOnGround(false);
    EXPECT_FALSE(body.isOnGround());
}

// ============================================================================
// PhysicsSystem tests
// ============================================================================

TEST(PhysicsSystemTest, MoveInEmptyWorld) {
    auto shapeProvider = [](const BlockPos&, RaycastMode) -> const CollisionShape* {
        return nullptr;  // No blocks
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(0.5f, 5.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));

    Vec3 movement = physics.moveBody(body, Vec3(1.0f, 0.0f, 0.0f));

    EXPECT_NEAR(movement.x, 1.0f, 0.01f);  // Full movement
    EXPECT_NEAR(body.position().x, 1.5f, 0.01f);
}

TEST(PhysicsSystemTest, BlocksMovement) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(2, 5, 0), true);  // Block in the way

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(0.5f, 5.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));

    // Try to move into the block
    Vec3 movement = physics.moveBody(body, Vec3(5.0f, 0.0f, 0.0f));

    // Should stop before the block (at x=2 - 0.3 - margin)
    EXPECT_LT(body.position().x, 2.0f);
    EXPECT_LT(movement.x, 5.0f);
}

TEST(PhysicsSystemTest, FallsWithGravity) {
    auto shapeProvider = [](const BlockPos&, RaycastMode) -> const CollisionShape* {
        return nullptr;  // No blocks
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(0.5f, 10.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));

    float dt = 0.1f;
    physics.applyGravity(body, dt);

    // Velocity should be negative (falling)
    EXPECT_LT(body.velocity().y, 0.0f);
    EXPECT_NEAR(body.velocity().y, -DEFAULT_GRAVITY * dt, 0.01f);
}

TEST(PhysicsSystemTest, LandsOnGround) {
    SimpleBlockWorld world;
    // Create a floor
    for (int x = -2; x <= 2; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    // Body starts above ground
    SimplePhysicsBody body(Vec3(0.5f, 2.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));

    // Apply downward movement
    Vec3 movement = physics.moveBody(body, Vec3(0.0f, -5.0f, 0.0f));

    // Should land on the block at y=1
    EXPECT_NEAR(body.position().y, 1.0f, 0.01f);
    EXPECT_TRUE(body.isOnGround());
}

TEST(PhysicsSystemTest, WalksOnGround) {
    SimpleBlockWorld world;
    // Create a floor
    for (int x = -5; x <= 10; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(0.5f, 1.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));
    body.setOnGround(true);

    // Walk forward
    Vec3 movement = physics.moveBody(body, Vec3(5.0f, 0.0f, 0.0f));

    EXPECT_NEAR(movement.x, 5.0f, 0.01f);
    EXPECT_NEAR(body.position().x, 5.5f, 0.01f);
    EXPECT_NEAR(body.position().y, 1.0f, 0.01f);  // Stays on ground
}

TEST(PhysicsSystemTest, StepClimbing) {
    SimpleBlockWorld world;
    // Create a floor at y=0
    for (int x = -2; x <= 5; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }
    // Create a half-block step at x=3 (using bottom slab shape)
    // Since we only have FULL_BLOCK in SimpleBlockWorld, use a custom provider

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) -> const CollisionShape* {
        // Step block at x=3, y=1 - make it a half slab (0.5 blocks high)
        if (pos.x == 3 && pos.y == 1 && pos.z == 0) {
            return &CollisionShape::HALF_SLAB_BOTTOM;  // 0 to 0.5 height
        }
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    // Body at y=1 (standing on floor at y=0, floor top is y=1)
    // Half-extents (0.3, 0.5, 0.3) = 1 block tall
    SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));
    body.setOnGround(true);

    // Walk toward the step (half slab at y=1 to y=1.5)
    Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

    // Should step up onto the half slab
    EXPECT_GT(body.position().x, 2.5f);  // Made horizontal progress
    EXPECT_GT(body.position().y, 1.0f);  // Stepped up (at least a bit)
}

TEST(PhysicsSystemTest, CantClimbTooHigh) {
    SimpleBlockWorld world;
    // Create a floor
    for (int x = -2; x <= 5; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }
    // Create a wall (too high to step over)
    world.setBlock(BlockPos(3, 1, 0), true);
    world.setBlock(BlockPos(3, 2, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));
    body.setOnGround(true);

    // Walk toward the wall
    Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

    // Should be blocked by the wall
    EXPECT_LT(body.position().x, 3.0f);
    EXPECT_NEAR(body.position().y, 1.0f, 0.01f);  // Still on ground level
}

TEST(PhysicsSystemTest, CheckOnGround) {
    SimpleBlockWorld world;
    world.setBlock(BlockPos(0, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);

    // Body standing on block
    SimplePhysicsBody onBlock(Vec3(0.5f, 1.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));
    EXPECT_TRUE(physics.checkOnGround(onBlock));

    // Body floating in air
    SimplePhysicsBody inAir(Vec3(0.5f, 5.0f, 0.5f), Vec3(0.3f, 0.9f, 0.3f));
    EXPECT_FALSE(physics.checkOnGround(inAir));
}

TEST(PhysicsSystemTest, GravityConfiguration) {
    auto shapeProvider = [](const BlockPos&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };

    PhysicsSystem physics(shapeProvider);

    EXPECT_NEAR(physics.gravity(), DEFAULT_GRAVITY, 0.01f);

    physics.setGravity(10.0f);
    EXPECT_NEAR(physics.gravity(), 10.0f, 0.01f);
}

TEST(PhysicsSystemTest, UpdateIntegration) {
    SimpleBlockWorld world;
    // Create a floor
    world.setBlock(BlockPos(0, 0, 0), true);
    world.setBlock(BlockPos(1, 0, 0), true);
    world.setBlock(BlockPos(-1, 0, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(0.5f, 5.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));

    // Simulate falling
    for (int i = 0; i < 100; ++i) {
        physics.update(body, 0.016f);  // ~60 FPS
        if (body.isOnGround()) break;
    }

    // Should have landed
    EXPECT_TRUE(body.isOnGround());
    EXPECT_NEAR(body.position().y, 1.0f, 0.1f);
}

// ============================================================================
// Per-body configurable step height tests
// ============================================================================

TEST(PhysicsBodyTest, MaxStepHeightDefault) {
    SimplePhysicsBody body(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.3f, 0.9f, 0.3f));
    EXPECT_NEAR(body.maxStepHeight(), MAX_STEP_HEIGHT, 0.001f);
}

TEST(PhysicsBodyTest, MaxStepHeightConfigurable) {
    SimplePhysicsBody body(Vec3(0.0f, 0.0f, 0.0f), Vec3(0.3f, 0.9f, 0.3f));

    body.setMaxStepHeight(1.0f);  // Full block stepping (like Hytale)
    EXPECT_NEAR(body.maxStepHeight(), 1.0f, 0.001f);

    body.setMaxStepHeight(0.5f);  // Half block stepping
    EXPECT_NEAR(body.maxStepHeight(), 0.5f, 0.001f);
}

TEST(PhysicsSystemTest, PerBodyStepHeightHigherAllowsHigherStep) {
    // Test that a body with higher maxStepHeight can climb a step that
    // a body with lower maxStepHeight cannot
    SimpleBlockWorld world;
    // Create a floor at y=0
    for (int x = -2; x <= 10; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }

    // Use a half slab (0.5 tall) at x=3, y=1 - this is climbable with default step height
    // This is the same setup as the passing StepClimbing test
    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) -> const CollisionShape* {
        if (pos.x == 3 && pos.y == 1 && pos.z == 0) {
            return &CollisionShape::HALF_SLAB_BOTTOM;  // 0 to 0.5 height
        }
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);

    // Test 1: Body with LOW step height (0.3) - should NOT be able to step up 0.5 blocks
    {
        SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));
        body.setOnGround(true);
        body.setMaxStepHeight(0.3f);  // Can only step 0.3 blocks (less than 0.5 slab)

        Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

        // Should NOT step up - blocked by the half slab
        EXPECT_LT(body.position().x, 3.0f);  // Blocked
        EXPECT_NEAR(body.position().y, 1.0f, 0.1f);  // Still at ground level
    }

    // Test 2: Body with HIGH step height (0.6) - should be able to step up 0.5 blocks
    {
        SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));
        body.setOnGround(true);
        body.setMaxStepHeight(0.6f);  // Can step 0.6 blocks (more than 0.5 slab)

        Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

        // Should step up onto the half slab
        EXPECT_GT(body.position().x, 2.5f);  // Made horizontal progress
        EXPECT_GT(body.position().y, 1.0f);  // Stepped up
    }
}

TEST(PhysicsSystemTest, PerBodyStepHeightLimited) {
    SimpleBlockWorld world;
    // Create a floor at y=0
    for (int x = -2; x <= 5; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }
    // Create a full block step at x=3, y=1
    world.setBlock(BlockPos(3, 1, 0), true);

    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) {
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    // Small body that can fit
    SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));
    body.setOnGround(true);
    body.setMaxStepHeight(0.5f);  // Can only step half a block

    // Walk toward the full block step
    Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

    // Should NOT be able to step up (full block is too high)
    EXPECT_LT(body.position().x, 3.0f);  // Blocked by the wall
    EXPECT_NEAR(body.position().y, 1.0f, 0.1f);  // Still on ground level
}

TEST(PhysicsSystemTest, PerBodyStepHeightZeroDisablesStep) {
    SimpleBlockWorld world;
    // Create a floor at y=0
    for (int x = -2; x <= 5; ++x) {
        for (int z = -2; z <= 2; ++z) {
            world.setBlock(BlockPos(x, 0, z), true);
        }
    }

    // Use a custom shape provider for half-slab
    auto shapeProvider = [&world](const BlockPos& pos, RaycastMode mode) -> const CollisionShape* {
        // Half slab step at x=3, y=1
        if (pos.x == 3 && pos.y == 1 && pos.z == 0) {
            return &CollisionShape::HALF_SLAB_BOTTOM;
        }
        return world.getShape(pos, mode);
    };

    PhysicsSystem physics(shapeProvider);
    SimplePhysicsBody body(Vec3(1.5f, 1.0f, 0.5f), Vec3(0.3f, 0.5f, 0.3f));
    body.setOnGround(true);
    body.setMaxStepHeight(0.0f);  // No stepping at all

    // Walk toward the half slab step
    Vec3 movement = physics.moveBody(body, Vec3(3.0f, 0.0f, 0.0f));

    // Should NOT step up even over a half slab
    EXPECT_LT(body.position().x, 3.0f);  // Blocked
    EXPECT_NEAR(body.position().y, 1.0f, 0.1f);  // Still on ground level
}
