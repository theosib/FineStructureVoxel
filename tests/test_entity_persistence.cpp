#include <gtest/gtest.h>
#include "finevox/core/entity_serializer.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/graphics_event_queue.hpp"

using namespace finevox;

// ============================================================================
// Test Fixture
// ============================================================================

class EntitySerializerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register a test mob type
        EntityTypeDef def;
        def.name = "persist_test_zombie";
        def.maxHealth = 30.0f;
        def.halfExtents = glm::vec3(0.4f, 1.0f, 0.4f);
        def.hasGravity = true;
        EntityTypeRegistry::global().registerType("persist_test_zombie", std::move(def));
    }
};

// ============================================================================
// entityToData / entityFromData — Basic Entity
// ============================================================================

TEST_F(EntitySerializerTest, BasicEntityRoundTrip) {
    Entity original(EntityId{42}, EntityType::ItemDrop);
    original.setPosition(Vec3(10.5f, 64.0f, -3.25f));
    original.setVelocity(Vec3(0.0f, -9.8f, 1.5f));
    original.setHalfExtents(Vec3(0.25f, 0.25f, 0.25f));
    original.setYaw(90.0f);
    original.setPitch(-45.0f);
    original.setOnGround(true);
    original.setHasGravity(false);
    original.setAnimation(3);

    auto dc = EntitySerializer::entityToData(original);
    ASSERT_NE(dc, nullptr);

    auto restored = EntitySerializer::entityFromData(*dc);
    ASSERT_NE(restored, nullptr);

    // ID should be INVALID (regenerated on load)
    EXPECT_EQ(restored->id(), INVALID_ENTITY_ID);

    // Type
    EXPECT_EQ(restored->type(), EntityType::ItemDrop);

    // Position
    EXPECT_NEAR(restored->position().x, 10.5f, 0.01f);
    EXPECT_NEAR(restored->position().y, 64.0f, 0.01f);
    EXPECT_NEAR(restored->position().z, -3.25f, 0.01f);

    // Velocity
    EXPECT_NEAR(restored->velocity().x, 0.0f, 0.01f);
    EXPECT_NEAR(restored->velocity().y, -9.8f, 0.01f);
    EXPECT_NEAR(restored->velocity().z, 1.5f, 0.01f);

    // Half extents
    EXPECT_NEAR(restored->halfExtents().x, 0.25f, 0.01f);
    EXPECT_NEAR(restored->halfExtents().y, 0.25f, 0.01f);
    EXPECT_NEAR(restored->halfExtents().z, 0.25f, 0.01f);

    // Look direction
    EXPECT_NEAR(restored->yaw(), 90.0f, 0.01f);
    EXPECT_NEAR(restored->pitch(), -45.0f, 0.01f);

    // Physics
    EXPECT_TRUE(restored->isOnGround());
    EXPECT_FALSE(restored->hasGravity());

    // Animation
    EXPECT_EQ(restored->animationId(), 3);
}

TEST_F(EntitySerializerTest, DefaultEntityValues) {
    Entity original(EntityId{1}, EntityType::Player);
    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);

    EXPECT_EQ(restored->type(), EntityType::Player);
    EXPECT_NEAR(restored->position().x, 0.0f, 0.01f);
    EXPECT_NEAR(restored->position().y, 0.0f, 0.01f);
    EXPECT_NEAR(restored->position().z, 0.0f, 0.01f);
    EXPECT_FALSE(restored->isOnGround());
    EXPECT_TRUE(restored->hasGravity());
    EXPECT_EQ(restored->animationId(), 0);
}

// ============================================================================
// entityToData / entityFromData — MobEntity
// ============================================================================

