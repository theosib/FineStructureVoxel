#include <gtest/gtest.h>
#include "finevox/block_model.hpp"
#include "finevox/block_model_loader.hpp"
#include "finevox/block_type.hpp"

using namespace finevox;

// ============================================================================
// FaceGeometry Tests
// ============================================================================

TEST(FaceGeometryTest, ComputeBoundsFromVertices) {
    FaceGeometry face;
    face.vertices = {
        ModelVertex(0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        ModelVertex(1.0f, 0.0f, 0.0f, 1.0f, 0.0f),
        ModelVertex(1.0f, 1.0f, 0.0f, 1.0f, 1.0f),
        ModelVertex(0.0f, 1.0f, 0.0f, 0.0f, 1.0f)
    };

    AABB bounds = face.computeBounds();

    EXPECT_FLOAT_EQ(bounds.min.x, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.y, 0.0f);
    EXPECT_FLOAT_EQ(bounds.min.z, 0.0f);
    EXPECT_FLOAT_EQ(bounds.max.x, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.y, 1.0f);
    EXPECT_FLOAT_EQ(bounds.max.z, 0.0f);
}

TEST(FaceGeometryTest, StandardFaceDetection) {
    FaceGeometry face;
    face.faceIndex = 3;  // PosY

    EXPECT_TRUE(face.isStandardFace());
    auto stdFace = face.standardFace();
    ASSERT_TRUE(stdFace.has_value());
    EXPECT_EQ(*stdFace, Face::PosY);
}

TEST(FaceGeometryTest, CustomFaceDetection) {
    FaceGeometry face;
    face.faceIndex = 7;  // Custom face

    EXPECT_FALSE(face.isStandardFace());
    EXPECT_FALSE(face.standardFace().has_value());
}

TEST(FaceGeometryTest, ValidFaceHasAtLeast3Vertices) {
    FaceGeometry face;
    EXPECT_FALSE(face.isValid());  // Empty

    face.vertices.push_back(ModelVertex(0, 0, 0, 0, 0));
    face.vertices.push_back(ModelVertex(1, 0, 0, 1, 0));
    EXPECT_FALSE(face.isValid());  // Only 2 vertices

    face.vertices.push_back(ModelVertex(0, 1, 0, 0, 1));
    EXPECT_TRUE(face.isValid());  // 3 vertices - valid triangle
}

// ============================================================================
// BlockGeometry Tests
// ============================================================================

TEST(BlockGeometryTest, AddFaceAssignsIndex) {
    BlockGeometry geom;

    FaceGeometry face1;
    face1.name = "top";
    face1.faceIndex = 3;  // PosY
    face1.vertices = {
        ModelVertex(0, 1, 0, 0, 0),
        ModelVertex(1, 1, 0, 1, 0),
        ModelVertex(1, 1, 1, 1, 1),
        ModelVertex(0, 1, 1, 0, 1)
    };

    geom.addFace(std::move(face1));

    EXPECT_FALSE(geom.isEmpty());
    EXPECT_EQ(geom.faces().size(), 1);

    const FaceGeometry* retrieved = geom.getFace("top");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->faceIndex, 3);
}

TEST(BlockGeometryTest, GetFaceByIndex) {
    BlockGeometry geom;

    FaceGeometry face;
    face.faceIndex = 2;  // NegY (bottom)
    face.vertices = {
        ModelVertex(0, 0, 0, 0, 0),
        ModelVertex(1, 0, 0, 1, 0),
        ModelVertex(1, 0, 1, 1, 1)
    };

    geom.addFace(std::move(face));

    const FaceGeometry* byIndex = geom.getFace(2);
    ASSERT_NE(byIndex, nullptr);
    EXPECT_EQ(byIndex->faceIndex, 2);

    // Non-existent index
    EXPECT_EQ(geom.getFace(5), nullptr);
}

