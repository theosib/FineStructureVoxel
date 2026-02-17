#include <gtest/gtest.h>
#include "finevox/core/skeleton.hpp"
#include "finevox/core/animation_clip.hpp"
#include "finevox/core/animation_controller.hpp"
#include "finevox/core/skeleton_loader.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <cmath>

using namespace finevox;

// ============================================================================
// Bone Tests
// ============================================================================

TEST(BoneTest, DefaultValues) {
    Bone b;
    EXPECT_TRUE(b.name.empty());
    EXPECT_EQ(b.parentIndex, -1);
    EXPECT_FLOAT_EQ(b.restPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(b.restPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(b.restPosition.z, 0.0f);
    EXPECT_FLOAT_EQ(b.restScale.x, 1.0f);
    EXPECT_FLOAT_EQ(b.restScale.y, 1.0f);
    EXPECT_FLOAT_EQ(b.restScale.z, 1.0f);
    // Identity quaternion
    EXPECT_FLOAT_EQ(b.restRotation.w, 1.0f);
    EXPECT_FLOAT_EQ(b.restRotation.x, 0.0f);
    EXPECT_FLOAT_EQ(b.restRotation.y, 0.0f);
    EXPECT_FLOAT_EQ(b.restRotation.z, 0.0f);
}

// ============================================================================
// Skeleton Tests
// ============================================================================

TEST(SkeletonTest, EmptyByDefault) {
    Skeleton skel;
    EXPECT_TRUE(skel.isEmpty());
    EXPECT_EQ(skel.boneCount(), 0);
}

TEST(SkeletonTest, AddBone) {
    Skeleton skel;
    Bone root;
    root.name = "root";
    root.restPosition = glm::vec3(0, 1, 0);
    skel.addBone(std::move(root));

    EXPECT_FALSE(skel.isEmpty());
    EXPECT_EQ(skel.boneCount(), 1);
    EXPECT_EQ(skel.bone(0).name, "root");
    EXPECT_FLOAT_EQ(skel.bone(0).restPosition.y, 1.0f);
}

TEST(SkeletonTest, FindBone) {
    Skeleton skel;
    Bone b1; b1.name = "body";
    Bone b2; b2.name = "head";
    skel.addBone(std::move(b1));
    skel.addBone(std::move(b2));

    EXPECT_EQ(skel.findBone("body"), 0);
    EXPECT_EQ(skel.findBone("head"), 1);
    EXPECT_EQ(skel.findBone("missing"), -1);
}

TEST(SkeletonTest, ParentHierarchy) {
    Skeleton skel;
    Bone root; root.name = "body"; root.parentIndex = -1;
    Bone head; head.name = "head"; head.parentIndex = 0;
    Bone arm;  arm.name = "arm";  arm.parentIndex = 0;
    skel.addBone(std::move(root));
    skel.addBone(std::move(head));
    skel.addBone(std::move(arm));

    EXPECT_EQ(skel.bone(0).parentIndex, -1);
    EXPECT_EQ(skel.bone(1).parentIndex, 0);
    EXPECT_EQ(skel.bone(2).parentIndex, 0);
}

TEST(SkeletonTest, ComputeRestPose) {
    Skeleton skel;
    Bone root;
    root.name = "root";
    root.restPosition = glm::vec3(1, 2, 3);
    skel.addBone(std::move(root));

    std::vector<glm::mat4> poses;
    skel.computeRestPose(poses);
    ASSERT_EQ(poses.size(), 1u);

    // Rest pose should be a translation matrix for (1,2,3)
    EXPECT_FLOAT_EQ(poses[0][3][0], 1.0f);
    EXPECT_FLOAT_EQ(poses[0][3][1], 2.0f);
    EXPECT_FLOAT_EQ(poses[0][3][2], 3.0f);
}

TEST(SkeletonTest, ComputeWorldTransforms) {
    Skeleton skel;
    Bone root;
    root.name = "root";
    root.restPosition = glm::vec3(0, 1, 0);
    root.parentIndex = -1;
    skel.addBone(std::move(root));

    Bone child;
    child.name = "child";
    child.restPosition = glm::vec3(0, 0.5f, 0);
    child.parentIndex = 0;
    skel.addBone(std::move(child));

    std::vector<glm::mat4> local;
    skel.computeRestPose(local);

    std::vector<glm::mat4> world;
    skel.computeWorldTransforms(local, world);

    ASSERT_EQ(world.size(), 2u);

    // Root: world = local translation (0, 1, 0)
    EXPECT_FLOAT_EQ(world[0][3][0], 0.0f);
    EXPECT_FLOAT_EQ(world[0][3][1], 1.0f);
    EXPECT_FLOAT_EQ(world[0][3][2], 0.0f);

    // Child: world = root * child = (0, 1.5, 0)
    EXPECT_FLOAT_EQ(world[1][3][0], 0.0f);
    EXPECT_FLOAT_EQ(world[1][3][1], 1.5f);
    EXPECT_FLOAT_EQ(world[1][3][2], 0.0f);
}

TEST(SkeletonTest, WorldTransformsThreeLevels) {
    Skeleton skel;
    Bone root;  root.name = "root";  root.parentIndex = -1;
    root.restPosition = glm::vec3(0, 1, 0);
    skel.addBone(std::move(root));

    Bone mid;   mid.name = "mid";    mid.parentIndex = 0;
    mid.restPosition = glm::vec3(0, 2, 0);
    skel.addBone(std::move(mid));

    Bone tip;   tip.name = "tip";    tip.parentIndex = 1;
    tip.restPosition = glm::vec3(0, 3, 0);
    skel.addBone(std::move(tip));

    std::vector<glm::mat4> local, world;
    skel.computeRestPose(local);
    skel.computeWorldTransforms(local, world);

    // tip world Y = 1 + 2 + 3 = 6
    EXPECT_FLOAT_EQ(world[2][3][1], 6.0f);
}

// ============================================================================
// Keyframe Tests
// ============================================================================

TEST(KeyframeTest, DefaultValues) {
    Keyframe kf;
    EXPECT_FLOAT_EQ(kf.time, 0.0f);
    EXPECT_FLOAT_EQ(kf.position.x, 0.0f);
    EXPECT_FLOAT_EQ(kf.scale.x, 1.0f);
    EXPECT_FLOAT_EQ(kf.rotation.w, 1.0f);
}

// ============================================================================
// BoneTrack Tests
// ============================================================================

TEST(BoneTrackTest, EmptyTrack) {
    BoneTrack track;
    EXPECT_TRUE(track.isEmpty());
    EXPECT_FLOAT_EQ(track.duration(), 0.0f);

    auto kf = track.sample(0.5f);
    EXPECT_FLOAT_EQ(kf.time, 0.0f);
}

TEST(BoneTrackTest, SingleKeyframe) {
    BoneTrack track;
    track.boneIndex = 0;
    Keyframe kf;
    kf.time = 0.0f;
    kf.position = glm::vec3(1, 2, 3);
    track.keyframes.push_back(kf);

    auto result = track.sample(0.0f);
    EXPECT_FLOAT_EQ(result.position.x, 1.0f);
    EXPECT_FLOAT_EQ(result.position.y, 2.0f);
    EXPECT_FLOAT_EQ(result.position.z, 3.0f);

    // Sampling beyond single keyframe returns same value
    auto result2 = track.sample(1.0f);
    EXPECT_FLOAT_EQ(result2.position.x, 1.0f);
}

TEST(BoneTrackTest, TwoKeyframeInterpolation) {
    BoneTrack track;
    track.boneIndex = 0;

    Keyframe kf0;
    kf0.time = 0.0f;
    kf0.position = glm::vec3(0, 0, 0);
    track.keyframes.push_back(kf0);

    Keyframe kf1;
    kf1.time = 1.0f;
    kf1.position = glm::vec3(10, 20, 30);
    track.keyframes.push_back(kf1);

    // Midpoint
    auto mid = track.sample(0.5f);
    EXPECT_NEAR(mid.position.x, 5.0f, 0.01f);
    EXPECT_NEAR(mid.position.y, 10.0f, 0.01f);
    EXPECT_NEAR(mid.position.z, 15.0f, 0.01f);

    // At start
    auto start = track.sample(0.0f);
    EXPECT_FLOAT_EQ(start.position.x, 0.0f);

    // At end
    auto end = track.sample(1.0f);
    EXPECT_FLOAT_EQ(end.position.x, 10.0f);
}

TEST(BoneTrackTest, ClampBeforeFirst) {
    BoneTrack track;
    track.boneIndex = 0;

    Keyframe kf0; kf0.time = 0.5f; kf0.position = glm::vec3(5, 0, 0);
    Keyframe kf1; kf1.time = 1.0f; kf1.position = glm::vec3(10, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);

    auto result = track.sample(0.0f);
    EXPECT_FLOAT_EQ(result.position.x, 5.0f);
}

TEST(BoneTrackTest, ClampAfterLast) {
    BoneTrack track;
    track.boneIndex = 0;

    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(0, 0, 0);
    Keyframe kf1; kf1.time = 1.0f; kf1.position = glm::vec3(10, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);

    auto result = track.sample(2.0f);
    EXPECT_FLOAT_EQ(result.position.x, 10.0f);
}

TEST(BoneTrackTest, RotationSlerp) {
    BoneTrack track;
    track.boneIndex = 0;

    Keyframe kf0;
    kf0.time = 0.0f;
    kf0.rotation = glm::quat(1, 0, 0, 0); // identity
    track.keyframes.push_back(kf0);

    Keyframe kf1;
    kf1.time = 1.0f;
    // 90 degrees around Y axis
    float angle = glm::radians(90.0f);
    kf1.rotation = glm::angleAxis(angle, glm::vec3(0, 1, 0));
    track.keyframes.push_back(kf1);

    // Midpoint should be ~45 degrees
    auto mid = track.sample(0.5f);
    // The rotation quaternion should be valid (unit length)
    float len = glm::length(mid.rotation);
    EXPECT_NEAR(len, 1.0f, 0.01f);
}

TEST(BoneTrackTest, Duration) {
    BoneTrack track;
    Keyframe kf0; kf0.time = 0.0f;
    Keyframe kf1; kf1.time = 2.5f;
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);

    EXPECT_FLOAT_EQ(track.duration(), 2.5f);
}

TEST(BoneTrackTest, ThreeKeyframeInterpolation) {
    BoneTrack track;
    track.boneIndex = 0;

    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(0, 0, 0);
    Keyframe kf1; kf1.time = 1.0f; kf1.position = glm::vec3(10, 0, 0);
    Keyframe kf2; kf2.time = 2.0f; kf2.position = glm::vec3(10, 10, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);
    track.keyframes.push_back(kf2);

    // Between first and second
    auto r1 = track.sample(0.5f);
    EXPECT_NEAR(r1.position.x, 5.0f, 0.01f);
    EXPECT_NEAR(r1.position.y, 0.0f, 0.01f);

    // Between second and third
    auto r2 = track.sample(1.5f);
    EXPECT_NEAR(r2.position.x, 10.0f, 0.01f);
    EXPECT_NEAR(r2.position.y, 5.0f, 0.01f);
}

// ============================================================================
// AnimationClip Tests
// ============================================================================

TEST(AnimationClipTest, EmptyClip) {
    AnimationClip clip;
    EXPECT_TRUE(clip.isEmpty());
    EXPECT_FLOAT_EQ(clip.duration, 0.0f);
}

TEST(AnimationClipTest, SampleWritesPoses) {
    AnimationClip clip;
    clip.duration = 1.0f;

    BoneTrack track;
    track.boneIndex = 0;
    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(1, 0, 0);
    Keyframe kf1; kf1.time = 1.0f; kf1.position = glm::vec3(3, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);
    clip.tracks.push_back(std::move(track));

    std::vector<glm::mat4> poses(2, glm::mat4(1.0f));
    clip.sample(0.5f, poses);

    // Bone 0 should have translation (2, 0, 0)
    EXPECT_NEAR(poses[0][3][0], 2.0f, 0.01f);
    // Bone 1 should be unchanged (identity)
    EXPECT_FLOAT_EQ(poses[1][3][0], 0.0f);
}

TEST(AnimationClipTest, LoopingWraps) {
    AnimationClip clip;
    clip.duration = 2.0f;
    clip.looping = true;

    BoneTrack track;
    track.boneIndex = 0;
    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(0, 0, 0);
    Keyframe kf1; kf1.time = 2.0f; kf1.position = glm::vec3(10, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);
    clip.tracks.push_back(std::move(track));

    std::vector<glm::mat4> poses(1, glm::mat4(1.0f));

    // At time=3.0 with duration=2.0, loops to time=1.0 => position (5,0,0)
    clip.sample(3.0f, poses);
    EXPECT_NEAR(poses[0][3][0], 5.0f, 0.01f);
}

TEST(AnimationClipTest, NonLoopingClamps) {
    AnimationClip clip;
    clip.duration = 2.0f;
    clip.looping = false;

    BoneTrack track;
    track.boneIndex = 0;
    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(0, 0, 0);
    Keyframe kf1; kf1.time = 2.0f; kf1.position = glm::vec3(10, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);
    clip.tracks.push_back(std::move(track));

    std::vector<glm::mat4> poses(1, glm::mat4(1.0f));

    // Time beyond duration => clamps to end
    clip.sample(5.0f, poses);
    EXPECT_NEAR(poses[0][3][0], 10.0f, 0.01f);
}

TEST(AnimationClipTest, SkipsInvalidBoneIndex) {
    AnimationClip clip;
    clip.duration = 1.0f;

    BoneTrack track;
    track.boneIndex = 5;  // Out of range
    Keyframe kf; kf.time = 0.0f; kf.position = glm::vec3(99, 0, 0);
    track.keyframes.push_back(kf);
    clip.tracks.push_back(std::move(track));

    std::vector<glm::mat4> poses(2, glm::mat4(1.0f));
    clip.sample(0.0f, poses);  // Should not crash

    // Poses unchanged
    EXPECT_FLOAT_EQ(poses[0][3][0], 0.0f);
    EXPECT_FLOAT_EQ(poses[1][3][0], 0.0f);
}

// ============================================================================
// AnimationController Tests
// ============================================================================

TEST(AnimationControllerTest, InitiallyNotPlaying) {
    AnimationController ctrl;
    EXPECT_FALSE(ctrl.isPlaying());
    EXPECT_EQ(ctrl.currentClip(), nullptr);
    EXPECT_FLOAT_EQ(ctrl.currentTime(), 0.0f);
}

TEST(AnimationControllerTest, PlayClip) {
    AnimationClip clip;
    clip.duration = 1.0f;
    clip.looping = true;

    AnimationController ctrl;
    ctrl.play(&clip, 0.0f);

    EXPECT_TRUE(ctrl.isPlaying());
    EXPECT_EQ(ctrl.currentClip(), &clip);
    EXPECT_FLOAT_EQ(ctrl.currentTime(), 0.0f);
}

TEST(AnimationControllerTest, UpdateAdvancesTime) {
    AnimationClip clip;
    clip.duration = 2.0f;
    clip.looping = true;

    AnimationController ctrl;
    ctrl.play(&clip, 0.0f);
    ctrl.update(0.5f);

    EXPECT_FLOAT_EQ(ctrl.currentTime(), 0.5f);

    ctrl.update(0.3f);
    EXPECT_FLOAT_EQ(ctrl.currentTime(), 0.8f);
}

TEST(AnimationControllerTest, Stop) {
    AnimationClip clip;
    clip.duration = 1.0f;

    AnimationController ctrl;
    ctrl.play(&clip, 0.0f);
    EXPECT_TRUE(ctrl.isPlaying());

    ctrl.stop();
    EXPECT_FALSE(ctrl.isPlaying());
    EXPECT_EQ(ctrl.currentClip(), nullptr);
}

TEST(AnimationControllerTest, PlayNullStops) {
    AnimationClip clip;
    clip.duration = 1.0f;

    AnimationController ctrl;
    ctrl.play(&clip, 0.0f);
    ctrl.play(nullptr, 0.0f);

    EXPECT_FALSE(ctrl.isPlaying());
}

TEST(AnimationControllerTest, CrossfadeBlend) {
    AnimationClip clipA;
    clipA.name = "A";
    clipA.duration = 2.0f;
    clipA.looping = true;
    BoneTrack trackA;
    trackA.boneIndex = 0;
    Keyframe kfA0; kfA0.time = 0.0f; kfA0.position = glm::vec3(0, 0, 0);
    Keyframe kfA1; kfA1.time = 2.0f; kfA1.position = glm::vec3(10, 0, 0);
    trackA.keyframes.push_back(kfA0);
    trackA.keyframes.push_back(kfA1);
    clipA.tracks.push_back(std::move(trackA));

    AnimationClip clipB;
    clipB.name = "B";
    clipB.duration = 2.0f;
    clipB.looping = true;
    BoneTrack trackB;
    trackB.boneIndex = 0;
    Keyframe kfB0; kfB0.time = 0.0f; kfB0.position = glm::vec3(0, 20, 0);
    Keyframe kfB1; kfB1.time = 2.0f; kfB1.position = glm::vec3(0, 20, 0);
    trackB.keyframes.push_back(kfB0);
    trackB.keyframes.push_back(kfB1);
    clipB.tracks.push_back(std::move(trackB));

    AnimationController ctrl;
    ctrl.play(&clipA, 0.0f);
    ctrl.update(1.0f);  // Advance A to t=1.0

    // Crossfade to B over 1 second
    ctrl.play(&clipB, 1.0f);
    ctrl.update(0.5f);  // Half-way through blend

    // Sample and verify blending
    std::vector<glm::mat4> poses(1, glm::mat4(1.0f));
    ctrl.sample(poses);

    // During blend: mix of A (continuing) and B (starting)
    // Just verify it produces some valid output
    EXPECT_TRUE(std::isfinite(poses[0][3][0]));
    EXPECT_TRUE(std::isfinite(poses[0][3][1]));
}

TEST(AnimationControllerTest, CrossfadeCompletes) {
    AnimationClip clipA;
    clipA.duration = 2.0f;
    clipA.looping = true;

    AnimationClip clipB;
    clipB.duration = 2.0f;
    clipB.looping = true;

    AnimationController ctrl;
    ctrl.play(&clipA, 0.0f);
    ctrl.update(0.5f);

    // Crossfade to B over 0.5s
    ctrl.play(&clipB, 0.5f);
    ctrl.update(1.0f);  // Well past crossfade duration

    // Should now be fully on clip B
    EXPECT_EQ(ctrl.currentClip(), &clipB);
}

TEST(AnimationControllerTest, SampleWithoutPlayingDoesNothing) {
    AnimationController ctrl;
    std::vector<glm::mat4> poses(3, glm::mat4(1.0f));
    ctrl.sample(poses);

    // Poses should remain identity
    for (int i = 0; i < 3; ++i) {
        EXPECT_FLOAT_EQ(poses[i][3][0], 0.0f);
        EXPECT_FLOAT_EQ(poses[i][3][1], 0.0f);
        EXPECT_FLOAT_EQ(poses[i][3][2], 0.0f);
    }
}

// ============================================================================
// SkeletonLoader Tests
// ============================================================================

TEST(SkeletonLoaderTest, LoadSimpleSkeleton) {
    const char* content = R"(
bone: body
parent: -
position: 0 0.5 0

bone: head
parent: body
position: 0 0.4 0

bone: left_arm
parent: body
position: -0.3 0.3 0
)";

    auto result = SkeletonLoader::loadSkeletonFromString(content);
    ASSERT_TRUE(result.has_value());

    const auto& skel = *result;
    EXPECT_EQ(skel.boneCount(), 3);

    // Check body
    EXPECT_EQ(skel.bone(0).name, "body");
    EXPECT_EQ(skel.bone(0).parentIndex, -1);
    EXPECT_FLOAT_EQ(skel.bone(0).restPosition.y, 0.5f);

    // Check head
    EXPECT_EQ(skel.bone(1).name, "head");
    EXPECT_EQ(skel.bone(1).parentIndex, 0);  // body
    EXPECT_FLOAT_EQ(skel.bone(1).restPosition.y, 0.4f);

    // Check left_arm
    EXPECT_EQ(skel.bone(2).name, "left_arm");
    EXPECT_EQ(skel.bone(2).parentIndex, 0);  // body
    EXPECT_FLOAT_EQ(skel.bone(2).restPosition.x, -0.3f);
}

