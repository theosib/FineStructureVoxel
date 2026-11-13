// Block states, per-face textures, and turning a block's look

#include <gtest/gtest.h>

#include "finevox/core/block_model_loader.hpp"
#include "finevox/core/block_model_transform.hpp"
#include "finevox/core/block_properties.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/content_pack_loader.hpp"

#include <filesystem>
#include <fstream>

using namespace finevox;
namespace fs = std::filesystem;

namespace {

glm::vec3 centreOf(const FaceGeometry& face) {
    glm::vec3 sum(0.0f);
    for (const auto& v : face.vertices) sum += v.position;
    return sum / static_cast<float>(face.vertices.size());
}

// A face across the north side of the lower half, as a slab's would be
FaceGeometry northHalfFace() {
    FaceGeometry face;
    face.name = "north";
    face.faceIndex = static_cast<int>(Face::NegZ);
    face.texture = face.textureRef = "side";
    face.vertices = {{0, 0, 0, 1, 1}, {1, 0, 0, 0, 1}, {1, 0.5f, 0, 0, 0.5f}, {0, 0.5f, 0, 1, 0.5f}};
    return face;
}

BlockModel cubeWith(const std::string& top, const std::string& bottom, const std::string& side) {
    BlockModel m;
    m.setFaceTextures(top, bottom, side);
    return m;
}

CollisionShape shapeOf(const AABB& box) {
    CollisionShape shape;
    shape.addBox(box);
    return shape;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

}  // namespace

// ---------------------------------------------------------------------------
// Turning
// ---------------------------------------------------------------------------

TEST(BlockModelTurn, QuarterTurnAboutYCarriesNorthToEast) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel m;
    m.setGeometry(g);
    m.setCollision(shapeOf(AABB(0, 0, 0, 1, 0.5f, 0.5f)));  // the north half

    BlockModel turned = turnedModel(m, 0, 90, false);
    ASSERT_EQ(turned.geometry().faces().size(), 1u);
    const FaceGeometry& face = turned.geometry().faces()[0];
    EXPECT_EQ(face.faceIndex, static_cast<int>(Face::PosX));
    EXPECT_EQ(face.name, "east");
    glm::vec3 c = centreOf(face);
    EXPECT_FLOAT_EQ(c.x, 1.0f);
    EXPECT_FLOAT_EQ(c.y, 0.25f);
    EXPECT_FLOAT_EQ(c.z, 0.5f);

    const auto& boxes = turned.resolvedCollision().boxes();
    ASSERT_EQ(boxes.size(), 1u);
    EXPECT_FLOAT_EQ(boxes[0].min.x, 0.5f);
    EXPECT_FLOAT_EQ(boxes[0].max.x, 1.0f);
    EXPECT_FLOAT_EQ(boxes[0].min.z, 0.0f);
    EXPECT_FLOAT_EQ(boxes[0].max.z, 1.0f);
    EXPECT_FLOAT_EQ(boxes[0].max.y, 0.5f);
}

TEST(BlockModelTurn, QuarterTurnAboutXCarriesNorthDownAndTopNorth) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel m;
    m.setGeometry(g);
    BlockModel turned = turnedModel(m, 90, 0, false);
    const FaceGeometry& face = turned.geometry().faces()[0];
    EXPECT_EQ(face.faceIndex, static_cast<int>(Face::NegY));
    // The north side swings down, its bottom edge going round to the south, so its lower
    // half ends up as the south half of the bottom
    glm::vec3 c = centreOf(face);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 0.75f);
}

TEST(BlockModelTurn, FourQuarterTurnsAreNone) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel m;
    m.setGeometry(g);
    BlockModel turned = turnedModel(m, 360, -360, false);
    const FaceGeometry& face = turned.geometry().faces()[0];
    const FaceGeometry original = northHalfFace();
    ASSERT_EQ(face.vertices.size(), original.vertices.size());
    for (size_t i = 0; i < face.vertices.size(); ++i) {
        EXPECT_EQ(face.vertices[i].position, original.vertices[i].position);
        EXPECT_EQ(face.vertices[i].uv, original.vertices[i].uv);
    }
}