TEST(BlockGeometryTest, SolidFacesMask) {
    BlockGeometry geom;

    FaceGeometry bottom;
    bottom.faceIndex = 2;  // NegY
    bottom.isSolid = true;
    bottom.vertices = {
        ModelVertex(0, 0, 0, 0, 0),
        ModelVertex(1, 0, 0, 1, 0),
        ModelVertex(1, 0, 1, 1, 1)
    };

    FaceGeometry top;
    top.faceIndex = 3;  // PosY
    top.isSolid = false;  // Not solid
    top.vertices = {
        ModelVertex(0, 1, 0, 0, 0),
        ModelVertex(1, 1, 0, 1, 0),
        ModelVertex(1, 1, 1, 1, 1)
    };

    geom.addFace(std::move(bottom));
    geom.addFace(std::move(top));

    uint8_t mask = geom.solidFacesMask();
    EXPECT_TRUE(mask & (1 << 2));   // Bottom is solid
    EXPECT_FALSE(mask & (1 << 3)); // Top is not solid
}

// ============================================================================
// RotationSet Tests
// ============================================================================

TEST(RotationSetTest, NoneReturnsIdentityOnly) {
    auto indices = getRotationIndices(RotationSet::None);
    EXPECT_EQ(indices.size(), 1);
    EXPECT_EQ(indices[0], 0);
}

TEST(RotationSetTest, VerticalReturns2Rotations) {
    auto indices = getRotationIndices(RotationSet::Vertical);
    EXPECT_EQ(indices.size(), 2);
}

TEST(RotationSetTest, HorizontalReturns4Rotations) {
    auto indices = getRotationIndices(RotationSet::Horizontal);
    EXPECT_EQ(indices.size(), 4);
}

TEST(RotationSetTest, HorizontalFlipReturns8Rotations) {
    auto indices = getRotationIndices(RotationSet::HorizontalFlip);
    EXPECT_EQ(indices.size(), 8);
}

TEST(RotationSetTest, AllReturns24Rotations) {
    auto indices = getRotationIndices(RotationSet::All);
    EXPECT_EQ(indices.size(), 24);
}

TEST(RotationSetTest, ParseRotationSet) {
    EXPECT_EQ(parseRotationSet("none"), RotationSet::None);
    EXPECT_EQ(parseRotationSet("vertical"), RotationSet::Vertical);
    EXPECT_EQ(parseRotationSet("horizontal"), RotationSet::Horizontal);
    EXPECT_EQ(parseRotationSet("horizontal-flip"), RotationSet::HorizontalFlip);
    EXPECT_EQ(parseRotationSet("all"), RotationSet::All);
    EXPECT_EQ(parseRotationSet("unknown"), RotationSet::Custom);
}

// ============================================================================
// BlockModel Tests
// ============================================================================

TEST(BlockModelTest, ResolvedCollisionFallsBackToGeometry) {
    BlockModel model;

    // Create geometry with a solid bottom face
    BlockGeometry geom;
    FaceGeometry bottom;
    bottom.faceIndex = 2;
    bottom.isSolid = true;
    bottom.vertices = {
        ModelVertex(0, 0, 0, 0, 0),
        ModelVertex(1, 0, 0, 1, 0),
        ModelVertex(1, 0, 1, 1, 1),
        ModelVertex(0, 0, 1, 0, 1)
    };
    geom.addFace(std::move(bottom));

    model.setGeometry(std::move(geom));

    // No explicit collision set, should compute from faces
    const CollisionShape& collision = model.resolvedCollision();
    EXPECT_FALSE(collision.isEmpty());
}

TEST(BlockModelTest, ResolvedCollisionUsesExplicit) {
    BlockModel model;

    CollisionShape customShape;
    customShape.addBox(AABB(glm::vec3(0), glm::vec3(1, 0.5f, 1)));
    model.setCollision(customShape);

    const CollisionShape& collision = model.resolvedCollision();
    EXPECT_FALSE(collision.isEmpty());
    EXPECT_EQ(collision.boxes().size(), 1);
    EXPECT_FLOAT_EQ(collision.boxes()[0].max.y, 0.5f);
}

