#include <gtest/gtest.h>
#include "finevox/physics.hpp"
#include <cmath>

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
