#include <gtest/gtest.h>
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_type_loader.hpp"

using namespace finevox;

// ============================================================================
// EntityTypeId Tests
// ============================================================================

TEST(EntityTypeIdTest, DefaultIsEmpty) {
    EntityTypeId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
}

TEST(EntityTypeIdTest, FromName) {
    auto id = EntityTypeId::fromName("test_entity_zombie");
    EXPECT_TRUE(id.isValid());
    EXPECT_FALSE(id.isEmpty());
    EXPECT_EQ(id.name(), "test_entity_zombie");
}

TEST(EntityTypeIdTest, SameNameSameId) {
    auto a = EntityTypeId::fromName("test_entity_same_name");
    auto b = EntityTypeId::fromName("test_entity_same_name");
    EXPECT_EQ(a, b);
}

TEST(EntityTypeIdTest, DifferentNameDifferentId) {
    auto a = EntityTypeId::fromName("test_entity_name_a");
    auto b = EntityTypeId::fromName("test_entity_name_b");
    EXPECT_NE(a, b);
}

TEST(EntityTypeIdTest, HashWorks) {
    auto id = EntityTypeId::fromName("test_entity_hashable");
    std::unordered_map<EntityTypeId, int> map;
    map[id] = 42;
    EXPECT_EQ(map[id], 42);
}

TEST(EntityTypeIdTest, ConstantIsEmpty) {
    EXPECT_TRUE(EMPTY_ENTITY_TYPE_ID.isEmpty());
    EXPECT_FALSE(EMPTY_ENTITY_TYPE_ID.isValid());
}

// ============================================================================
// AIType Tests
// ============================================================================

TEST(AITypeTest, ParsePassive) {
    EXPECT_EQ(parseAIType("passive"), AIType::Passive);
}

TEST(AITypeTest, ParseHostile) {
    EXPECT_EQ(parseAIType("hostile"), AIType::Hostile);
}

TEST(AITypeTest, ParseNeutral) {
    EXPECT_EQ(parseAIType("neutral"), AIType::Neutral);
}

TEST(AITypeTest, ParseNone) {
    EXPECT_EQ(parseAIType("none"), AIType::None);
}

TEST(AITypeTest, ParseUnknownDefaultsToPassive) {
    EXPECT_EQ(parseAIType("garbage"), AIType::Passive);
}

TEST(AITypeTest, RoundTrip) {
    EXPECT_EQ(parseAIType(aiTypeName(AIType::Passive)), AIType::Passive);
    EXPECT_EQ(parseAIType(aiTypeName(AIType::Hostile)), AIType::Hostile);
    EXPECT_EQ(parseAIType(aiTypeName(AIType::Neutral)), AIType::Neutral);
    EXPECT_EQ(parseAIType(aiTypeName(AIType::None)), AIType::None);
}

// ============================================================================
// EntityTypeDef Tests
// ============================================================================

TEST(EntityTypeDefTest, DefaultValues) {
    EntityTypeDef def;
    EXPECT_TRUE(def.name.empty());
    EXPECT_EQ(def.aiType, AIType::Passive);
    EXPECT_FLOAT_EQ(def.maxHealth, 20.0f);
    EXPECT_FLOAT_EQ(def.maxSpeed, 4.0f);
    EXPECT_FLOAT_EQ(def.attackDamage, 0.0f);
    EXPECT_TRUE(def.hasGravity);
    EXPECT_FALSE(def.swims);
    EXPECT_TRUE(def.lootTable.isEmpty());
    EXPECT_TRUE(def.script.empty());
}

// ============================================================================
// EntityTypeRegistry Tests
// ============================================================================

class EntityTypeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();
    }
    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

