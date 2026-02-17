#include <gtest/gtest.h>
#include "finevox/core/entity_mesh.hpp"
#include "finevox/core/entity_render_state.hpp"
#include "finevox/core/skeleton.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

using namespace finevox;

// ============================================================================
// EntityVertex Tests
// ============================================================================

TEST(EntityVertexTest, DefaultValues) {
    EntityVertex v{};
    EXPECT_FLOAT_EQ(v.position.x, 0.0f);
    EXPECT_FLOAT_EQ(v.normal.x, 0.0f);
    EXPECT_FLOAT_EQ(v.texCoord.x, 0.0f);
}

// ============================================================================
// EntityMeshPart Tests
// ============================================================================

TEST(EntityMeshPartTest, DefaultValues) {
    EntityMeshPart part;
    EXPECT_EQ(part.boneIndex, 0);
    EXPECT_FLOAT_EQ(part.offset.x, 0.0f);
    EXPECT_FLOAT_EQ(part.size.x, 1.0f);
    EXPECT_FLOAT_EQ(part.uvOffset.x, 0.0f);
    EXPECT_FLOAT_EQ(part.uvSize.x, 1.0f);
}

// ============================================================================
// EntityMesh Tests
// ============================================================================

TEST(EntityMeshTest, EmptyMesh) {
    EntityMesh mesh;
    EXPECT_TRUE(mesh.isEmpty());
    EXPECT_EQ(mesh.partCount(), 0u);

    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    std::vector<glm::mat4> poses;
    mesh.buildVertices(poses, verts, indices);

    EXPECT_TRUE(verts.empty());
    EXPECT_TRUE(indices.empty());
}

TEST(EntityMeshTest, SinglePartGeneratesBox) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.boneIndex = 0;
    part.offset = glm::vec3(0, 0, 0);
    part.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(part);

    // Identity pose
    std::vector<glm::mat4> poses = { glm::mat4(1.0f) };
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    // 6 faces × 4 verts = 24 vertices
    EXPECT_EQ(verts.size(), 24u);
    // 6 faces × 2 triangles × 3 indices = 36 indices
    EXPECT_EQ(indices.size(), 36u);
}

TEST(EntityMeshTest, TwoPartsDoubleGeometry) {
    EntityMesh mesh;
    EntityMeshPart p1;
    p1.boneIndex = 0;
    p1.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(p1);

    EntityMeshPart p2;
    p2.boneIndex = 0;
    p2.offset = glm::vec3(0, 2, 0);
    p2.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(p2);

    std::vector<glm::mat4> poses = { glm::mat4(1.0f) };
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    EXPECT_EQ(verts.size(), 48u);   // 24 * 2
    EXPECT_EQ(indices.size(), 72u); // 36 * 2
}

TEST(EntityMeshTest, BoneTransformApplied) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.boneIndex = 0;
    part.offset = glm::vec3(0, 0, 0);
    part.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(part);

    // Translate bone by (5, 0, 0)
    glm::mat4 translate = glm::translate(glm::mat4(1.0f), glm::vec3(5, 0, 0));
    std::vector<glm::mat4> poses = { translate };
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    // All vertices should be centered around x=5
    float sumX = 0;
    for (const auto& v : verts) {
        sumX += v.position.x;
    }
    float avgX = sumX / static_cast<float>(verts.size());
    EXPECT_NEAR(avgX, 5.0f, 0.01f);
}

TEST(EntityMeshTest, PartOnDifferentBones) {
    EntityMesh mesh;

    EntityMeshPart p1;
    p1.boneIndex = 0;
    p1.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(p1);

    EntityMeshPart p2;
    p2.boneIndex = 1;
    p2.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(p2);

    // Bone 0 at origin, bone 1 at (10, 0, 0)
    std::vector<glm::mat4> poses = {
        glm::mat4(1.0f),
        glm::translate(glm::mat4(1.0f), glm::vec3(10, 0, 0)),
    };

    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    // First 24 verts: centered at origin
    float sumX1 = 0;
    for (size_t i = 0; i < 24; ++i) sumX1 += verts[i].position.x;
    EXPECT_NEAR(sumX1 / 24.0f, 0.0f, 0.01f);

    // Next 24 verts: centered at (10, 0, 0)
    float sumX2 = 0;
    for (size_t i = 24; i < 48; ++i) sumX2 += verts[i].position.x;
    EXPECT_NEAR(sumX2 / 24.0f, 10.0f, 0.01f);
}