TEST_F(EntitySerializerTest, MobEntityRoundTrip) {
    auto typeId = EntityTypeId::fromName("persist_test_zombie");
    MobEntity original(EntityId{100}, typeId);
    original.setPosition(Vec3(5.0f, 70.0f, 15.0f));
    original.setVelocity(Vec3(1.0f, 0.0f, -0.5f));
    original.setMaxHealth(30.0f);
    original.setHealth(15.0f);
    original.setSpeedMultiplier(0.75f);
    original.setYaw(180.0f);
    original.setPitch(10.0f);
    original.setOnGround(true);

    auto dc = EntitySerializer::entityToData(original);
    ASSERT_NE(dc, nullptr);

    auto restored = EntitySerializer::entityFromData(*dc);
    ASSERT_NE(restored, nullptr);

    // Should be a MobEntity
    auto* mob = dynamic_cast<MobEntity*>(restored.get());
    ASSERT_NE(mob, nullptr);

    // Type ID preserved
    EXPECT_EQ(mob->typeId(), typeId);
    EXPECT_EQ(mob->typeId().name(), "persist_test_zombie");

    // Health
    EXPECT_NEAR(mob->health(), 15.0f, 0.01f);
    EXPECT_NEAR(mob->maxHealth(), 30.0f, 0.01f);

    // Speed
    EXPECT_NEAR(mob->speedMultiplier(), 0.75f, 0.01f);

    // Position
    EXPECT_NEAR(mob->position().x, 5.0f, 0.01f);
    EXPECT_NEAR(mob->position().y, 70.0f, 0.01f);
    EXPECT_NEAR(mob->position().z, 15.0f, 0.01f);

    // Look
    EXPECT_NEAR(mob->yaw(), 180.0f, 0.01f);
    EXPECT_NEAR(mob->pitch(), 10.0f, 0.01f);

    // Physics
    EXPECT_TRUE(mob->isOnGround());
}

TEST_F(EntitySerializerTest, MobEntityWithLowHealth) {
    auto typeId = EntityTypeId::fromName("persist_test_zombie");
    MobEntity original(EntityId{101}, typeId);
    original.setMaxHealth(30.0f);
    original.setHealth(1.0f);

    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);
    auto* mob = dynamic_cast<MobEntity*>(restored.get());
    ASSERT_NE(mob, nullptr);

    EXPECT_NEAR(mob->health(), 1.0f, 0.01f);
    EXPECT_NEAR(mob->maxHealth(), 30.0f, 0.01f);
}

// ============================================================================
// Extra Data (DataContainer)
// ============================================================================

TEST_F(EntitySerializerTest, EntityWithExtraData) {
    Entity original(EntityId{200}, EntityType::Player);
    original.setPosition(Vec3(1.0f, 2.0f, 3.0f));

    auto& data = original.getOrCreateEntityData();
    data.set<int64_t>("score", 42);
    data.set<double>("experience", 1234.5);
    data.set<std::string>("name", "TestPlayer");

    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);

    ASSERT_NE(restored->entityData(), nullptr);
    EXPECT_EQ(restored->entityData()->get<int64_t>("score"), 42);
    EXPECT_NEAR(restored->entityData()->get<double>("experience"), 1234.5, 0.01);
    EXPECT_EQ(restored->entityData()->get<std::string>("name"), "TestPlayer");
}

TEST_F(EntitySerializerTest, EntityWithoutExtraData) {
    Entity original(EntityId{201}, EntityType::Arrow);
    original.setPosition(Vec3(5.0f, 5.0f, 5.0f));

    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);

    EXPECT_EQ(restored->entityData(), nullptr);
}

TEST_F(EntitySerializerTest, MobEntityWithExtraData) {
    auto typeId = EntityTypeId::fromName("persist_test_zombie");
    MobEntity original(EntityId{202}, typeId);
    original.setMaxHealth(30.0f);
    original.setHealth(20.0f);

    auto& data = original.getOrCreateEntityData();
    data.set<int64_t>("aggro_target", 999);
    data.set<std::string>("variant", "armored");

    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);

    auto* mob = dynamic_cast<MobEntity*>(restored.get());
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->health(), 20.0f, 0.01f);

    ASSERT_NE(mob->entityData(), nullptr);
    EXPECT_EQ(mob->entityData()->get<int64_t>("aggro_target"), 999);
    EXPECT_EQ(mob->entityData()->get<std::string>("variant"), "armored");
}

// ============================================================================
// serialize / deserialize — Multiple Entities via CBOR
// ============================================================================

TEST_F(EntitySerializerTest, SerializeEmpty) {
    std::vector<const Entity*> entities;
    auto bytes = EntitySerializer::serialize(entities);
    EXPECT_FALSE(bytes.empty());  // Still has the "count: 0" wrapper

    auto result = EntitySerializer::deserialize(bytes);
    EXPECT_TRUE(result.empty());
}