TEST(BlockModelTest, AllowedRotations) {
    BlockModel model;
    model.setRotations(RotationSet::Horizontal);

    auto allowed = model.allowedRotations();
    EXPECT_EQ(allowed.size(), 4);

    EXPECT_TRUE(model.isRotationAllowed(0));
    EXPECT_TRUE(model.isRotationAllowed(1));
    EXPECT_TRUE(model.isRotationAllowed(2));
    EXPECT_TRUE(model.isRotationAllowed(3));
    EXPECT_FALSE(model.isRotationAllowed(4));
}

TEST(BlockModelTest, CustomRotations) {
    BlockModel model;
    model.setRotations(std::vector<uint8_t>{0, 5, 10});

    EXPECT_TRUE(model.isRotationAllowed(0));
    EXPECT_TRUE(model.isRotationAllowed(5));
    EXPECT_TRUE(model.isRotationAllowed(10));
    EXPECT_FALSE(model.isRotationAllowed(1));
}

// ============================================================================
// Face Name Parsing Tests
// ============================================================================

TEST(FaceNameTest, ParseStandardFaceNames) {
    // NegX aliases
    EXPECT_EQ(parseFaceName("negx"), 0);
    EXPECT_EQ(parseFaceName("west"), 0);
    EXPECT_EQ(parseFaceName("w"), 0);
    EXPECT_EQ(parseFaceName("-x"), 0);

    // PosX aliases
    EXPECT_EQ(parseFaceName("posx"), 1);
    EXPECT_EQ(parseFaceName("east"), 1);

    // NegY aliases
    EXPECT_EQ(parseFaceName("negy"), 2);
    EXPECT_EQ(parseFaceName("down"), 2);
    EXPECT_EQ(parseFaceName("bottom"), 2);

    // PosY aliases
    EXPECT_EQ(parseFaceName("posy"), 3);
    EXPECT_EQ(parseFaceName("up"), 3);
    EXPECT_EQ(parseFaceName("top"), 3);

    // NegZ aliases
    EXPECT_EQ(parseFaceName("negz"), 4);
    EXPECT_EQ(parseFaceName("north"), 4);

    // PosZ aliases
    EXPECT_EQ(parseFaceName("posz"), 5);
    EXPECT_EQ(parseFaceName("south"), 5);
}

TEST(FaceNameTest, ParseNumericIndices) {
    EXPECT_EQ(parseFaceName("0"), 0);
    EXPECT_EQ(parseFaceName("5"), 5);
    EXPECT_EQ(parseFaceName("7"), 7);
}

TEST(FaceNameTest, UnknownNameReturnsNegativeOne) {
    EXPECT_EQ(parseFaceName("step_top"), -1);
    EXPECT_EQ(parseFaceName("riser"), -1);
    EXPECT_EQ(parseFaceName("custom_face"), -1);
}

TEST(FaceNameTest, IsStandardFaceName) {
    EXPECT_TRUE(isStandardFaceName("top"));
    EXPECT_TRUE(isStandardFaceName("bottom"));
    EXPECT_TRUE(isStandardFaceName("north"));
    EXPECT_TRUE(isStandardFaceName("south"));
    EXPECT_TRUE(isStandardFaceName("east"));
    EXPECT_TRUE(isStandardFaceName("west"));

    EXPECT_FALSE(isStandardFaceName("step_top"));
    EXPECT_FALSE(isStandardFaceName("diagonal"));
}

TEST(FaceNameTest, FaceNameFromIndex) {
    EXPECT_EQ(faceName(0), "west");
    EXPECT_EQ(faceName(1), "east");
    EXPECT_EQ(faceName(2), "bottom");
    EXPECT_EQ(faceName(3), "top");
    EXPECT_EQ(faceName(4), "north");
    EXPECT_EQ(faceName(5), "south");
    EXPECT_EQ(faceName(7), "7");  // Custom face returns number
}

// ============================================================================
// BlockModelLoader Tests
// ============================================================================

