#include <gtest/gtest.h>
#include "finevox/block_type.hpp"
#include "finevox/world.hpp"

using namespace finevox;

class BlockTypeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any previously registered types (except air)
        // Note: In a real test setup, we might want a way to reset the registry
    }
};

// ============================================================================
// BlockType Tests
// ============================================================================

TEST_F(BlockTypeTest, DefaultBlockTypeHasFullCollision) {
    BlockType type;
    type.setShape(CollisionShape::FULL_BLOCK);

    EXPECT_TRUE(type.hasCollision());
    EXPECT_TRUE(type.hasHitShape());

    const auto& shape = type.collisionShape();
    EXPECT_FALSE(shape.isEmpty());
    EXPECT_EQ(shape.boxes().size(), 1);

    // Full block should have box from (0,0,0) to (1,1,1)
    const auto& box = shape.boxes()[0];
    EXPECT_FLOAT_EQ(box.min.x, 0.0f);
    EXPECT_FLOAT_EQ(box.min.y, 0.0f);
    EXPECT_FLOAT_EQ(box.min.z, 0.0f);
    EXPECT_FLOAT_EQ(box.max.x, 1.0f);
    EXPECT_FLOAT_EQ(box.max.y, 1.0f);
    EXPECT_FLOAT_EQ(box.max.z, 1.0f);
}

TEST_F(BlockTypeTest, NoCollisionBlock) {
    BlockType type;
    type.setNoCollision();

    EXPECT_FALSE(type.hasCollision());

    const auto& shape = type.collisionShape();
    EXPECT_TRUE(shape.isEmpty());
}

TEST_F(BlockTypeTest, DifferentCollisionAndHitShapes) {
    BlockType type;
    type.setCollisionShape(CollisionShape::NONE);
    type.setHitShape(CollisionShape::FULL_BLOCK);

    EXPECT_FALSE(type.hasCollision());
    EXPECT_TRUE(type.hasHitShape());

    const auto& collision = type.collisionShape();
    EXPECT_TRUE(collision.isEmpty());

    const auto& hit = type.hitShape();
    EXPECT_FALSE(hit.isEmpty());
}

TEST_F(BlockTypeTest, HitShapeFallsBackToCollision) {
    BlockType type;
    type.setCollisionShape(CollisionShape::FULL_BLOCK);
    // Don't set hit shape explicitly

    // Hit shape should fall back to collision shape
    EXPECT_TRUE(type.hasHitShape());

    const auto& hit = type.hitShape();
    EXPECT_FALSE(hit.isEmpty());
    EXPECT_EQ(hit.boxes().size(), 1);
}

TEST_F(BlockTypeTest, HalfSlabShape) {
    BlockType type;
    type.setShape(CollisionShape::HALF_SLAB_BOTTOM);

    const auto& shape = type.collisionShape();
    EXPECT_FALSE(shape.isEmpty());
    EXPECT_EQ(shape.boxes().size(), 1);

    // Half slab bottom: (0,0,0) to (1,0.5,1)
    const auto& box = shape.boxes()[0];
    EXPECT_FLOAT_EQ(box.min.y, 0.0f);
    EXPECT_FLOAT_EQ(box.max.y, 0.5f);
}

TEST_F(BlockTypeTest, RotatedShapes) {
    BlockType type;
    type.setShape(CollisionShape::HALF_SLAB_BOTTOM);

    // Get shape at identity rotation
    const auto& shape0 = type.collisionShape(Rotation::IDENTITY);
    EXPECT_EQ(shape0.boxes().size(), 1);

    // Get shape at a different rotation
    // Using ROTATE_X_90 to rotate half slab bottom around X axis
    const auto& shapeRotated = type.collisionShape(Rotation::ROTATE_X_90);
    EXPECT_EQ(shapeRotated.boxes().size(), 1);

    // The rotated shape should be different (rotated around center)
    // For half slab rotated 90 degrees around X, the "bottom" becomes "back"
}

TEST_F(BlockTypeTest, BlockProperties) {
    BlockType type;
    type.setOpaque(false)
        .setTransparent(true)
        .setLightEmission(14)
        .setHardness(0.5f);

    EXPECT_FALSE(type.isOpaque());
    EXPECT_TRUE(type.isTransparent());
    EXPECT_EQ(type.lightEmission(), 14);
    EXPECT_FLOAT_EQ(type.hardness(), 0.5f);
}

TEST_F(BlockTypeTest, BuilderChaining) {
    BlockType type;
    type.setShape(CollisionShape::FULL_BLOCK)
        .setOpaque(true)
        .setTransparent(false)
        .setLightEmission(0)
        .setHardness(1.5f);

    EXPECT_TRUE(type.hasCollision());
    EXPECT_TRUE(type.isOpaque());
    EXPECT_FALSE(type.isTransparent());
    EXPECT_EQ(type.lightEmission(), 0);
    EXPECT_FLOAT_EQ(type.hardness(), 1.5f);
}

// ============================================================================
// BlockRegistry Tests
// ============================================================================

TEST_F(BlockTypeTest, RegistrySingleton) {
    BlockRegistry& reg1 = BlockRegistry::global();
    BlockRegistry& reg2 = BlockRegistry::global();

    EXPECT_EQ(&reg1, &reg2);
}

TEST_F(BlockTypeTest, AirTypeRegisteredByDefault) {
    const BlockType& air = BlockRegistry::global().getType(AIR_BLOCK_TYPE);

    EXPECT_FALSE(air.hasCollision());
    EXPECT_FALSE(air.isOpaque());
    EXPECT_TRUE(air.isTransparent());
}