TEST_F(EntitySerializerTest, SerializeSingleEntity) {
    Entity e(EntityId{1}, EntityType::Boat);
    e.setPosition(Vec3(100.0f, 63.0f, -200.0f));

    std::vector<const Entity*> entities = {&e};
    auto bytes = EntitySerializer::serialize(entities);
    EXPECT_FALSE(bytes.empty());

    auto result = EntitySerializer::deserialize(bytes);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0]->type(), EntityType::Boat);
    EXPECT_NEAR(result[0]->position().x, 100.0f, 0.01f);
}

TEST_F(EntitySerializerTest, SerializeMultipleEntities) {
    Entity e1(EntityId{1}, EntityType::Arrow);
    e1.setPosition(Vec3(1.0f, 2.0f, 3.0f));
    e1.setVelocity(Vec3(10.0f, 5.0f, 0.0f));

    Entity e2(EntityId{2}, EntityType::ItemDrop);
    e2.setPosition(Vec3(4.0f, 5.0f, 6.0f));

    auto typeId = EntityTypeId::fromName("persist_test_zombie");
    MobEntity e3(EntityId{3}, typeId);
    e3.setPosition(Vec3(7.0f, 8.0f, 9.0f));
    e3.setMaxHealth(30.0f);
    e3.setHealth(25.0f);

    std::vector<const Entity*> entities = {&e1, &e2, &e3};
    auto bytes = EntitySerializer::serialize(entities);
    EXPECT_FALSE(bytes.empty());

    auto result = EntitySerializer::deserialize(bytes);
    ASSERT_EQ(result.size(), 3u);

    // Entity 1 — Arrow
    EXPECT_EQ(result[0]->type(), EntityType::Arrow);
    EXPECT_NEAR(result[0]->position().x, 1.0f, 0.01f);
    EXPECT_NEAR(result[0]->velocity().x, 10.0f, 0.01f);

    // Entity 2 — ItemDrop
    EXPECT_EQ(result[1]->type(), EntityType::ItemDrop);
    EXPECT_NEAR(result[1]->position().x, 4.0f, 0.01f);

    // Entity 3 — MobEntity (zombie)
    auto* mob = dynamic_cast<MobEntity*>(result[2].get());
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->position().x, 7.0f, 0.01f);
    EXPECT_NEAR(mob->health(), 25.0f, 0.01f);
    EXPECT_NEAR(mob->maxHealth(), 30.0f, 0.01f);
}

TEST_F(EntitySerializerTest, SerializeWithExtraData) {
    Entity e(EntityId{1}, EntityType::Minecart);
    e.setPosition(Vec3(50.0f, 60.0f, 70.0f));
    auto& data = e.getOrCreateEntityData();
    data.set<int64_t>("fuel", 100);
    data.set<double>("speed_limit", 8.0);

    std::vector<const Entity*> entities = {&e};
    auto bytes = EntitySerializer::serialize(entities);
    auto result = EntitySerializer::deserialize(bytes);

    ASSERT_EQ(result.size(), 1u);
    ASSERT_NE(result[0]->entityData(), nullptr);
    EXPECT_EQ(result[0]->entityData()->get<int64_t>("fuel"), 100);
    EXPECT_NEAR(result[0]->entityData()->get<double>("speed_limit"), 8.0, 0.01);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(EntitySerializerTest, DeserializeInvalidData) {
    std::vector<uint8_t> garbage = {0xFF, 0xFE, 0x00, 0x01};
    auto result = EntitySerializer::deserialize(garbage);
    EXPECT_TRUE(result.empty());
}

TEST_F(EntitySerializerTest, DeserializeEmptyData) {
    std::vector<uint8_t> empty;
    auto result = EntitySerializer::deserialize(empty);
    EXPECT_TRUE(result.empty());
}

TEST_F(EntitySerializerTest, NegativePosition) {
    Entity original(EntityId{1}, EntityType::Player);
    original.setPosition(Vec3(-512.5f, -64.0f, -1024.75f));

    auto dc = EntitySerializer::entityToData(original);
    auto restored = EntitySerializer::entityFromData(*dc);

    EXPECT_NEAR(restored->position().x, -512.5f, 0.01f);
    EXPECT_NEAR(restored->position().y, -64.0f, 0.01f);
    EXPECT_NEAR(restored->position().z, -1024.75f, 0.01f);
}

TEST_F(EntitySerializerTest, LargeEntityCount) {
    std::vector<Entity> entities;
    std::vector<const Entity*> ptrs;
    entities.reserve(50);
    ptrs.reserve(50);

    for (int i = 0; i < 50; ++i) {
        entities.emplace_back(EntityId{static_cast<uint32_t>(i)}, EntityType::Chicken);
        entities.back().setPosition(Vec3(
            static_cast<float>(i),
            64.0f,
            static_cast<float>(i * 2)));
    }
    for (auto& e : entities) {
        ptrs.push_back(&e);
    }

    auto bytes = EntitySerializer::serialize(ptrs);
    auto result = EntitySerializer::deserialize(bytes);

    ASSERT_EQ(result.size(), 50u);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(result[i]->type(), EntityType::Chicken);
        EXPECT_NEAR(result[i]->position().x, static_cast<float>(i), 0.01f);
        EXPECT_NEAR(result[i]->position().z, static_cast<float>(i * 2), 0.01f);
    }
}