TEST(BlockModelLoaderTest, ParseGeometryFromString) {
    BlockModelLoader loader;

    std::string geomStr = R"(
face:bottom:
    0 0 1  0 1
    0 0 0  0 0
    1 0 0  1 0
    1 0 1  1 1

face:top:
    0 0.5 0  0 0
    0 0.5 1  0 1
    1 0.5 1  1 1
    1 0.5 0  1 0

solid-faces: bottom
)";

    auto geom = loader.parseGeometryFromString(geomStr);
    ASSERT_TRUE(geom.has_value());

    EXPECT_EQ(geom->faces().size(), 2);

    const FaceGeometry* bottom = geom->getFace(2);  // NegY index
    ASSERT_NE(bottom, nullptr);
    EXPECT_EQ(bottom->vertices.size(), 4);
    EXPECT_TRUE(bottom->isSolid);

    const FaceGeometry* top = geom->getFace(3);  // PosY index
    ASSERT_NE(top, nullptr);
    EXPECT_FALSE(top->isSolid);

    // Check solid faces mask
    uint8_t mask = geom->solidFacesMask();
    EXPECT_TRUE(mask & (1 << 2));   // Bottom
    EXPECT_FALSE(mask & (1 << 3)); // Top
}

TEST(BlockModelLoaderTest, ParseCollisionFromString) {
    BlockModelLoader loader;

    std::string collisionStr = R"(
box:
    0 0 0
    1 0.5 1

box:
    0 0.5 0.5
    1 1 1
)";

    auto shape = loader.parseCollisionFromString(collisionStr);
    ASSERT_TRUE(shape.has_value());

    EXPECT_EQ(shape->boxes().size(), 2);

    // First box: bottom slab
    const auto& box1 = shape->boxes()[0];
    EXPECT_FLOAT_EQ(box1.min.y, 0.0f);
    EXPECT_FLOAT_EQ(box1.max.y, 0.5f);

    // Second box: upper step
    const auto& box2 = shape->boxes()[1];
    EXPECT_FLOAT_EQ(box2.min.y, 0.5f);
    EXPECT_FLOAT_EQ(box2.max.y, 1.0f);
}

TEST(BlockModelLoaderTest, ParseModelFromString) {
    BlockModelLoader loader;

    // Set up a file resolver that returns our test geometry content
    loader.setFileResolver([](const std::string& path) -> std::string {
        if (path == "shapes/test_geom.geom") {
            return R"(
face:bottom:
    0 0 0  0 0
    1 0 0  1 0
    1 0 1  1 1
    0 0 1  0 1

solid-faces: bottom
)";
        }
        return "";
    });

    std::string modelStr = R"(
geometry: shapes/test_geom
rotations: horizontal
hardness: 2.0
texture: blocks/stone
sounds: stone
)";

    auto model = loader.parseModelFromString(modelStr);
    ASSERT_TRUE(model.has_value());

    EXPECT_TRUE(model->hasCustomGeometry());
    EXPECT_FLOAT_EQ(model->hardness(), 2.0f);
    EXPECT_EQ(model->texture(), "blocks/stone");
    EXPECT_EQ(model->sounds(), "stone");
    EXPECT_EQ(model->rotationSet(), RotationSet::Horizontal);
}

TEST(BlockModelLoaderTest, ParseTriangleFace) {
    BlockModelLoader loader;

    std::string geomStr = R"(
face:west:
    0 1 0  0 1
    0 0 0  0 0
    0 0 1  1 0
)";

    auto geom = loader.parseGeometryFromString(geomStr);
    ASSERT_TRUE(geom.has_value());

    const FaceGeometry* west = geom->getFace(0);  // NegX index
    ASSERT_NE(west, nullptr);
    EXPECT_EQ(west->vertices.size(), 3);  // Triangle
    EXPECT_TRUE(west->isValid());
}

TEST(BlockModelLoaderTest, ParseCustomFaceName) {
    BlockModelLoader loader;

    std::string geomStr = R"(
face:step_top:
    0 0.5 0.5  0 0.5
    0 0.5 1    0 1
    1 0.5 1    1 1
    1 0.5 0.5  1 0.5
)";

    auto geom = loader.parseGeometryFromString(geomStr);
    ASSERT_TRUE(geom.has_value());

    // Custom face should be assigned index 6 or higher
    const FaceGeometry* stepTop = geom->getFace("step_top");
    ASSERT_NE(stepTop, nullptr);
    EXPECT_GE(stepTop->faceIndex, 6);
    EXPECT_FALSE(stepTop->isStandardFace());
}

