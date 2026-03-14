#include <gtest/gtest.h>
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_loader.hpp"
#include "finevox/core/data_container.hpp"

using namespace finevox;

TEST(EntityTypePropertiesTest, DefaultNoProperties) {
    EntityTypeDef def;
    EXPECT_EQ(def.properties, nullptr);
    EXPECT_EQ(def.getProperty<int64_t>("health_regen", 0), 0);
    EXPECT_EQ(def.getProperty<std::string>("custom_tag", "none"), "none");
}

TEST(EntityTypePropertiesTest, SetAndGetProperties) {
    EntityTypeDef def;
    def.properties = std::make_unique<DataContainer>();
    def.properties->set<int64_t>("fire_resistance", 5);
    def.properties->set<std::string>("faction", "undead");

    EXPECT_EQ(def.getProperty<int64_t>("fire_resistance"), 5);
    EXPECT_EQ(def.getProperty<std::string>("faction"), "undead");
    EXPECT_EQ(def.getProperty<int64_t>("missing_key", 42), 42);
}

TEST(EntityTypePropertiesTest, LoaderParsesUnknownKeys) {
    std::string entityDef = R"(
name: test_mob
ai: passive
max_health: 30.0
custom_ability: fireball
custom_level: 5
)";
    auto def = EntityTypeLoader::loadFromString(entityDef);
    ASSERT_TRUE(def.has_value());
    EXPECT_EQ(def->name, "test_mob");
    EXPECT_FLOAT_EQ(def->maxHealth, 30.0f);

    // Unknown keys should be in properties
    ASSERT_NE(def->properties, nullptr);
    EXPECT_EQ(def->getProperty<std::string>("custom_ability"), "fireball");
    EXPECT_EQ(def->getProperty<int64_t>("custom_level"), 5);
}

TEST(EntityTypePropertiesTest, LoaderNoUnknownKeys) {
    std::string entityDef = R"(
name: simple_mob
ai: hostile
max_health: 20.0
)";
    auto def = EntityTypeLoader::loadFromString(entityDef);
    ASSERT_TRUE(def.has_value());
    // No unknown keys → properties stays null
    EXPECT_EQ(def->properties, nullptr);
}