TEST(BlockModelTurn, UVLockProjectsFromWhereTheFaceEndsUp) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel m;
    m.setGeometry(g);
    // Turned onto the east side; an east face shows u = 1 - z, v = 1 - y
    BlockModel turned = turnedModel(m, 0, 90, true);
    const FaceGeometry& face = turned.geometry().faces()[0];
    for (const auto& v : face.vertices) {
        EXPECT_FLOAT_EQ(v.uv.x, 1.0f - v.position.z);
        EXPECT_FLOAT_EQ(v.uv.y, 1.0f - v.position.y);
    }
}

TEST(BlockModelTurn, TurnedCubeStaysACubeWithItsTexturesMoved) {
    // A column: ends on top and bottom, bark around. Laid on its side by a turn about X.
    BlockModel log = cubeWith("end", "end", "bark");
    BlockModel turned = turnedModel(log, 90, 0, false);
    ASSERT_FALSE(turned.hasCustomGeometry());

    std::string base;
    int degrees = 0;
    splitTextureTurn(turned.cubeFaceTexture(Face::NegZ), base, degrees);
    EXPECT_EQ(base, "end");  // the top went north
    splitTextureTurn(turned.cubeFaceTexture(Face::PosZ), base, degrees);
    EXPECT_EQ(base, "end");
    splitTextureTurn(turned.cubeFaceTexture(Face::PosY), base, degrees);
    EXPECT_EQ(base, "bark");
    // East and west keep their faces but turn in place, so the grain lies along Z
    splitTextureTurn(turned.cubeFaceTexture(Face::PosX), base, degrees);
    EXPECT_EQ(base, "bark");
    EXPECT_TRUE(degrees == 90 || degrees == 270) << degrees;
    splitTextureTurn(turned.cubeFaceTexture(Face::NegX), base, degrees);
    EXPECT_TRUE(degrees == 90 || degrees == 270) << degrees;
}

TEST(BlockModelTurn, TurnAboutYLeavesSideTexturesUpright) {
    BlockModel oven;
    oven.setFaceTextures("top", "top", "side");
    oven.setFaceTexture(Face::NegZ, "front");
    BlockModel east = turnedModel(oven, 0, 90, false);
    EXPECT_EQ(east.cubeFaceTexture(Face::PosX), "front");
    EXPECT_EQ(east.cubeFaceTexture(Face::NegZ), "side");
    EXPECT_EQ(east.cubeFaceTexture(Face::PosZ), "side");
}

TEST(BlockModelTurn, TextureTurnNamesAddUp) {
    EXPECT_EQ(turnedTextureName("a", 90), "a@90");
    EXPECT_EQ(turnedTextureName("a@90", 90), "a@180");
    EXPECT_EQ(turnedTextureName("a@270", 90), "a");
    EXPECT_EQ(turnedTextureName("a", -90), "a@270");
    EXPECT_EQ(turnedTextureName("mail@home", 0), "mail@home");  // not a turn
}

TEST(BlockModelCube, CubeGeometryIsRecognisedAsACube) {
    BlockModel m = cubeWith("t", "b", "s");
    BlockModel asGeometry;
    asGeometry.setGeometry(cubeGeometry(m));
    ASSERT_TRUE(drawAsCubeIfPlain(asGeometry));
    EXPECT_FALSE(asGeometry.hasCustomGeometry());
    EXPECT_EQ(asGeometry.cubeFaceTexture(Face::PosY), "t");
    EXPECT_EQ(asGeometry.cubeFaceTexture(Face::NegY), "b");
    EXPECT_EQ(asGeometry.cubeFaceTexture(Face::PosX), "s");
}

TEST(BlockModelCube, PartialShapesAreNotCubes) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel m;
    m.setGeometry(g);
    EXPECT_FALSE(drawAsCubeIfPlain(m));
    EXPECT_TRUE(m.hasCustomGeometry());
}

TEST(BlockModelCube, CombinedLookKeepsEveryFaceAndShape) {
    BlockGeometry g;
    g.addFace(northHalfFace());
    BlockModel a;
    a.setGeometry(g);
    a.setCollision(shapeOf(AABB(0, 0, 0, 1, 0.5f, 0.5f)));
    BlockModel b = turnedModel(a, 0, 180, false);
    BlockModel both = combinedLook({a, b});
    EXPECT_EQ(both.geometry().faces().size(), 2u);
    EXPECT_EQ(both.resolvedCollision().boxes().size(), 2u);
}

// ---------------------------------------------------------------------------
// State names
// ---------------------------------------------------------------------------

