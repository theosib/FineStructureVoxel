#include <gtest/gtest.h>
#include "finevox/core/ai_driver.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/string_interner.hpp"
#include <finescript/value.h>
#include <finescript/map_data.h>

using namespace finevox;

// ============================================================================
// Test helpers
// ============================================================================

class AIDriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        EntityTypeDef pigDef;
        pigDef.name = "test_driver_pig";
        pigDef.aiType = AIType::Passive;
        pigDef.maxHealth = 10.0f;
        EntityTypeRegistry::global().registerType("test_driver_pig", std::move(pigDef));

        EntityTypeDef zombieDef;
        zombieDef.name = "test_driver_zombie";
        zombieDef.aiType = AIType::Hostile;
        zombieDef.maxHealth = 20.0f;
        zombieDef.attackDamage = 3.0f;
        zombieDef.attackRange = 1.5f;
        EntityTypeRegistry::global().registerType("test_driver_zombie", std::move(zombieDef));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

// ============================================================================
// BrainAIDriver
// ============================================================================

TEST_F(AIDriverTest, BrainAIDriverSubscribesNoEvents) {
    BrainAIDriver driver;
    auto events = driver.subscribedEvents();
    EXPECT_TRUE(events.empty());
}

TEST_F(AIDriverTest, BrainAIDriverTickDoesNotCrash) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    BrainAIDriver driver;
    driver.tick(mob, 0.05f);  // Should not crash
}

// ============================================================================
// PlayerInputDriver
// ============================================================================

TEST_F(AIDriverTest, PlayerInputDriverSubscribesEvents) {
    PlayerInputDriver driver;
    auto events = driver.subscribedEvents();
    EXPECT_GT(events.size(), 0u);
    // Should include look, jump, sprint, sneak, etc.
    EXPECT_GE(events.size(), 5u);
}

TEST_F(AIDriverTest, PlayerInputDriverHandlesLookEvent) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    PlayerInputDriver driver;

    auto& si = StringInterner::global();

    // Build a look event: {:type :player_look, :yaw 1.5, :pitch 0.5}
    auto map = std::make_shared<finescript::MapData>();
    map->set(si.intern("type"), finescript::Value::symbol(si.intern("player_look")));
    map->set(si.intern("yaw"), finescript::Value::number(1.5));
    map->set(si.intern("pitch"), finescript::Value::number(0.5));

    finescript::Value event = finescript::Value::map(map);
    driver.onEvent(mob, event);

    EXPECT_FLOAT_EQ(mob.yaw(), 1.5f);
    EXPECT_FLOAT_EQ(mob.pitch(), 0.5f);
}

TEST_F(AIDriverTest, PlayerInputDriverHandlesJumpEvent) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    mob.setOnGround(true);

    PlayerInputDriver driver;
    auto& si = StringInterner::global();

    auto map = std::make_shared<finescript::MapData>();
    map->set(si.intern("type"), finescript::Value::symbol(si.intern("player_jump")));

    finescript::Value event = finescript::Value::map(map);
    driver.onEvent(mob, event);

    // Jump should have set a positive Y velocity
    EXPECT_GT(mob.velocity().y, 0.0f);
}

TEST_F(AIDriverTest, PlayerInputDriverHandlesSprintEvent) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    PlayerInputDriver driver;

    auto& si = StringInterner::global();

    // Start sprinting
    auto map = std::make_shared<finescript::MapData>();
    map->set(si.intern("type"), finescript::Value::symbol(si.intern("player_sprint")));
    map->set(si.intern("starting"), finescript::Value::boolean(true));

    finescript::Value event = finescript::Value::map(map);
    driver.onEvent(mob, event);

    EXPECT_FLOAT_EQ(mob.speedMultiplier(), 1.3f);

    // Stop sprinting
    auto map2 = std::make_shared<finescript::MapData>();
    map2->set(si.intern("type"), finescript::Value::symbol(si.intern("player_sprint")));
    map2->set(si.intern("starting"), finescript::Value::boolean(false));

    finescript::Value event2 = finescript::Value::map(map2);
    driver.onEvent(mob, event2);

    EXPECT_FLOAT_EQ(mob.speedMultiplier(), 1.0f);
}