TEST_F(EntityTypeRegistryTest, RegisterAndRetrieve) {
    EntityTypeDef def;
    def.name = "test_zombie";
    def.maxHealth = 20.0f;
    def.aiType = AIType::Hostile;

    EXPECT_TRUE(EntityTypeRegistry::global().registerType("test_zombie", std::move(def)));
    EXPECT_EQ(EntityTypeRegistry::global().size(), 1u);

    auto id = EntityTypeId::fromName("test_zombie");
    const auto* found = EntityTypeRegistry::global().getType(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "test_zombie");
    EXPECT_FLOAT_EQ(found->maxHealth, 20.0f);
    EXPECT_EQ(found->aiType, AIType::Hostile);
}

TEST_F(EntityTypeRegistryTest, RetrieveByName) {
    EntityTypeDef def;
    def.name = "test_pig";

    EntityTypeRegistry::global().registerType("test_pig", std::move(def));

    const auto* found = EntityTypeRegistry::global().getType("test_pig");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "test_pig");
}

TEST_F(EntityTypeRegistryTest, NoDuplicates) {
    EntityTypeDef def;
    def.name = "test_dup";

    EntityTypeDef def2;
    def2.name = "test_dup";
    EXPECT_TRUE(EntityTypeRegistry::global().registerType("test_dup", std::move(def)));
    EXPECT_FALSE(EntityTypeRegistry::global().registerType("test_dup", std::move(def2)));
    EXPECT_EQ(EntityTypeRegistry::global().size(), 1u);
}

TEST_F(EntityTypeRegistryTest, HasType) {
    EntityTypeDef def;
    def.name = "test_has";
    EntityTypeRegistry::global().registerType("test_has", std::move(def));

    auto id = EntityTypeId::fromName("test_has");
    EXPECT_TRUE(EntityTypeRegistry::global().hasType(id));
    EXPECT_TRUE(EntityTypeRegistry::global().hasType("test_has"));
    EXPECT_FALSE(EntityTypeRegistry::global().hasType("nonexistent"));
}

TEST_F(EntityTypeRegistryTest, GetNonexistentReturnsNull) {
    auto id = EntityTypeId::fromName("test_nope");
    EXPECT_EQ(EntityTypeRegistry::global().getType(id), nullptr);
    EXPECT_EQ(EntityTypeRegistry::global().getType("test_nope"), nullptr);
}

