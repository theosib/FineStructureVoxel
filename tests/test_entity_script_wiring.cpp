#include <gtest/gtest.h>

#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/script_mob_event_hooks.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/mob_event_hooks.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/ai_goals.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/graphics_event_queue.hpp"

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Test fixture
// ============================================================================

class EntityScriptWiringTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        EntityTypeDef def;
        def.name = "test_wiring_mob";
        def.aiType = AIType::Hostile;
        def.maxHealth = 20.0f;
        def.attackDamage = 3.0f;
        def.attackRange = 1.5f;
        EntityTypeRegistry::global().registerType("test_wiring_mob", std::move(def));

        EntityTypeDef passiveDef;
        passiveDef.name = "test_wiring_passive";
        passiveDef.aiType = AIType::Passive;
        passiveDef.maxHealth = 10.0f;
        EntityTypeRegistry::global().registerType("test_wiring_passive", std::move(passiveDef));

        EntityTypeDef noneDef;
        noneDef.name = "test_wiring_none";
        noneDef.aiType = AIType::None;
        noneDef.maxHealth = 10.0f;
        EntityTypeRegistry::global().registerType("test_wiring_none", std::move(noneDef));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

// ============================================================================
// EntityManager auto-configures AI presets on spawn
// ============================================================================

TEST_F(EntityScriptWiringTest, SpawnedHostileMobGetsAIGoals) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    auto typeId = EntityTypeId::fromName("test_wiring_mob");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
    auto* mobPtr = mob.get();

    em.spawnEntity(std::move(mob));

    // Hostile AI preset should add goals (Chase, Attack, Idle, Wander, LookAtPlayer)
    EXPECT_GT(mobPtr->brain().goalCount(), 0u);
}

TEST_F(EntityScriptWiringTest, SpawnedPassiveMobGetsAIGoals) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    auto typeId = EntityTypeId::fromName("test_wiring_passive");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
    auto* mobPtr = mob.get();

    em.spawnEntity(std::move(mob));
    EXPECT_GT(mobPtr->brain().goalCount(), 0u);
}

TEST_F(EntityScriptWiringTest, SpawnedNoneAIMobGetsNoGoals) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    auto typeId = EntityTypeId::fromName("test_wiring_none");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
    auto* mobPtr = mob.get();

    em.spawnEntity(std::move(mob));
    EXPECT_EQ(mobPtr->brain().goalCount(), 0u);
}

TEST_F(EntityScriptWiringTest, PreConfiguredGoalsNotOverwritten) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    auto typeId = EntityTypeId::fromName("test_wiring_mob");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);
    auto* mobPtr = mob.get();

    // Pre-add a goal before spawning
    mobPtr->brain().addGoal(0, std::make_unique<IdleGoal>(0));
    size_t preCount = mobPtr->brain().goalCount();

    em.spawnEntity(std::move(mob));

    // Goals should not be reconfigured since brain was non-empty
    EXPECT_EQ(mobPtr->brain().goalCount(), preCount);
}

// ============================================================================
// MobEventHooksProvider wiring
// ============================================================================

struct TestHooksProvider {
    struct SimpleHooks : MobEventHooks {
        int spawnCount = 0;
        int tickCount = 0;
        void onSpawn(MobEntity&) override { ++spawnCount; }
        void onTick(MobEntity&, float) override { ++tickCount; }
        void onDamage(MobEntity&, float, EntityId) override {}
        void onDeath(MobEntity&, EntityId) override {}
        void onInteract(MobEntity&, EntityId) override {}
        void onStrike(MobEntity&, EntityId) override {}
    };

    SimpleHooks hooks;

    MobEventHooksProvider createProvider() {
        return [this](const std::string&) -> MobEventHooks* {
            return &hooks;
        };
    }
};

TEST_F(EntityScriptWiringTest, HooksProviderCallsOnSpawn) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    TestHooksProvider provider;
    em.setMobEventHooksProvider(provider.createProvider());

    auto typeId = EntityTypeId::fromName("test_wiring_none");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);

    em.spawnEntity(std::move(mob));

    EXPECT_EQ(provider.hooks.spawnCount, 1);
}

TEST_F(EntityScriptWiringTest, EntityManagerTickCallsOnTick) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    TestHooksProvider provider;
    em.setMobEventHooksProvider(provider.createProvider());

    auto typeId = EntityTypeId::fromName("test_wiring_none");
    auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, typeId);

    em.spawnEntity(std::move(mob));
    EXPECT_EQ(provider.hooks.tickCount, 0);

    em.tick(0.05f);
    EXPECT_EQ(provider.hooks.tickCount, 1);

    em.tick(0.05f);
    EXPECT_EQ(provider.hooks.tickCount, 2);
}

TEST_F(EntityScriptWiringTest, NonMobEntitiesIgnored) {
    World world;
    GraphicsEventQueue gfxQueue;
    EntityManager em(world, gfxQueue);

    TestHooksProvider provider;
    em.setMobEventHooksProvider(provider.createProvider());

    // Spawn a plain Entity (not MobEntity)
    auto entity = std::make_unique<Entity>(INVALID_ENTITY_ID, EntityType::Player);
    em.spawnEntity(std::move(entity));

    // Should not call hooks for non-mob entities
    EXPECT_EQ(provider.hooks.spawnCount, 0);
}