TEST_F(BlockTypeTest, RegisterAndRetrieveType) {
    BlockType stone;
    stone.setShape(CollisionShape::FULL_BLOCK)
         .setHardness(1.5f);

    BlockTypeId stoneId = BlockTypeId::fromName("test:stone");
    bool registered = BlockRegistry::global().registerType(stoneId, stone);
    EXPECT_TRUE(registered);

    const BlockType& retrieved = BlockRegistry::global().getType(stoneId);
    EXPECT_TRUE(retrieved.hasCollision());
    EXPECT_FLOAT_EQ(retrieved.hardness(), 1.5f);
}

TEST_F(BlockTypeTest, RegisterByName) {
    BlockType glass;
    glass.setShape(CollisionShape::FULL_BLOCK)
         .setOpaque(false)
         .setTransparent(true);

    bool registered = BlockRegistry::global().registerType("test:glass", glass);
    EXPECT_TRUE(registered);

    const BlockType& retrieved = BlockRegistry::global().getType("test:glass");
    EXPECT_FALSE(retrieved.isOpaque());
    EXPECT_TRUE(retrieved.isTransparent());
}

TEST_F(BlockTypeTest, UnregisteredTypeReturnsDefault) {
    BlockTypeId unknownId = BlockTypeId::fromName("test:unknown_block_xyz");

    const BlockType& retrieved = BlockRegistry::global().getType(unknownId);

    // Default type is full solid block
    EXPECT_TRUE(retrieved.hasCollision());
    EXPECT_TRUE(retrieved.isOpaque());
}

TEST_F(BlockTypeTest, DefaultTypeIsFullBlock) {
    const BlockType& def = BlockRegistry::defaultType();

    EXPECT_TRUE(def.hasCollision());
    EXPECT_TRUE(def.isOpaque());

    const auto& shape = def.collisionShape();
    EXPECT_FALSE(shape.isEmpty());
}

TEST_F(BlockTypeTest, AirTypeStaticAccessor) {
    const BlockType& air = BlockRegistry::airType();

    EXPECT_FALSE(air.hasCollision());
    EXPECT_FALSE(air.hasHitShape());
    EXPECT_FALSE(air.isOpaque());
}

TEST_F(BlockTypeTest, CannotOverwriteExistingType) {
    BlockType type1;
    type1.setHardness(1.0f);

    BlockType type2;
    type2.setHardness(2.0f);

    BlockTypeId id = BlockTypeId::fromName("test:no_overwrite");

    bool first = BlockRegistry::global().registerType(id, type1);
    EXPECT_TRUE(first);

    bool second = BlockRegistry::global().registerType(id, type2);
    EXPECT_FALSE(second);

    // Original should still be there
    const BlockType& retrieved = BlockRegistry::global().getType(id);
    EXPECT_FLOAT_EQ(retrieved.hardness(), 1.0f);
}

TEST_F(BlockTypeTest, HasTypeCheck) {
    BlockTypeId existingId = BlockTypeId::fromName("test:has_type_check");
    BlockTypeId nonExistingId = BlockTypeId::fromName("test:does_not_exist_xyz");

    EXPECT_FALSE(BlockRegistry::global().hasType(existingId));

    BlockType type;
    BlockRegistry::global().registerType(existingId, type);

    EXPECT_TRUE(BlockRegistry::global().hasType(existingId));
    EXPECT_FALSE(BlockRegistry::global().hasType(nonExistingId));
}

// ============================================================================
// BlockShapeProvider Tests
// ============================================================================

TEST_F(BlockTypeTest, CreateBlockShapeProvider) {
    World world;

    // Register a test block type
    BlockType testBlock;
    testBlock.setShape(CollisionShape::FULL_BLOCK);
    BlockTypeId testId = BlockTypeId::fromName("test:provider_block");
    BlockRegistry::global().registerType(testId, testBlock);

    // Place block in world
    world.setBlock(0, 0, 0, testId);

    // Create shape provider
    auto provider = createBlockShapeProvider(world);

    // Query collision at placed block
    const CollisionShape* shape = provider(BlockPos(0, 0, 0), RaycastMode::Collision);
    ASSERT_NE(shape, nullptr);
    EXPECT_FALSE(shape->isEmpty());

    // Query at empty position (air)
    const CollisionShape* airShape = provider(BlockPos(100, 100, 100), RaycastMode::Collision);
    EXPECT_EQ(airShape, nullptr);  // Air returns nullptr
}

TEST_F(BlockTypeTest, BlockShapeProviderRespectsRaycastMode) {
    World world;

    // Register a block with collision but no hit (like ladder)
    BlockType passThrough;
    passThrough.setCollisionShape(CollisionShape::NONE);
    passThrough.setHitShape(CollisionShape::FULL_BLOCK);
    BlockTypeId passId = BlockTypeId::fromName("test:pass_through");
    BlockRegistry::global().registerType(passId, passThrough);

    world.setBlock(5, 5, 5, passId);

    auto provider = createBlockShapeProvider(world);
    BlockPos pos(5, 5, 5);

    // Collision mode: should return nullptr (no collision)
    const CollisionShape* collision = provider(pos, RaycastMode::Collision);
    EXPECT_EQ(collision, nullptr);

    // Interaction mode: should return hit shape
    const CollisionShape* hit = provider(pos, RaycastMode::Interaction);
    ASSERT_NE(hit, nullptr);
    EXPECT_FALSE(hit->isEmpty());
}
