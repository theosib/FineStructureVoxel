#include <gtest/gtest.h>
#include "finevox/core/entity_model.hpp"
#include "finevox/core/position.hpp"

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

TEST(EntityModel, PartsCanBeShownOnlyDuringAnAnimation) {
    const char* model = R"(
skeleton: rig
part: arm
bone: body
size: 0.1 0.5 0.1
part: axe
bone: body
size: 0.05 0.6 0.05
only_in: chop quarry
part: axe_on_belt
bone: body
size: 0.05 0.3 0.05
hidden_in: chop quarry
)";
    EntityModelRegistry registry;
    auto parsed = registry.parse("tooled", model, reader);
    ASSERT_TRUE(parsed.has_value());
    ASSERT_EQ(parsed->mesh.parts.size(), 3u);

    std::vector<glm::mat4> poses(parsed->skeleton->boneCount(), glm::mat4(1.0f));
    std::vector<EntityVertex> vertices;
    std::vector<uint32_t> indices;
    auto partsShown = [&](std::string_view animation) {
        parsed->mesh.buildVertices(poses, vertices, indices, glm::vec3(1.0f), animation);
        return vertices.size() / 24;
    };
    EXPECT_EQ(partsShown("walk"), 2u);   // arm, and the axe at the belt
    EXPECT_EQ(partsShown(""), 2u);
    EXPECT_EQ(partsShown("chop"), 2u);   // arm, and the axe in hand
    EXPECT_EQ(partsShown("quarry"), 2u);

    const auto& axe = parsed->mesh.parts[1];
    const auto& belt = parsed->mesh.parts[2];
    EXPECT_TRUE(axe.shownDuring("chop"));
    EXPECT_FALSE(axe.shownDuring("walk"));
    EXPECT_FALSE(belt.shownDuring("chop"));
    EXPECT_TRUE(belt.shownDuring("walk"));
}

TEST(EntityModel, AHeldPartIsTheHeldBlockWithItsOwnFaces) {
    const char* model = R"(
skeleton: rig
hold_pose: rig_walk
part: body
bone: body
size: 0.5 1 0.3
part: load
bone: body
offset: 0 0 0.4
size: 0.4 0.4 0.4
color: 0.5 0.5 0.5
held: true
only_in: walk
)";
    EntityModelRegistry registry;
    auto parsed = registry.parse("carrier", model, reader);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_NE(parsed->holdPose, nullptr);
    EXPECT_TRUE(parsed->mesh.parts[1].held);
    EXPECT_TRUE(parsed->mesh.showsHeld("walk"));
    EXPECT_FALSE(parsed->mesh.showsHeld("chop"));

    std::vector<glm::mat4> poses(parsed->skeleton->boneCount(), glm::mat4(1.0f));
    std::vector<EntityVertex> vertices;
    std::vector<uint32_t> indices;
    // Holding nothing, there's nothing to draw
    parsed->mesh.buildVertices(poses, vertices, indices, glm::vec3(1.0f), "walk");
    EXPECT_EQ(vertices.size(), 24u);

    // Each face its own tile, by Face, so a mixed-up face shows
    std::array<glm::vec4, 6> faces;
    for (size_t f = 0; f < 6; ++f) faces[f] = glm::vec4(0.1f * f, 0.5f, 0.1f * f + 0.05f, 0.6f);
    parsed->mesh.buildVertices(poses, vertices, indices, glm::vec3(1.0f), "walk", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                               glm::vec2(64.0f), &faces);
    ASSERT_EQ(vertices.size(), 48u);
    auto faceOf = [](const glm::vec3& n) {
        if (n.x > 0.5f) return Face::PosX;
        if (n.x < -0.5f) return Face::NegX;
        if (n.y > 0.5f) return Face::PosY;
        if (n.y < -0.5f) return Face::NegY;
        return n.z > 0.5f ? Face::PosZ : Face::NegZ;
    };
    for (size_t i = 24; i < 48; ++i) {
        const auto& v = vertices[i];
        EXPECT_TRUE(v.skinned);
        EXPECT_EQ(v.color, glm::vec3(0.5f));  // its own colour, never the team's
        const glm::vec4& r = faces[static_cast<size_t>(faceOf(v.normal))];
        EXPECT_GE(v.texCoord.x, r.x - 1e-6f);
        EXPECT_LE(v.texCoord.x, r.z + 1e-6f);
        // Sides upright, as the world draws a block: its top edge at the top of the image
        if (std::abs(v.normal.y) < 0.5f) {
            float want = v.position.y > 0.0f ? r.y : r.w;
            EXPECT_FLOAT_EQ(v.texCoord.y, want) << "vertex " << i;
        }
    }

    // Put away during other work
    parsed->mesh.buildVertices(poses, vertices, indices, glm::vec3(1.0f), "chop", glm::vec4(0.0f, 0.0f, 1.0f, 1.0f),
                               glm::vec2(64.0f), &faces);
    EXPECT_EQ(vertices.size(), 24u);
}