TEST_F(EntitySerializerTest, EntityTypesPreserved) {
    // Test various EntityType enum values
    struct TypeTest {
        EntityType type;
        std::string desc;
    };
    std::vector<TypeTest> types = {
        {EntityType::Player, "Player"},
        {EntityType::Pig, "Pig"},
        {EntityType::Zombie, "Zombie"},
        {EntityType::ItemDrop, "ItemDrop"},
        {EntityType::Arrow, "Arrow"},
        {EntityType::Minecart, "Minecart"},
    };

    for (const auto& t : types) {
        Entity e(EntityId{1}, t.type);
        auto dc = EntitySerializer::entityToData(e);
        auto restored = EntitySerializer::entityFromData(*dc);
        EXPECT_EQ(restored->type(), t.type) << "Failed for type: " << t.desc;
    }
}

// ============================================================================
// EntityManager Column Integration Tests
// ============================================================================

class EntityColumnTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeDef def;
        def.name = "column_test_zombie";
        def.maxHealth = 20.0f;
        def.aiType = AIType::Hostile;
        EntityTypeRegistry::global().registerType("column_test_zombie", std::move(def));
    }

    World world;
    GraphicsEventQueue queue;
};

TEST_F(EntityColumnTest, GetEntitiesInColumn) {
    EntityManager em(world, queue);

    // Column (0,0) covers blocks [0..15, *, 0..15]
    em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));    // In column (0,0)
    em.spawnEntity(EntityType::Arrow, Vec3(20.0f, 64.0f, 5.0f));   // In column (1,0)
    em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 20.0f));   // In column (0,1)

    auto inCol00 = em.getEntitiesInColumn(ColumnPos(0, 0));
    EXPECT_EQ(inCol00.size(), 1u);

    auto inCol10 = em.getEntitiesInColumn(ColumnPos(1, 0));
    EXPECT_EQ(inCol10.size(), 1u);

    auto inCol01 = em.getEntitiesInColumn(ColumnPos(0, 1));
    EXPECT_EQ(inCol01.size(), 1u);

    auto inColEmpty = em.getEntitiesInColumn(ColumnPos(5, 5));
    EXPECT_EQ(inColEmpty.size(), 0u);
}

TEST_F(EntityColumnTest, SaveAndLoadColumnEntities) {
    // Create a column at position (0,0)
    ChunkColumn column(ColumnPos(0, 0));

    {
        EntityManager em(world, queue);

        // Spawn entities in column (0,0) — blocks [0..15, *, 0..15]
        em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));
        em.spawnEntity(EntityType::ItemDrop, Vec3(10.0f, 65.0f, 10.0f));

        EXPECT_EQ(em.entityCount(), 2u);

        // Save to column
        em.saveColumnEntities(column);
    }

    // Column should have entity data
    ASSERT_TRUE(column.hasData());
    auto bytes = column.data()->get<std::vector<uint8_t>>("entity_data");
    EXPECT_FALSE(bytes.empty());

    // Load into a fresh EntityManager
    {
        EntityManager em2(world, queue);
        EXPECT_EQ(em2.entityCount(), 0u);

        size_t loaded = em2.loadColumnEntities(column);
        EXPECT_EQ(loaded, 2u);
        EXPECT_EQ(em2.entityCount(), 2u);
    }
}