TEST(EntityMeshTest, NormalsAreNormalized) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.boneIndex = 0;
    part.size = glm::vec3(2, 3, 4);  // Non-uniform scale
    mesh.parts.push_back(part);

    std::vector<glm::mat4> poses = { glm::mat4(1.0f) };
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    for (const auto& v : verts) {
        float len = glm::length(v.normal);
        EXPECT_NEAR(len, 1.0f, 0.01f);
    }
}

TEST(EntityMeshTest, UVsWithinRange) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.boneIndex = 0;
    part.uvOffset = glm::vec2(0.25f, 0.5f);
    part.uvSize = glm::vec2(0.1f, 0.1f);
    mesh.parts.push_back(part);

    std::vector<glm::mat4> poses = { glm::mat4(1.0f) };
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    for (const auto& v : verts) {
        EXPECT_GE(v.texCoord.x, 0.25f - 0.001f);
        EXPECT_LE(v.texCoord.x, 0.35f + 0.001f);
        EXPECT_GE(v.texCoord.y, 0.5f - 0.001f);
        EXPECT_LE(v.texCoord.y, 0.6f + 0.001f);
    }
}

TEST(EntityMeshTest, InvalidBoneIndexUsesIdentity) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.boneIndex = 99;  // Out of range
    part.size = glm::vec3(1, 1, 1);
    mesh.parts.push_back(part);

    std::vector<glm::mat4> poses = { glm::mat4(1.0f) };  // Only 1 bone
    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(poses, verts, indices);

    // Should still generate vertices (using identity transform)
    EXPECT_EQ(verts.size(), 24u);

    // Center should be at origin
    float sumX = 0;
    for (const auto& v : verts) sumX += v.position.x;
    EXPECT_NEAR(sumX / 24.0f, 0.0f, 0.01f);
}

TEST(EntityMeshTest, WithSkeletonPoses) {
    // Build a simple skeleton
    Skeleton skel;
    Bone root; root.name = "root"; root.parentIndex = -1;
    root.restPosition = glm::vec3(0, 1, 0);
    skel.addBone(std::move(root));

    Bone arm; arm.name = "arm"; arm.parentIndex = 0;
    arm.restPosition = glm::vec3(1, 0, 0);
    skel.addBone(std::move(arm));

    // Compute poses
    std::vector<glm::mat4> local, world;
    skel.computeRestPose(local);
    skel.computeWorldTransforms(local, world);

    // Build mesh
    EntityMesh mesh;
    EntityMeshPart body;
    body.boneIndex = 0;
    body.size = glm::vec3(0.5f, 1.0f, 0.3f);
    mesh.parts.push_back(body);

    EntityMeshPart armPart;
    armPart.boneIndex = 1;
    armPart.size = glm::vec3(0.2f, 0.6f, 0.2f);
    mesh.parts.push_back(armPart);

    std::vector<EntityVertex> verts;
    std::vector<uint32_t> indices;
    mesh.buildVertices(world, verts, indices);

    EXPECT_EQ(verts.size(), 48u);
    EXPECT_EQ(indices.size(), 72u);

    // Body verts should be near (0, 1, 0) from root bone
    float bodyAvgY = 0;
    for (size_t i = 0; i < 24; ++i) bodyAvgY += verts[i].position.y;
    EXPECT_NEAR(bodyAvgY / 24.0f, 1.0f, 0.01f);

    // Arm verts should be near (1, 1, 0) from root + arm offset
    float armAvgX = 0, armAvgY = 0;
    for (size_t i = 24; i < 48; ++i) {
        armAvgX += verts[i].position.x;
        armAvgY += verts[i].position.y;
    }
    EXPECT_NEAR(armAvgX / 24.0f, 1.0f, 0.01f);
    EXPECT_NEAR(armAvgY / 24.0f, 1.0f, 0.01f);
}

// ============================================================================
// EntityRenderState Tests
// ============================================================================

TEST(EntityRenderStateTest, DefaultValues) {
    EntityRenderState state;
    EXPECT_EQ(state.id, INVALID_ENTITY_ID);
    EXPECT_TRUE(state.visible);
    EXPECT_TRUE(state.active);
    EXPECT_FLOAT_EQ(state.prevYaw, 0.0f);
    EXPECT_FLOAT_EQ(state.currentYaw, 0.0f);
}

TEST(EntityRenderStateTest, InterpolatePositionMidpoint) {
    EntityRenderState state;
    state.prevPosition = glm::dvec3(0, 0, 0);
    state.currentPosition = glm::dvec3(10, 20, 30);

    auto mid = state.interpolatedPosition(0.5f);
    EXPECT_NEAR(mid.x, 5.0, 0.01);
    EXPECT_NEAR(mid.y, 10.0, 0.01);
    EXPECT_NEAR(mid.z, 15.0, 0.01);
}