class BlockPropertyNames : public ::testing::Test {
protected:
    void SetUp() override {
        BlockPropertyRegistry::global().clear();
        BlockPropertyRegistry::global().addFamily(
            "door", {{"half", {"lower", "upper"}}, {"open", {"false", "true"}}});
    }
    void TearDown() override { BlockPropertyRegistry::global().clear(); }
};

TEST_F(BlockPropertyNames, DefaultStateKeepsThePlainName) {
    auto& r = BlockPropertyRegistry::global();
    EXPECT_EQ(r.stateName("door", {"lower", "false"}), "door");
    EXPECT_EQ(r.stateName("door", {"upper", "false"}), "door[half=upper,open=false]");
    EXPECT_EQ(r.stateName("ns:door", {"lower", "true"}), "ns:door[half=lower,open=true]");
}

TEST_F(BlockPropertyNames, ReadAndChangeOneProperty) {
    auto& r = BlockPropertyRegistry::global();
    EXPECT_EQ(r.property("door", "open"), "false");
    EXPECT_EQ(r.property("ns:door[half=upper,open=true]", "half"), "upper");
    EXPECT_EQ(r.withProperty("ns:door", "open", "true"), "ns:door[half=lower,open=true]");
    EXPECT_EQ(r.withProperty("door[half=lower,open=true]", "open", "false"), "door");
    EXPECT_FALSE(r.withProperty("door", "open", "ajar").has_value());
    EXPECT_FALSE(r.property("door", "colour").has_value());
    EXPECT_FALSE(r.property("stone", "open").has_value());
    EXPECT_EQ(BlockPropertyRegistry::familyOf("ns:door[open=true]"), "ns:door");
}

TEST(PropertyCondition, MatchesListedValuesAndIgnoresTheRest) {
    std::vector<std::string> names = {"half", "facing"};
    auto c = PropertyCondition::parse("facing=east|west");
    EXPECT_TRUE(c.matches(names, {"top", "east"}));
    EXPECT_TRUE(c.matches(names, {"bottom", "west"}));
    EXPECT_FALSE(c.matches(names, {"top", "north"}));
    EXPECT_TRUE(PropertyCondition::parse("*").matches(names, {"top", "north"}));
    EXPECT_FALSE(PropertyCondition::parse("colour=red").matches(names, {"top", "north"}));
}

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

class BlockPropertyLoading : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("finevox_props_" + std::to_string(::getpid()));
        fs::remove_all(root_);
        BlockPropertyRegistry::global().clear();
    }
    void TearDown() override {
        fs::remove_all(root_);
        BlockPropertyRegistry::global().clear();
    }
    fs::path root_;
};

TEST_F(BlockPropertyLoading, EveryCombinationIsABlockWithItsVariantsLook) {
    write(root_ / "shapes/half.geom",
          "face:north: #side\n"
          "    0 0 0  1 1\n    1 0 0  0 1\n    1 0.5 0  0 0.5\n    0 0.5 0  1 0.5\n");
    write(root_ / "blocks/looks/half.model",
          "geometry: shapes/half\ntexture:side: block/plank\ncollision: shapes/half_box\n");
    write(root_ / "shapes/half_box.collision", "box:\n    0 0 0 1 0.5 0.5\n");
    write(root_ / "blocks/step.model",
          "hardness: 2\n"
          "opaque: false\n"
          "state:facing: north east south west\n"
          "state:lit: false true\n"
          "variant:facing=north: looks/half\n"
          "variant:facing=east: looks/half y=90\n"
          "variant:facing=south: looks/half y=180\n"
          "variant:facing=west: looks/half y=270\n");

    ContentPackLoader loader("test");
    EXPECT_EQ(loader.loadBlocks(root_), 8u);

    auto east = BlockTypeId::fromName("test:step[facing=east,lit=false]");
    const BlockGeometry* geometry = loader.getGeometry(east);
    ASSERT_NE(geometry, nullptr);
    ASSERT_EQ(geometry->faces().size(), 1u);
    EXPECT_EQ(geometry->faces()[0].faceIndex, static_cast<int>(Face::PosX));
    EXPECT_EQ(geometry->faces()[0].texture, "block/plank");
    EXPECT_FLOAT_EQ(BlockRegistry::global().getType(east).hardness(), 2.0f);

    const auto& box = BlockRegistry::global().getType(east).collisionShape().boxes();
    ASSERT_EQ(box.size(), 1u);
    EXPECT_FLOAT_EQ(box[0].min.x, 0.5f);

    // The default state has the plain name, and scripts can find their way between them
    ASSERT_NE(loader.getGeometry(BlockTypeId::fromName("test:step")), nullptr);
    EXPECT_EQ(BlockPropertyRegistry::global().withProperty("test:step", "facing", "east"),
              "test:step[facing=east,lit=false]");

    auto names = loader.faceTextureNames();
    EXPECT_EQ(names, std::vector<std::string>{"block/plank"});
    loader.placeFaceTextures([](const std::string&) { return glm::vec4(0.1f, 0.2f, 0.3f, 0.4f); });
    EXPECT_TRUE(loader.getGeometry(east)->faces()[0].hasTextureUV);
}