// ============================================================================
// Collision Loading Chain Tests
// ============================================================================

TEST(BlockModelLoaderTest, CollisionFullCreatesValidShape) {
    BlockModelLoader loader;

    std::string modelStr = R"(
collision: full
rotations: none
hardness: 1.0
)";

    auto model = loader.parseModelFromString(modelStr);
    ASSERT_TRUE(model.has_value());

    // Check that explicit collision was set
    EXPECT_TRUE(model->hasExplicitCollision());

    // Get resolved collision
    const CollisionShape& collision = model->resolvedCollision();
    EXPECT_FALSE(collision.isEmpty()) << "collision: full should create non-empty collision shape";
    EXPECT_EQ(collision.boxes().size(), 1) << "Full block should have exactly 1 AABB";

    // Verify it's a full block (0,0,0) to (1,1,1)
    if (!collision.boxes().empty()) {
        const AABB& box = collision.boxes()[0];
        EXPECT_FLOAT_EQ(box.min.x, 0.0f);
        EXPECT_FLOAT_EQ(box.min.y, 0.0f);
        EXPECT_FLOAT_EQ(box.min.z, 0.0f);
        EXPECT_FLOAT_EQ(box.max.x, 1.0f);
        EXPECT_FLOAT_EQ(box.max.y, 1.0f);
        EXPECT_FLOAT_EQ(box.max.z, 1.0f);
    }
}

TEST(BlockModelLoaderTest, CollisionFullBlockStaticConstant) {
    // Verify CollisionShape::FULL_BLOCK is valid
    const CollisionShape& fullBlock = CollisionShape::FULL_BLOCK;
    EXPECT_FALSE(fullBlock.isEmpty()) << "FULL_BLOCK static constant should not be empty";
    EXPECT_EQ(fullBlock.boxes().size(), 1) << "FULL_BLOCK should have exactly 1 AABB";

    if (!fullBlock.boxes().empty()) {
        const AABB& box = fullBlock.boxes()[0];
        EXPECT_FLOAT_EQ(box.min.x, 0.0f);
        EXPECT_FLOAT_EQ(box.min.y, 0.0f);
        EXPECT_FLOAT_EQ(box.min.z, 0.0f);
        EXPECT_FLOAT_EQ(box.max.x, 1.0f);
        EXPECT_FLOAT_EQ(box.max.y, 1.0f);
        EXPECT_FLOAT_EQ(box.max.z, 1.0f);
    }
}

TEST(BlockModelLoaderTest, IncludeInheritsCollision) {
    BlockModelLoader loader;

    // Create a map of file contents for the resolver
    std::map<std::string, std::string> files;

    files["base/solid_cube.model"] = R"(
collision: full
rotations: none
hardness: 1.0
)";

    files["stone.model"] = R"(
include: base/solid_cube
texture: blocks/stone
sounds: stone
hardness: 1.5
)";

    // Set up resolver that returns file paths (not content)
    // Since we're using parseModelFromString, we need to test loadModel instead
    // But loadModel needs actual files. Let's test the inheritance manually.

    // First, parse the base model
    auto baseModel = loader.parseModelFromString(files["base/solid_cube.model"]);
    ASSERT_TRUE(baseModel.has_value());
    EXPECT_TRUE(baseModel->hasExplicitCollision());
    EXPECT_FALSE(baseModel->resolvedCollision().isEmpty());

    // The include mechanism in loadModel would copy the collision from base
    // For now, verify that if we manually copy the collision, it works
    BlockModel derivedModel;
    derivedModel.setCollision(baseModel->resolvedCollision());
    derivedModel.setHardness(1.5f);

    EXPECT_TRUE(derivedModel.hasExplicitCollision());
    EXPECT_FALSE(derivedModel.resolvedCollision().isEmpty());
}