TEST_F(AIDriverTest, PlayerInputDriverHandlesSneakEvent) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    PlayerInputDriver driver;

    auto& si = StringInterner::global();

    // Start sneaking
    auto map = std::make_shared<finescript::MapData>();
    map->set(si.intern("type"), finescript::Value::symbol(si.intern("player_sneak")));
    map->set(si.intern("starting"), finescript::Value::boolean(true));

    finescript::Value event = finescript::Value::map(map);
    driver.onEvent(mob, event);

    EXPECT_FLOAT_EQ(mob.speedMultiplier(), 0.3f);
}

TEST_F(AIDriverTest, SubscribesToChecksCorrectly) {
    PlayerInputDriver driver;
    auto& si = StringInterner::global();
    auto lookId = si.intern("player_look");
    auto unknownId = si.intern("totally_unknown_event_xyz");

    EXPECT_TRUE(driver.subscribesTo(lookId));
    EXPECT_FALSE(driver.subscribesTo(unknownId));
}

// ============================================================================
// AIDriver on MobEntity
// ============================================================================

TEST_F(AIDriverTest, MobEntityDriverAccessor) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);

    // No driver by default
    EXPECT_EQ(mob.driver(), nullptr);

    // Set a driver
    mob.setDriver(std::make_unique<BrainAIDriver>());
    EXPECT_NE(mob.driver(), nullptr);
}

// ============================================================================
// Player entity via spawnPlayer
// ============================================================================

TEST_F(AIDriverTest, SpawnPlayerCreatesMobEntity) {
    auto world = std::make_unique<World>();
    GraphicsEventQueue queue;
    EntityManager em(*world, queue);

    EntityId playerId = em.spawnPlayer(Vec3(0.0f, 64.0f, 0.0f));
    EXPECT_NE(playerId, INVALID_ENTITY_ID);

    Entity* entity = em.getEntity(playerId);
    ASSERT_NE(entity, nullptr);

    // Player should be identifiable as a player
    EXPECT_TRUE(isPlayer(*entity));
    EXPECT_TRUE(entity->isPlayerEntity());

    // Player should be a MobEntity
    auto* mob = dynamic_cast<MobEntity*>(entity);
    ASSERT_NE(mob, nullptr);

    // Player should have a PlayerInputDriver
    EXPECT_NE(mob->driver(), nullptr);
}

TEST_F(AIDriverTest, SpawnPlayerHasCorrectDefaults) {
    auto world = std::make_unique<World>();
    GraphicsEventQueue queue;
    EntityManager em(*world, queue);

    EntityId playerId = em.spawnPlayer(Vec3(5.0f, 64.0f, 5.0f));
    auto* mob = dynamic_cast<MobEntity*>(em.getEntity(playerId));
    ASSERT_NE(mob, nullptr);

    EXPECT_FLOAT_EQ(mob->maxHealth(), 20.0f);
    EXPECT_FLOAT_EQ(mob->health(), 20.0f);
    EXPECT_FLOAT_EQ(mob->eyeHeight(), 1.65f);
    EXPECT_FLOAT_EQ(mob->maxStepHeight(), 0.6f);
}

TEST_F(AIDriverTest, PlayerEntityNotSerialized) {
    // Verify isPlayer works for serialization skip
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    EXPECT_FALSE(isPlayer(mob));

    mob.setIsPlayer(true);
    EXPECT_TRUE(isPlayer(mob));
}

// ============================================================================
// Landing detection
// ============================================================================

TEST_F(AIDriverTest, LandingDetectionCapturesVelocity) {
    auto typeId = EntityTypeId::fromName("test_driver_pig");
    MobEntity mob(1, typeId);
    auto world = std::make_unique<World>();

    // Simulate falling
    mob.setOnGround(false);
    mob.setVelocity(Vec3(0.0f, -10.0f, 0.0f));
    mob.tick(0.05f, *world);

    // preLandingVelocityY should have captured the falling velocity
    EXPECT_FLOAT_EQ(mob.preLandingVelocityY(), -10.0f);
}