TEST(SkeletonLoaderTest, EmptyContentFails) {
    auto result = SkeletonLoader::loadSkeletonFromString("");
    EXPECT_FALSE(result.has_value());
}

TEST(SkeletonLoaderTest, BoneWithRotation) {
    const char* content = R"(
bone: root
parent: -
position: 0 0 0
rotation: 0 0.7071 0 0.7071
)";

    auto result = SkeletonLoader::loadSkeletonFromString(content);
    ASSERT_TRUE(result.has_value());

    const auto& skel = *result;
    EXPECT_EQ(skel.boneCount(), 1);
    // Rotation: x=0, y=0.7071, z=0, w=0.7071 (90 deg around Y)
    EXPECT_NEAR(skel.bone(0).restRotation.y, 0.7071f, 0.001f);
    EXPECT_NEAR(skel.bone(0).restRotation.w, 0.7071f, 0.001f);
}

TEST(SkeletonLoaderTest, BoneWithScale) {
    const char* content = R"(
bone: scaled
parent: -
position: 0 0 0
scale: 2 3 4
)";

    auto result = SkeletonLoader::loadSkeletonFromString(content);
    ASSERT_TRUE(result.has_value());

    EXPECT_FLOAT_EQ(result->bone(0).restScale.x, 2.0f);
    EXPECT_FLOAT_EQ(result->bone(0).restScale.y, 3.0f);
    EXPECT_FLOAT_EQ(result->bone(0).restScale.z, 4.0f);
}