TEST_F(BlockPropertyLoading, PartsDrawTogether) {
    write(root_ / "shapes/half.geom",
          "face:north: block/rail\n"
          "    0 0 0  1 1\n    1 0 0  0 1\n    1 0.5 0  0 0.5\n    0 0.5 0  1 0.5\n");
    write(root_ / "blocks/looks/side.model", "geometry: shapes/half\n");
    write(root_ / "blocks/fence.model",
          "state:north: false true\n"
          "state:south: false true\n"
          "part:north=true: looks/side\n"
          "part:south=true: looks/side y=180\n");
    ContentPackLoader loader("test");
    EXPECT_EQ(loader.loadBlocks(root_), 4u);
    const BlockGeometry* both =
        loader.getGeometry(BlockTypeId::fromName("test:fence[north=true,south=true]"));
    ASSERT_NE(both, nullptr);
    EXPECT_EQ(both->faces().size(), 2u);
    const BlockGeometry* one =
        loader.getGeometry(BlockTypeId::fromName("test:fence[north=false,south=true]"));
    ASSERT_NE(one, nullptr);
    EXPECT_EQ(one->faces().size(), 1u);
    EXPECT_EQ(one->faces()[0].faceIndex, static_cast<int>(Face::PosZ));
}

TEST_F(BlockPropertyLoading, CubeFacesCanEachHaveATexture) {
    write(root_ / "blocks/oven.model",
          "texture: block/oven_side\n"
          "texture-top: block/oven_top\n"
          "texture-north: block/oven_front\n");
    ContentPackLoader loader("test");
    ASSERT_EQ(loader.loadBlocks(root_), 1u);
    const auto& set = loader.blockTextures().at(BlockTypeId::fromName("test:oven").id);
    EXPECT_EQ(set.forFace(Face::NegZ), "block/oven_front");
    EXPECT_EQ(set.forFace(Face::PosZ), "block/oven_side");
    EXPECT_EQ(set.forFace(Face::PosY), "block/oven_top");
    EXPECT_EQ(set.forFace(Face::NegY), "block/oven_side");
}

TEST_F(BlockPropertyLoading, GeometryThatIsACubeDrawsAsOne) {
    // Six whole faces, as a converter that only writes geometry would make
    std::string geom;
    const char* names[] = {"west", "east", "down", "up", "north", "south"};
    BlockModel source;
    source.setTexture("block/rock");
    BlockGeometry cube = cubeGeometry(source);
    for (const auto& face : cube.faces()) {
        geom += "face:" + std::string(names[face.faceIndex]) + ": block/rock\n";
        for (const auto& v : face.vertices) {
            geom += "    " + std::to_string(v.position.x) + " " + std::to_string(v.position.y) + " " +
                    std::to_string(v.position.z) + " " + std::to_string(v.uv.x) + " " +
                    std::to_string(v.uv.y) + "\n";
        }
    }
    write(root_ / "shapes/rock.geom", geom + "solid-faces: west east down up north south\n");
    write(root_ / "blocks/rock.model", "geometry: shapes/rock\n");
    ContentPackLoader loader("test");
    ASSERT_EQ(loader.loadBlocks(root_), 1u);
    auto id = BlockTypeId::fromName("test:rock");
    EXPECT_EQ(loader.getGeometry(id), nullptr);
    EXPECT_EQ(loader.blockTextures().at(id.id).forFace(Face::PosX), "block/rock");
}