TEST_F(EntityTypeRegistryTest, ForEachType) {
    EntityTypeDef zombie;
    zombie.name = "test_zombie_iter";
    EntityTypeDef pig;
    pig.name = "test_pig_iter";

    EntityTypeRegistry::global().registerType("test_zombie_iter", std::move(zombie));
    EntityTypeRegistry::global().registerType("test_pig_iter", std::move(pig));

    int count = 0;
    EntityTypeRegistry::global().forEachType([&](EntityTypeId, const EntityTypeDef&) {
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST_F(EntityTypeRegistryTest, ClearWorks) {
    EntityTypeDef def;
    def.name = "test_clear";
    EntityTypeRegistry::global().registerType("test_clear", std::move(def));
    EXPECT_EQ(EntityTypeRegistry::global().size(), 1u);

    EntityTypeRegistry::global().clear();
    EXPECT_EQ(EntityTypeRegistry::global().size(), 0u);
}

// ============================================================================
// EntityTypeLoader Tests
// ============================================================================

TEST(EntityTypeLoaderTest, LoadFromString) {
    const char* content = R"(
name: test_loader_zombie
half_extents: 0.3 0.9 0.3
max_speed: 4.0
max_health: 20
armor: 2
ai: hostile
follow_range: 35
attack_damage: 3
attack_range: 1.5
attack_cooldown: 1.0
model: zombie
default_animation: idle
spawn_weight: 1.5
spawn_group_min: 1
spawn_group_max: 4
loot: zombie
sounds: zombie
script: entities/zombie.fs
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test_loader_zombie");
    EXPECT_FLOAT_EQ(def->halfExtents.x, 0.3f);
    EXPECT_FLOAT_EQ(def->halfExtents.y, 0.9f);
    EXPECT_FLOAT_EQ(def->halfExtents.z, 0.3f);
    EXPECT_FLOAT_EQ(def->maxSpeed, 4.0f);
    EXPECT_FLOAT_EQ(def->maxHealth, 20.0f);
    EXPECT_FLOAT_EQ(def->armor, 2.0f);
    EXPECT_EQ(def->aiType, AIType::Hostile);
    EXPECT_FLOAT_EQ(def->followRange, 35.0f);
    EXPECT_FLOAT_EQ(def->attackDamage, 3.0f);
    EXPECT_FLOAT_EQ(def->attackRange, 1.5f);
    EXPECT_FLOAT_EQ(def->attackCooldown, 1.0f);
    EXPECT_EQ(StringInterner::global().lookup(def->model), "zombie");
    EXPECT_EQ(StringInterner::global().lookup(def->defaultAnimation), "idle");
    EXPECT_FLOAT_EQ(def->spawnWeight, 1.5f);
    EXPECT_EQ(def->spawnGroupMin, 1);
    EXPECT_EQ(def->spawnGroupMax, 4);
    EXPECT_EQ(def->lootTable.name(), "zombie");
    EXPECT_EQ(def->soundSet.name(), "zombie");
    EXPECT_EQ(def->script, "entities/zombie.fs");
}

TEST(EntityTypeLoaderTest, MinimalEntity) {
    const char* content = R"(
name: test_loader_minimal
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test_loader_minimal");
    // Everything else is defaults
    EXPECT_EQ(def->aiType, AIType::Passive);
    EXPECT_FLOAT_EQ(def->maxHealth, 20.0f);
    EXPECT_TRUE(def->script.empty());
}

TEST(EntityTypeLoaderTest, EmptyContentFails) {
    auto def = EntityTypeLoader::loadFromString("");
    EXPECT_FALSE(def.has_value());
}

TEST(EntityTypeLoaderTest, NoNameFails) {
    const char* content = R"(
max_health: 10
ai: hostile
)";
    auto def = EntityTypeLoader::loadFromString(content);
    EXPECT_FALSE(def.has_value());
}

TEST(EntityTypeLoaderTest, PassiveEntity) {
    const char* content = R"(
name: test_loader_pig
half_extents: 0.45 0.45 0.45
max_speed: 2.5
max_health: 10
ai: passive
model: pig
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->aiType, AIType::Passive);
    EXPECT_FLOAT_EQ(def->maxSpeed, 2.5f);
    EXPECT_FLOAT_EQ(def->maxHealth, 10.0f);
}

TEST(EntityTypeLoaderTest, BooleanFields) {
    const char* content = R"(
name: test_loader_fish
gravity: false
swims: true
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());
    EXPECT_FALSE(def->hasGravity);
    EXPECT_TRUE(def->swims);
}

TEST(EntityTypeLoaderTest, NoneAIType) {
    const char* content = R"(
name: test_loader_scripted
ai: none
script: entities/custom.fs
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->aiType, AIType::None);
    EXPECT_EQ(def->script, "entities/custom.fs");
}

// ============================================================================
// Integration: Load + Register
// ============================================================================

class EntityTypeLoaderRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();
    }
    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

TEST_F(EntityTypeLoaderRegistryTest, LoadAndRegister) {
    const char* content = R"(
name: test_integ_zombie
max_health: 20
ai: hostile
attack_damage: 3
)";

    auto def = EntityTypeLoader::loadFromString(content);
    ASSERT_TRUE(def.has_value());

    auto name = def->name;  // Copy before move
    EXPECT_TRUE(EntityTypeRegistry::global().registerType(name, std::move(*def)));

    auto id = EntityTypeId::fromName("test_integ_zombie");
    const auto* found = EntityTypeRegistry::global().getType(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->aiType, AIType::Hostile);
    EXPECT_FLOAT_EQ(found->attackDamage, 3.0f);
}