TEST(SkeletonLoaderTest, MultipleBoneChain) {
    const char* content = R"(
bone: root
parent: -
position: 0 0 0

bone: mid
parent: root
position: 0 1 0

bone: tip
parent: mid
position: 0 1 0
)";

    auto result = SkeletonLoader::loadSkeletonFromString(content);
    ASSERT_TRUE(result.has_value());

    const auto& skel = *result;
    EXPECT_EQ(skel.boneCount(), 3);
    EXPECT_EQ(skel.bone(2).parentIndex, 1);  // tip -> mid
    EXPECT_EQ(skel.bone(1).parentIndex, 0);  // mid -> root
    EXPECT_EQ(skel.bone(0).parentIndex, -1); // root -> none
}

// ============================================================================
// Animation Loader Tests
// ============================================================================

TEST(SkeletonLoaderTest, LoadSimpleAnimation) {
    // First create a skeleton
    Skeleton skel;
    Bone body; body.name = "body"; body.parentIndex = -1;
    Bone head; head.name = "head"; head.parentIndex = 0;
    skel.addBone(std::move(body));
    skel.addBone(std::move(head));

    const char* content = R"(
name: walk
duration: 1.0
loop: true

track: body
keyframe: 0.0
pos: 0 0 0
keyframe: 0.5
pos: 0 0.1 0
keyframe: 1.0
pos: 0 0 0

track: head
keyframe: 0.0
pos: 0 0.4 0
rot: 0 0 0 1
keyframe: 1.0
pos: 0 0.4 0
rot: 0 0 0 1
)";

    auto result = SkeletonLoader::loadAnimationFromString(content, skel);
    ASSERT_TRUE(result.has_value());

    const auto& clip = *result;
    EXPECT_EQ(clip.name, "walk");
    EXPECT_FLOAT_EQ(clip.duration, 1.0f);
    EXPECT_TRUE(clip.looping);
    EXPECT_EQ(clip.tracks.size(), 2u);

    // Body track
    EXPECT_EQ(clip.tracks[0].boneIndex, 0);
    EXPECT_EQ(clip.tracks[0].keyframes.size(), 3u);
    EXPECT_FLOAT_EQ(clip.tracks[0].keyframes[1].position.y, 0.1f);

    // Head track
    EXPECT_EQ(clip.tracks[1].boneIndex, 1);
    EXPECT_EQ(clip.tracks[1].keyframes.size(), 2u);
}

