// Entity parts textured with the box unwrap

#include <gtest/gtest.h>

#include "finevox/core/entity_mesh.hpp"
#include "finevox/core/entity_model.hpp"

#include <glm/glm.hpp>

using namespace finevox;

namespace {

// A 8x8x8 box at uv (0, 0) on a 64x64 skin: the unwrap puts the front at pixels 8..16 across
// and 8..16 down, the top at 8..16 across and 0..8 down, the right side at 0..8, 8..16.
EntityMeshPart cube() {
    EntityMeshPart part;
    part.size = glm::vec3(0.5f);
    part.textured = true;
    part.uvPixels = glm::vec2(0.0f);
    part.uvBox = glm::vec3(8.0f);
    return part;
}

// The texture rectangle a face's four vertices cover, in pixels
glm::vec4 faceRect(const EntityMesh& mesh, glm::vec3 normal, bool* found = nullptr) {
    std::vector<EntityVertex> v;
    std::vector<uint32_t> idx;
    mesh.buildVertices({glm::mat4(1.0f)}, v, idx, glm::vec3(1.0f), {}, glm::vec4(0, 0, 1, 1), glm::vec2(64.0f));
    glm::vec4 r(1e9f, 1e9f, -1e9f, -1e9f);
    for (const auto& vert : v) {
        if (glm::length(vert.normal - normal) > 1e-4f) continue;
        if (found) *found = true;
        glm::vec2 px = vert.texCoord * 64.0f;
        r = glm::vec4(std::min(r.x, px.x), std::min(r.y, px.y), std::max(r.z, px.x), std::max(r.w, px.y));
    }
    return r;
}

}  // namespace

TEST(EntitySkin, FrontTopAndSidesComeFromTheirPlaces) {
    EntityMesh mesh;
    mesh.parts.push_back(cube());
    // Rigs face +Z: that face shows the unwrap's front
    EXPECT_EQ(faceRect(mesh, {0, 0, 1}), glm::vec4(8, 8, 16, 16));
    EXPECT_EQ(faceRect(mesh, {0, 1, 0}), glm::vec4(8, 0, 16, 8));
    EXPECT_EQ(faceRect(mesh, {0, -1, 0}), glm::vec4(16, 0, 24, 8));
    EXPECT_EQ(faceRect(mesh, {0, 0, -1}), glm::vec4(24, 8, 32, 16));
    // Facing +Z, the creature's right is -X, which the unwrap puts first
    EXPECT_EQ(faceRect(mesh, {-1, 0, 0}), glm::vec4(0, 8, 8, 16));
    EXPECT_EQ(faceRect(mesh, {1, 0, 0}), glm::vec4(16, 8, 24, 16));
}

TEST(EntitySkin, FrontReadsUprightAndUnmirrored) {
    EntityMesh mesh;
    mesh.parts.push_back(cube());
    std::vector<EntityVertex> v;
    std::vector<uint32_t> idx;
    mesh.buildVertices({glm::mat4(1.0f)}, v, idx, glm::vec3(1.0f), {}, glm::vec4(0, 0, 1, 1), glm::vec2(64.0f));
    for (const auto& vert : v) {
        if (vert.normal != glm::vec3(0, 0, 1)) continue;
        glm::vec2 px = vert.texCoord * 64.0f;
        // Seen from in front, looking toward -Z, the viewer's left is -X: the texture's left
        // edge (u = 8) is there, next to the creature's right side (0..8, also on -X)
        if (vert.position.x < 0) EXPECT_FLOAT_EQ(px.x, 8.0f);
        if (vert.position.x > 0) EXPECT_FLOAT_EQ(px.x, 16.0f);
        // and the top of the texture at the top
        if (vert.position.y > 0) EXPECT_FLOAT_EQ(px.y, 8.0f);
        if (vert.position.y < 0) EXPECT_FLOAT_EQ(px.y, 16.0f);
    }
}

TEST(EntitySkin, MirrorSwapsTheSides) {
    EntityMesh mesh;
    EntityMeshPart part = cube();
    part.mirror = true;
    mesh.parts.push_back(part);
    EXPECT_EQ(faceRect(mesh, {1, 0, 0}), glm::vec4(0, 8, 8, 16));
    EXPECT_EQ(faceRect(mesh, {-1, 0, 0}), glm::vec4(16, 8, 24, 16));
}

TEST(EntitySkin, AtlasPlacementScalesIntoItsRectangle) {
    EntityMesh mesh;
    mesh.parts.push_back(cube());
    std::vector<EntityVertex> v;
    std::vector<uint32_t> idx;
    // The skin in the right half of an atlas
    mesh.buildVertices({glm::mat4(1.0f)}, v, idx, glm::vec3(1.0f), {}, glm::vec4(0.5f, 0, 1, 1), glm::vec2(64.0f));
    for (const auto& vert : v) {
        EXPECT_GE(vert.texCoord.x, 0.5f);
        EXPECT_LE(vert.texCoord.x, 1.0f);
    }
}

TEST(EntitySkin, TurnedPartLiesDownButKeepsItsUnwrap) {
    EntityMesh mesh;
    EntityMeshPart part = cube();
    part.size = glm::vec3(0.5f, 1.0f, 0.25f);  // tall and thin, standing
    part.uvBox = glm::vec3(8, 16, 4);
    part.turnDegrees = glm::vec3(90, 0, 0);
    mesh.parts.push_back(part);
    std::vector<EntityVertex> v;
    std::vector<uint32_t> idx;
    mesh.buildVertices({glm::mat4(1.0f)}, v, idx);
    float minY = 1e9f, maxY = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const auto& vert : v) {
        minY = std::min(minY, vert.position.y);
        maxY = std::max(maxY, vert.position.y);
        minZ = std::min(minZ, vert.position.z);
        maxZ = std::max(maxZ, vert.position.z);
    }
    EXPECT_NEAR(maxY - minY, 0.25f, 1e-5f);  // its depth is now its height
    EXPECT_NEAR(maxZ - minZ, 1.0f, 1e-5f);   // and its height its length
}

TEST(EntitySkin, ModelFileGivesTextureAndParts) {
    EntityModelRegistry registry;
    auto readFile = [](const std::string& name) -> std::optional<std::string> {
        if (name == "rig.skeleton") return std::string("bone: body\nparent: -\nposition: 0 0 0\n");
        return std::nullopt;
    };
    auto model = registry.parse("beast",
                                "skeleton: rig\ntexture: entity/beast\ntexture_size: 64 32\n"
                                "part: head\nbone: body\nsize: 0.5 0.5 0.5\nuv: 4 2\nmirror: true\n"
                                "part: tail\nbone: body\nuv: 0 30\nuv_size: 1 2 1\nsize: 0.1 0.1 0.1\n",
                                readFile);
    ASSERT_TRUE(model.has_value());
    EXPECT_EQ(model->texture, "entity/beast");
    EXPECT_EQ(model->textureSize, glm::vec2(64, 32));
    ASSERT_EQ(model->mesh.parts.size(), 2u);
    EXPECT_TRUE(model->mesh.parts[0].textured);
    EXPECT_TRUE(model->mesh.parts[0].mirror);
    EXPECT_EQ(model->mesh.parts[0].uvBox, glm::vec3(8.0f));  // from its size, 16 a block
    EXPECT_EQ(model->mesh.parts[1].uvBox, glm::vec3(1, 2, 1));
}
