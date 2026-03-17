#include <gtest/gtest.h>
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/mob_event_hooks.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/world.hpp"

using namespace finevox;

// ============================================================================
// Mock MobEventHooks that records calls
// ============================================================================

struct RecordingHooks : MobEventHooks {
    int spawnCount = 0;
    int tickCount = 0;
    int damageCount = 0;
    int deathCount = 0;
    int interactCount = 0;
    int strikeCount = 0;

    float lastDamageAmount = 0;
    EntityId lastDamageSource = INVALID_ENTITY_ID;
    EntityId lastDeathKiller = INVALID_ENTITY_ID;
    float lastTickDt = 0;

    void onSpawn(MobEntity& /*mob*/) override { ++spawnCount; }
    void onTick(MobEntity& /*mob*/, float dt) override { ++tickCount; lastTickDt = dt; }
    void onDamage(MobEntity& /*mob*/, float amount, EntityId source) override {
        ++damageCount;
        lastDamageAmount = amount;
        lastDamageSource = source;
    }
    void onDeath(MobEntity& /*mob*/, EntityId killer) override {
        ++deathCount;
        lastDeathKiller = killer;
    }
    void onInteract(MobEntity& /*mob*/, EntityId /*player*/) override { ++interactCount; }
    void onStrike(MobEntity& /*mob*/, EntityId /*attacker*/) override { ++strikeCount; }
};

class MobEventHooksTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        EntityTypeDef def;
        def.name = "test_hooks_mob";
        def.aiType = AIType::None;
        def.maxHealth = 20.0f;
        EntityTypeRegistry::global().registerType("test_hooks_mob", std::move(def));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_F(MobEventHooksTest, NullHooksDoesNotCrash) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);
    // No hooks set — these should not crash
    mob.damage(5.0f, 99);
    EXPECT_FLOAT_EQ(mob.health(), 15.0f);
}

TEST_F(MobEventHooksTest, DamageCallsOnDamage) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);
    RecordingHooks hooks;
    mob.setEventHooks(&hooks);

    mob.damage(3.0f, 42);
    EXPECT_EQ(hooks.damageCount, 1);
    EXPECT_FLOAT_EQ(hooks.lastDamageAmount, 3.0f);
    EXPECT_EQ(hooks.lastDamageSource, 42u);
    EXPECT_FLOAT_EQ(mob.health(), 17.0f);
}

TEST_F(MobEventHooksTest, DamageCallsOnDamageWithCorrectArgs) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);
    RecordingHooks hooks;
    mob.setEventHooks(&hooks);

    mob.damage(5.0f, 100);
    mob.damage(2.0f, 200);

    EXPECT_EQ(hooks.damageCount, 2);
    EXPECT_FLOAT_EQ(hooks.lastDamageAmount, 2.0f);
    EXPECT_EQ(hooks.lastDamageSource, 200u);
}

TEST_F(MobEventHooksTest, DeathCallsOnDeathExactlyOnce) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);
    RecordingHooks hooks;
    mob.setEventHooks(&hooks);

    // Deal lethal damage
    mob.damage(25.0f, 77);
    EXPECT_EQ(hooks.damageCount, 1);

    // Tick to trigger death processing
    World world;
    mob.tick(0.05f, world);
    EXPECT_EQ(hooks.deathCount, 1);
    EXPECT_EQ(hooks.lastDeathKiller, 77u);

    // Tick again — should NOT fire onDeath again
    mob.tick(0.05f, world);
    EXPECT_EQ(hooks.deathCount, 1);
}

TEST_F(MobEventHooksTest, DeathHookFiredFlagPreventsDoubleFire) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);
    RecordingHooks hooks;
    mob.setEventHooks(&hooks);

    mob.setHealth(0.0f);

    World world;
    mob.tick(0.05f, world);
    mob.tick(0.05f, world);
    mob.tick(0.05f, world);

    EXPECT_EQ(hooks.deathCount, 1);
}

TEST_F(MobEventHooksTest, EventHooksAccessor) {
    auto typeId = EntityTypeId::fromName("test_hooks_mob");
    MobEntity mob(1, typeId);

    EXPECT_EQ(mob.eventHooks(), nullptr);

    RecordingHooks hooks;
    mob.setEventHooks(&hooks);
    EXPECT_EQ(mob.eventHooks(), &hooks);
}

// ============================================================================
// Named Animation State Tests
// ============================================================================

class MobAnimationTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        EntityTypeDef def;
        def.name = "test_anim_mob";
        def.aiType = AIType::None;
        def.maxHealth = 20.0f;
        def.animationStates["idle"] = 0;
        def.animationStates["walk"] = 1;
        def.animationStates["attack"] = 2;
        def.animationStates["death"] = 3;
        EntityTypeRegistry::global().registerType("test_anim_mob", std::move(def));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

TEST_F(MobAnimationTest, ResolveKnownAnimation) {
    auto typeId = EntityTypeId::fromName("test_anim_mob");
    MobEntity mob(1, typeId);

    EXPECT_EQ(mob.resolveAnimation("idle"), 0);
    EXPECT_EQ(mob.resolveAnimation("walk"), 1);
    EXPECT_EQ(mob.resolveAnimation("attack"), 2);
    EXPECT_EQ(mob.resolveAnimation("death"), 3);
}

TEST_F(MobAnimationTest, ResolveUnknownReturnsDefault) {
    auto typeId = EntityTypeId::fromName("test_anim_mob");
    MobEntity mob(1, typeId);

    EXPECT_EQ(mob.resolveAnimation("swim", 99), 99);
    EXPECT_EQ(mob.resolveAnimation("nonexistent"), 0);
}

TEST_F(MobAnimationTest, PlayAnimationSetsSlot) {
    auto typeId = EntityTypeId::fromName("test_anim_mob");
    MobEntity mob(1, typeId);

    mob.playAnimation("attack");
    EXPECT_EQ(mob.animationId(), 2);

    mob.playAnimation("walk");
    EXPECT_EQ(mob.animationId(), 1);

    mob.playAnimation("idle");
    EXPECT_EQ(mob.animationId(), 0);
}

TEST_F(MobAnimationTest, PlayUnknownKeepsCurrent) {
    auto typeId = EntityTypeId::fromName("test_anim_mob");
    MobEntity mob(1, typeId);

    mob.playAnimation("walk");  // Set to 1
    EXPECT_EQ(mob.animationId(), 1);

    mob.playAnimation("nonexistent");  // Unknown — keeps current
    EXPECT_EQ(mob.animationId(), 1);
}

TEST_F(MobAnimationTest, ResolveWithNoTypeDef) {
    // Use an unregistered type
    auto typeId = EntityTypeId::fromName("test_unregistered_type");
    MobEntity mob(1, typeId);

    // No type def → always returns defaultSlot
    EXPECT_EQ(mob.resolveAnimation("idle"), 0);
    EXPECT_EQ(mob.resolveAnimation("walk", 5), 5);
}