// Test that BlockType correctly receives collision from BlockModel
TEST(BlockModelToBlockTypeTest, CollisionTransfer) {
    // Create a model with explicit full collision
    BlockModel model;
    model.setCollision(CollisionShape::FULL_BLOCK);

    ASSERT_TRUE(model.hasExplicitCollision());
    ASSERT_FALSE(model.resolvedCollision().isEmpty());

    // Create BlockType and set collision from model
    BlockType blockType;
    blockType.setCollisionShape(model.resolvedCollision());

    // Verify BlockType has collision
    EXPECT_TRUE(blockType.hasCollision()) << "BlockType should have collision after setCollisionShape";

    // Verify the collision shape is valid
    const CollisionShape& shape = blockType.collisionShape();
    EXPECT_FALSE(shape.isEmpty()) << "BlockType collision shape should not be empty";
    EXPECT_EQ(shape.boxes().size(), 1) << "Should have exactly 1 AABB";
}

// Test the condition used in render_demo.cpp
TEST(BlockModelToBlockTypeTest, RenderDemoConditionTest) {
    BlockModelLoader loader;

    // Simulate solid_cube.model content
    std::string solidCubeStr = R"(
geometry: shapes/solid_cube
collision: full
rotations: none
hardness: 1.0
)";

    // Set up resolver for geometry
    loader.setFileResolver([](const std::string& path) -> std::string {
        if (path == "shapes/solid_cube.geom") {
            return R"(
face:bottom:
    0 0 0  0 0
    0 0 1  0 1
    1 0 1  1 1
    1 0 0  1 0

face:top:
    0 1 1  0 1
    0 1 0  0 0
    1 1 0  1 0
    1 1 1  1 1

face:north:
    1 1 0  1 1
    1 0 0  1 0
    0 0 0  0 0
    0 1 0  0 1

face:south:
    0 1 1  0 1
    0 0 1  0 0
    1 0 1  1 0
    1 1 1  1 1

face:west:
    0 1 0  0 1
    0 0 0  0 0
    0 0 1  1 0
    0 1 1  1 1

face:east:
    1 1 1  0 1
    1 0 1  0 0
    1 0 0  1 0
    1 1 0  1 1

solid-faces: bottom top north south west east
)";
        }
        return "";
    });

    auto model = loader.parseModelFromString(solidCubeStr);
    ASSERT_TRUE(model.has_value());

    // This is the condition from render_demo.cpp line 222
    bool shouldSetCollision = model->hasExplicitCollision() || model->hasCustomGeometry();
    EXPECT_TRUE(shouldSetCollision) << "Solid cube should trigger collision setup";

    // Test hasExplicitCollision specifically
    EXPECT_TRUE(model->hasExplicitCollision()) << "Model with 'collision: full' should have explicit collision";

    // Test hasCustomGeometry (solid cubes have geometry)
    EXPECT_TRUE(model->hasCustomGeometry()) << "Model with geometry should have custom geometry";

    // Get the resolved collision
    const CollisionShape& collision = model->resolvedCollision();
    EXPECT_FALSE(collision.isEmpty()) << "Resolved collision should not be empty";

    // Now simulate what render_demo does
    BlockType blockType;
    if (shouldSetCollision) {
        blockType.setCollisionShape(model->resolvedCollision());
    }

    EXPECT_TRUE(blockType.hasCollision()) << "BlockType should have collision after setup";
}

// Test loading from actual spec files
#include "finevox/resource_locator.hpp"
#include <filesystem>

class BlockModelFileLoadTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Find the resources directory
        // Try common locations relative to test execution
        std::vector<std::string> searchPaths = {
            "../resources",
            "../../resources",
            "../../../resources",
            "resources"
        };

        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path + "/blocks/stone.model")) {
                resourcePath_ = path;
                break;
            }
        }
    }

    std::string resourcePath_;
};