TEST(SkeletonLoaderTest, EmptyAnimationFails) {
    Skeleton skel;
    auto result = SkeletonLoader::loadAnimationFromString("", skel);
    EXPECT_FALSE(result.has_value());
}

TEST(SkeletonLoaderTest, AnimationWithScale) {
    Skeleton skel;
    Bone b; b.name = "bone"; b.parentIndex = -1;
    skel.addBone(std::move(b));

    const char* content = R"(
name: grow
duration: 1.0
loop: false

track: bone
keyframe: 0.0
scale: 1 1 1
keyframe: 1.0
scale: 2 2 2
)";

    auto result = SkeletonLoader::loadAnimationFromString(content, skel);
    ASSERT_TRUE(result.has_value());

    EXPECT_FALSE(result->looping);
    ASSERT_EQ(result->tracks.size(), 1u);
    ASSERT_EQ(result->tracks[0].keyframes.size(), 2u);
    EXPECT_FLOAT_EQ(result->tracks[0].keyframes[0].scale.x, 1.0f);
    EXPECT_FLOAT_EQ(result->tracks[0].keyframes[1].scale.x, 2.0f);
}

TEST(SkeletonLoaderTest, AnimationUnknownBoneSkipped) {
    Skeleton skel;
    Bone b; b.name = "body"; b.parentIndex = -1;
    skel.addBone(std::move(b));

    const char* content = R"(
name: test
duration: 1.0

track: nonexistent
keyframe: 0.0
pos: 1 2 3
)";

    auto result = SkeletonLoader::loadAnimationFromString(content, skel);
    // Track with invalid bone index should still load but be filtered out
    // (boneIndex will be -1 from findBone, and finalizeTrack requires boneIndex >= 0)
    // So the clip will have no tracks and thus be empty
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// Integration: Skeleton + Clip + Controller
// ============================================================================

