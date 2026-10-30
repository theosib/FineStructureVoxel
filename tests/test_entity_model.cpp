#include <gtest/gtest.h>
#include "finevox/core/entity_model.hpp"

using namespace finevox;

namespace {

const char* SKELETON = R"(
bone: body
parent: -
position: 0 1 0
bone: head
parent: body
position: 0 0.5 0
)";

const char* WALK = R"(
name: walk
duration: 0.5
track: head
keyframe: 0
pos: 0 0.5 0
keyframe: 0.5
pos: 0 0.6 0
)";

const char* MODEL = R"(
skeleton: rig
anim:walk: rig_walk
part: torso
bone: body
offset: 0 0.1 0
size: 0.5 0.6 0.3
color: 0.2 0.4 0.6
team: 0.75
part: hat
bone: head
size: 0.4 0.2 0.4
)";

std::optional<std::string> reader(const std::string& name) {
    if (name == "rig.skeleton") return SKELETON;
    if (name == "rig_walk.anim") return WALK;
    return std::nullopt;
}

}  // namespace

TEST(EntityModel, ParsesPartsSkeletonAndClips) {
    EntityModelRegistry registry;
    auto model = registry.parse("worker", MODEL, reader);
    ASSERT_TRUE(model.has_value());

    ASSERT_EQ(model->mesh.parts.size(), 2u);
    const auto& torso = model->mesh.parts[0];
    EXPECT_EQ(torso.boneIndex, model->skeleton->findBone("body"));
    EXPECT_FLOAT_EQ(torso.size.y, 0.6f);
    EXPECT_FLOAT_EQ(torso.color.b, 0.6f);
    EXPECT_FLOAT_EQ(torso.teamTint, 0.75f);
    EXPECT_EQ(model->mesh.parts[1].boneIndex, model->skeleton->findBone("head"));
    EXPECT_EQ(model->clips.count("walk"), 1u);
}

TEST(EntityModel, SharesSkeletonsBetweenModels) {
    EntityModelRegistry registry;
    auto a = registry.parse("a", MODEL, reader);
    auto b = registry.parse("b", MODEL, reader);
    ASSERT_TRUE(a && b);
    EXPECT_EQ(a->skeleton.get(), b->skeleton.get());
}

TEST(EntityModel, RejectsModelsWithoutASkeleton) {
    EntityModelRegistry registry;
    EXPECT_FALSE(registry.parse("x", "part: a\nsize: 1 1 1\n", reader).has_value());
    EXPECT_FALSE(registry.parse("x", "skeleton: missing\npart: a\n", reader).has_value());
}

TEST(EntityMesh, TeamColourBlendsByPart) {
    EntityMesh mesh;
    EntityMeshPart part;
    part.color = glm::vec3(1.0f, 0.0f, 0.0f);
    part.teamTint = 0.5f;
    mesh.parts.push_back(part);

    std::vector<EntityVertex> vertices;
    std::vector<uint32_t> indices;
    mesh.buildVertices({glm::mat4(1.0f)}, vertices, indices, glm::vec3(0.0f, 0.0f, 1.0f));
    ASSERT_FALSE(vertices.empty());
    EXPECT_FLOAT_EQ(vertices[0].color.r, 0.5f);
    EXPECT_FLOAT_EQ(vertices[0].color.b, 0.5f);
}