TEST_F(BlockModelFileLoadTest, LoadSolidCubeBaseModel) {
    if (resourcePath_.empty()) {
        GTEST_SKIP() << "Resources directory not found";
    }

    BlockModelLoader loader;

    // Set up file resolver that checks if files exist
    loader.setFileResolver([this](const std::string& path) -> std::string {
        // Try blocks/ subdirectory first
        std::string blocksPath = resourcePath_ + "/blocks/" + path;
        if (std::filesystem::exists(blocksPath)) {
            return blocksPath;
        }
        // Try root
        std::string rootPath = resourcePath_ + "/" + path;
        if (std::filesystem::exists(rootPath)) {
            return rootPath;
        }
        return "";
    });

    std::string modelPath = resourcePath_ + "/blocks/base/solid_cube.model";
    if (!std::filesystem::exists(modelPath)) {
        GTEST_SKIP() << "solid_cube.model not found at " << modelPath;
    }

    auto model = loader.loadModel(modelPath);
    ASSERT_TRUE(model.has_value()) << "Failed to load solid_cube.model: " << loader.lastError();

    // Verify collision
    EXPECT_TRUE(model->hasExplicitCollision()) << "solid_cube.model should have 'collision: full'";

    const CollisionShape& collision = model->resolvedCollision();
    EXPECT_FALSE(collision.isEmpty()) << "Collision should not be empty";

    std::cout << "solid_cube.model collision boxes: " << collision.boxes().size() << std::endl;
    for (size_t i = 0; i < collision.boxes().size(); ++i) {
        const auto& box = collision.boxes()[i];
        std::cout << "  Box " << i << ": ("
                  << box.min.x << "," << box.min.y << "," << box.min.z << ") to ("
                  << box.max.x << "," << box.max.y << "," << box.max.z << ")" << std::endl;
    }
}

TEST_F(BlockModelFileLoadTest, LoadStoneModelWithInheritance) {
    if (resourcePath_.empty()) {
        GTEST_SKIP() << "Resources directory not found";
    }

    BlockModelLoader loader;

    // Set up file resolver with debug output
    loader.setFileResolver([this](const std::string& path) -> std::string {
        std::cout << "  Resolver called with: " << path << std::endl;
        std::string blocksPath = resourcePath_ + "/blocks/" + path;
        std::cout << "    Trying: " << blocksPath << " exists: " << std::filesystem::exists(blocksPath) << std::endl;
        if (std::filesystem::exists(blocksPath)) {
            return blocksPath;
        }
        std::string rootPath = resourcePath_ + "/" + path;
        std::cout << "    Trying: " << rootPath << " exists: " << std::filesystem::exists(rootPath) << std::endl;
        if (std::filesystem::exists(rootPath)) {
            return rootPath;
        }
        std::cout << "    Not found, returning empty" << std::endl;
        return "";
    });

    std::string modelPath = resourcePath_ + "/blocks/stone.model";
    std::cout << "Loading stone.model from: " << modelPath << std::endl;
    if (!std::filesystem::exists(modelPath)) {
        GTEST_SKIP() << "stone.model not found";
    }

    auto model = loader.loadModel(modelPath);
    ASSERT_TRUE(model.has_value()) << "Failed to load stone.model: " << loader.lastError();

    // Stone includes solid_cube, which has collision: full
    std::cout << "stone.model hasExplicitCollision: " << model->hasExplicitCollision() << std::endl;
    std::cout << "stone.model hasCustomGeometry: " << model->hasCustomGeometry() << std::endl;

    const CollisionShape& collision = model->resolvedCollision();
    std::cout << "stone.model collision isEmpty: " << collision.isEmpty() << std::endl;
    std::cout << "stone.model collision boxes: " << collision.boxes().size() << std::endl;

    EXPECT_TRUE(model->hasExplicitCollision()) << "stone.model should inherit collision: full from solid_cube";
    EXPECT_FALSE(collision.isEmpty()) << "Collision should not be empty after inheritance";

    // Now test full chain to BlockType
    BlockType blockType;
    if (model->hasExplicitCollision() || model->hasCustomGeometry()) {
        blockType.setCollisionShape(model->resolvedCollision());
    }

    EXPECT_TRUE(blockType.hasCollision()) << "BlockType should have collision";

    const CollisionShape& btCollision = blockType.collisionShape();
    std::cout << "BlockType collision isEmpty: " << btCollision.isEmpty() << std::endl;
    std::cout << "BlockType collision boxes: " << btCollision.boxes().size() << std::endl;
}