TEST(AnimationIntegrationTest, FullPipelineWorks) {
    // Build skeleton
    Skeleton skel;
    Bone root; root.name = "root"; root.parentIndex = -1;
    root.restPosition = glm::vec3(0, 0, 0);
    skel.addBone(std::move(root));

    Bone arm; arm.name = "arm"; arm.parentIndex = 0;
    arm.restPosition = glm::vec3(1, 0, 0);
    skel.addBone(std::move(arm));

    // Build clip
    AnimationClip clip;
    clip.name = "wave";
    clip.duration = 1.0f;
    clip.looping = true;

    BoneTrack track;
    track.boneIndex = 1;  // arm
    Keyframe kf0; kf0.time = 0.0f; kf0.position = glm::vec3(1, 0, 0);
    Keyframe kf1; kf1.time = 0.5f; kf1.position = glm::vec3(1, 1, 0);
    Keyframe kf2; kf2.time = 1.0f; kf2.position = glm::vec3(1, 0, 0);
    track.keyframes.push_back(kf0);
    track.keyframes.push_back(kf1);
    track.keyframes.push_back(kf2);
    clip.tracks.push_back(std::move(track));

    // Use controller
    AnimationController ctrl;
    ctrl.play(&clip, 0.0f);
    ctrl.update(0.25f);  // quarter way

    // Sample into local poses
    std::vector<glm::mat4> local(2, glm::mat4(1.0f));
    skel.computeRestPose(local);  // Set rest pose first
    ctrl.sample(local);           // Override animated bones

    // Compute world transforms
    std::vector<glm::mat4> world;
    skel.computeWorldTransforms(local, world);

    // Root should be at origin (no animation on it, rest pose is 0,0,0)
    EXPECT_NEAR(world[0][3][0], 0.0f, 0.01f);
    EXPECT_NEAR(world[0][3][1], 0.0f, 0.01f);

    // Arm should be offset from root by animated position
    // At t=0.25, arm position is lerp(1,0,0 → 1,1,0) = (1, 0.5, 0)
    // World = root * arm_local
    EXPECT_NEAR(world[1][3][0], 1.0f, 0.01f);
    EXPECT_NEAR(world[1][3][1], 0.5f, 0.01f);
}