TEST_F(EntityColumnTest, SaveSkipsPlayers) {
    ChunkColumn column(ColumnPos(0, 0));

    EntityManager em(world, queue);
    em.spawnPlayer(Vec3(5.0f, 64.0f, 5.0f));
    em.spawnEntity(EntityType::Arrow, Vec3(10.0f, 64.0f, 10.0f));

    EXPECT_EQ(em.entityCount(), 2u);

    em.saveColumnEntities(column);

    // Load into fresh manager — only the arrow should load, not the player
    EntityManager em2(world, queue);
    size_t loaded = em2.loadColumnEntities(column);
    EXPECT_EQ(loaded, 1u);
}

TEST_F(EntityColumnTest, SaveOnlyEntitiesInColumn) {
    ChunkColumn column(ColumnPos(0, 0));

    EntityManager em(world, queue);
    // In column (0,0)
    em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));
    // In column (1,0) — should NOT be saved
    em.spawnEntity(EntityType::Arrow, Vec3(20.0f, 64.0f, 5.0f));

    em.saveColumnEntities(column);

    EntityManager em2(world, queue);
    size_t loaded = em2.loadColumnEntities(column);
    EXPECT_EQ(loaded, 1u);
}

TEST_F(EntityColumnTest, SaveEmptyColumnClearsData) {
    ChunkColumn column(ColumnPos(0, 0));

    // First save with entities
    {
        EntityManager em(world, queue);
        em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));
        em.saveColumnEntities(column);
        ASSERT_TRUE(column.hasData());
    }

    // Save again with no entities
    {
        EntityManager em(world, queue);
        em.saveColumnEntities(column);
    }

    // Data key should be removed
    if (column.hasData()) {
        auto bytes = column.data()->get<std::vector<uint8_t>>("entity_data");
        EXPECT_TRUE(bytes.empty());
    }
}

TEST_F(EntityColumnTest, LoadClearsEntityDataFromColumn) {
    ChunkColumn column(ColumnPos(0, 0));

    EntityManager em(world, queue);
    em.spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));
    em.saveColumnEntities(column);

    // Loading should remove entity_data to prevent double-loading
    EntityManager em2(world, queue);
    em2.loadColumnEntities(column);

    if (column.hasData()) {
        auto bytes = column.data()->get<std::vector<uint8_t>>("entity_data");
        EXPECT_TRUE(bytes.empty());
    }
}

TEST_F(EntityColumnTest, SaveAndLoadMobEntities) {
    ChunkColumn column(ColumnPos(0, 0));

    {
        EntityManager em(world, queue);

        auto typeId = EntityTypeId::fromName("column_test_zombie");
        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
        mob->setPosition(Vec3(5.0f, 64.0f, 5.0f));
        mob->setMaxHealth(20.0f);
        mob->setHealth(10.0f);
        mob->setSpeedMultiplier(0.8f);
        em.spawnEntity(std::move(mob));

        em.saveColumnEntities(column);
    }

    EntityManager em2(world, queue);
    size_t loaded = em2.loadColumnEntities(column);
    ASSERT_EQ(loaded, 1u);

    // Find the loaded entity
    const Entity* e = nullptr;
    for (const auto& [id, ent] : em2.entities()) {
        e = ent.get();
        break;
    }
    ASSERT_NE(e, nullptr);

    const auto* mob = dynamic_cast<const MobEntity*>(e);
    ASSERT_NE(mob, nullptr);
    EXPECT_NEAR(mob->health(), 10.0f, 0.01f);
    EXPECT_NEAR(mob->maxHealth(), 20.0f, 0.01f);
    EXPECT_NEAR(mob->speedMultiplier(), 0.8f, 0.01f);
}

TEST_F(EntityColumnTest, LoadFromEmptyColumn) {
    ChunkColumn column(ColumnPos(0, 0));

    EntityManager em(world, queue);
    size_t loaded = em.loadColumnEntities(column);
    EXPECT_EQ(loaded, 0u);
    EXPECT_EQ(em.entityCount(), 0u);
}