TEST(EntityRenderStateTest, InterpolatePositionStart) {
    EntityRenderState state;
    state.prevPosition = glm::dvec3(1, 2, 3);
    state.currentPosition = glm::dvec3(4, 5, 6);

    auto start = state.interpolatedPosition(0.0f);
    EXPECT_NEAR(start.x, 1.0, 0.01);
    EXPECT_NEAR(start.y, 2.0, 0.01);
    EXPECT_NEAR(start.z, 3.0, 0.01);
}

TEST(EntityRenderStateTest, InterpolatePositionEnd) {
    EntityRenderState state;
    state.prevPosition = glm::dvec3(1, 2, 3);
    state.currentPosition = glm::dvec3(4, 5, 6);

    auto end = state.interpolatedPosition(1.0f);
    EXPECT_NEAR(end.x, 4.0, 0.01);
    EXPECT_NEAR(end.y, 5.0, 0.01);
    EXPECT_NEAR(end.z, 6.0, 0.01);
}

TEST(EntityRenderStateTest, InterpolatePitch) {
    EntityRenderState state;
    state.prevPitch = -30.0f;
    state.currentPitch = 30.0f;

    EXPECT_NEAR(state.interpolatedPitch(0.5f), 0.0f, 0.01f);
    EXPECT_NEAR(state.interpolatedPitch(0.0f), -30.0f, 0.01f);
    EXPECT_NEAR(state.interpolatedPitch(1.0f), 30.0f, 0.01f);
}

TEST(EntityRenderStateTest, InterpolateYawNoWrapping) {
    EntityRenderState state;
    state.prevYaw = 10.0f;
    state.currentYaw = 20.0f;

    EXPECT_NEAR(state.interpolatedYaw(0.5f), 15.0f, 0.01f);
}

TEST(EntityRenderStateTest, InterpolateYawWrapping) {
    EntityRenderState state;
    state.prevYaw = 350.0f;
    state.currentYaw = 10.0f;

    // Should go 350 -> 360/0 -> 10, not 350 -> 180 -> 10
    float mid = state.interpolatedYaw(0.5f);
    // Midpoint of 350→10 wrapping should be 360/0
    EXPECT_NEAR(mid, 360.0f, 0.01f);
}

TEST(EntityRenderStateTest, InterpolateYawWrappingReverse) {
    EntityRenderState state;
    state.prevYaw = 10.0f;
    state.currentYaw = 350.0f;

    // Should go 10 -> 0/360 -> 350, not 10 -> 180 -> 350
    float mid = state.interpolatedYaw(0.5f);
    EXPECT_NEAR(mid, 0.0f, 0.01f);
}

TEST(EntityRenderStateTest, ApplySnapshot) {
    EntityRenderState state;
    state.currentPosition = glm::dvec3(1, 2, 3);
    state.currentYaw = 45.0f;
    state.currentPitch = -10.0f;

    EntityState snapshot;
    snapshot.position = glm::dvec3(4, 5, 6);
    snapshot.yaw = 90.0f;
    snapshot.pitch = 15.0f;
    state.applySnapshot(snapshot);

    // Previous should be old current
    EXPECT_NEAR(state.prevPosition.x, 1.0, 0.01);
    EXPECT_NEAR(state.prevYaw, 45.0f, 0.01f);
    EXPECT_NEAR(state.prevPitch, -10.0f, 0.01f);

    // Current should be new snapshot
    EXPECT_NEAR(state.currentPosition.x, 4.0, 0.01);
    EXPECT_NEAR(state.currentYaw, 90.0f, 0.01f);
    EXPECT_NEAR(state.currentPitch, 15.0f, 0.01f);
}

TEST(EntityRenderStateTest, MultipleSnapshots) {
    EntityRenderState state;

    EntityState snap1;
    snap1.position = glm::dvec3(1, 0, 0);
    snap1.yaw = 0.0f;
    snap1.pitch = 0.0f;
    state.applySnapshot(snap1);

    EntityState snap2;
    snap2.position = glm::dvec3(2, 0, 0);
    snap2.yaw = 90.0f;
    snap2.pitch = 0.0f;
    state.applySnapshot(snap2);

    // Previous should be snap1, current should be snap2
    EXPECT_NEAR(state.prevPosition.x, 1.0, 0.01);
    EXPECT_NEAR(state.currentPosition.x, 2.0, 0.01);
    EXPECT_NEAR(state.prevYaw, 0.0f, 0.01f);
    EXPECT_NEAR(state.currentYaw, 90.0f, 0.01f);

    // Interpolation should work
    auto mid = state.interpolatedPosition(0.5f);
    EXPECT_NEAR(mid.x, 1.5, 0.01);
}