TEST(AnimationIntegrationTest, LoaderPipeline) {
    const char* skelContent = R"(
bone: body
parent: -
position: 0 0.5 0

bone: head
parent: body
position: 0 0.4 0
)";

    auto skel = SkeletonLoader::loadSkeletonFromString(skelContent);
    ASSERT_TRUE(skel.has_value());

    const char* animContent = R"(
name: nod
duration: 1.0
loop: true

track: head
keyframe: 0.0
rot: 0 0 0 1
keyframe: 0.5
rot: 0.1 0 0 0.995
keyframe: 1.0
rot: 0 0 0 1
)";

    auto clip = SkeletonLoader::loadAnimationFromString(animContent, *skel);
    ASSERT_TRUE(clip.has_value());

    AnimationController ctrl;
    ctrl.play(&*clip, 0.0f);
    ctrl.update(0.25f);

    std::vector<glm::mat4> local(skel->boneCount(), glm::mat4(1.0f));
    skel->computeRestPose(local);
    ctrl.sample(local);

    std::vector<glm::mat4> world;
    skel->computeWorldTransforms(local, world);

    // Just verify it computes something valid
    ASSERT_EQ(world.size(), 2u);
    EXPECT_TRUE(std::isfinite(world[0][3][0]));
    EXPECT_TRUE(std::isfinite(world[1][3][1]));
}
