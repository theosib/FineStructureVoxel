#include <gtest/gtest.h>
#include "finevox/core/ai_goals.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"

using namespace finevox;

class AIGoalParamsTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        EntityTypeDef passiveDef;
        passiveDef.name = "test_params_pig";
        passiveDef.aiType = AIType::Passive;
        passiveDef.maxHealth = 10.0f;
        EntityTypeRegistry::global().registerType("test_params_pig", std::move(passiveDef));

        EntityTypeDef hostileDef;
        hostileDef.name = "test_params_zombie";
        hostileDef.aiType = AIType::Hostile;
        hostileDef.maxHealth = 20.0f;
        hostileDef.attackDamage = 3.0f;
        hostileDef.attackRange = 1.5f;
        EntityTypeRegistry::global().registerType("test_params_zombie", std::move(hostileDef));

        EntityTypeDef noneDef;
        noneDef.name = "test_params_none";
        noneDef.aiType = AIType::None;
        noneDef.maxHealth = 10.0f;
        EntityTypeRegistry::global().registerType("test_params_none", std::move(noneDef));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

// ============================================================================
// Default params produce original behavior
// ============================================================================

TEST_F(AIGoalParamsTest, IdleGoalDefaultParams) {
    auto typeId = EntityTypeId::fromName("test_params_pig");
    MobEntity mob(1, typeId);

    IdleGoal goal(0);  // default params
    EXPECT_TRUE(goal.canStart(mob));  // Always returns true
    EXPECT_EQ(goal.priority(), 0);
}

TEST_F(AIGoalParamsTest, WanderGoalDefaultParams) {
    WanderGoalParams params;
    EXPECT_FLOAT_EQ(params.range, 10.0f);
    EXPECT_FLOAT_EQ(params.maxTime, 10.0f);
    EXPECT_FLOAT_EQ(params.startChance, 0.1f);
    EXPECT_EQ(params.animSlot, 1);
}

TEST_F(AIGoalParamsTest, ChaseGoalDefaultParams) {
    ChaseGoalParams params;
    EXPECT_FLOAT_EQ(params.maxRange, 16.0f);
    EXPECT_FLOAT_EQ(params.repathInterval, 1.0f);
    EXPECT_FLOAT_EQ(params.damageMemory, 10.0f);
    EXPECT_EQ(params.animSlot, 1);
}

TEST_F(AIGoalParamsTest, FleeGoalDefaultParams) {
    FleeGoalParams params;
    EXPECT_FLOAT_EQ(params.distance, 16.0f);
    EXPECT_FLOAT_EQ(params.duration, 5.0f);
    EXPECT_FLOAT_EQ(params.speedMult, 1.5f);
    EXPECT_FLOAT_EQ(params.damageMemory, 2.0f);
    EXPECT_EQ(params.animSlot, 1);
}

TEST_F(AIGoalParamsTest, PanicGoalDefaultParams) {
    PanicGoalParams params;
    EXPECT_FLOAT_EQ(params.duration, 5.0f);
    EXPECT_FLOAT_EQ(params.speedMult, 1.5f);
    EXPECT_FLOAT_EQ(params.wanderRange, 12.0f);
    EXPECT_FLOAT_EQ(params.damageMemory, 1.0f);
    EXPECT_EQ(params.animSlot, 1);
}

// ============================================================================
// Custom params are applied
// ============================================================================

TEST_F(AIGoalParamsTest, CustomIdleDuration) {
    auto typeId = EntityTypeId::fromName("test_params_pig");
    MobEntity mob(1, typeId);

    IdleGoalParams params;
    params.minDuration = 10.0f;
    params.maxDuration = 10.0f;  // Fixed duration
    params.animSlot = 5;

    IdleGoal goal(0, params);
    goal.start(mob);

    // Tick partway — not complete
    goal.tick(mob, 5.0f);
    EXPECT_FALSE(goal.isComplete(mob));

    // Tick past 10s — complete
    goal.tick(mob, 6.0f);
    EXPECT_TRUE(goal.isComplete(mob));
}

TEST_F(AIGoalParamsTest, CustomFleeSpeed) {
    auto typeId = EntityTypeId::fromName("test_params_pig");
    MobEntity mob(1, typeId);

    FleeGoalParams params;
    params.speedMult = 3.0f;
    params.duration = 2.0f;

    FleeGoal goal(7, params);

    // Simulate being recently damaged for flee to work
    mob.damage(1.0f, 99);

    EXPECT_TRUE(goal.canStart(mob));

    goal.start(mob);
    EXPECT_FLOAT_EQ(mob.speedMultiplier(), 3.0f);

    // Complete after 2s
    goal.tick(mob, 2.5f);
    EXPECT_TRUE(goal.isComplete(mob));

    goal.stop(mob);
    EXPECT_FLOAT_EQ(mob.speedMultiplier(), 1.0f);
}

TEST_F(AIGoalParamsTest, CustomWanderStartChance) {
    auto typeId = EntityTypeId::fromName("test_params_pig");
    MobEntity mob(1, typeId);

    // Zero chance — never starts
    WanderGoalParams params;
    params.startChance = 0.0f;

    WanderGoal goal(1, params);
    // Run 100 times, should never start
    for (int i = 0; i < 100; ++i) {
        EXPECT_FALSE(goal.canStart(mob));
    }
}

// ============================================================================
// configureAIPreset
// ============================================================================

TEST_F(AIGoalParamsTest, ConfigureAIPresetNone) {
    auto typeId = EntityTypeId::fromName("test_params_none");
    MobEntity mob(1, typeId);

    configureAIPreset(mob, AIType::None);
    EXPECT_EQ(mob.brain().goalCount(), 0u);
}

TEST_F(AIGoalParamsTest, ConfigureAIPresetPassive) {
    auto typeId = EntityTypeId::fromName("test_params_pig");
    MobEntity mob(1, typeId);

    configureAIPreset(mob, AIType::Passive);
    EXPECT_GT(mob.brain().goalCount(), 0u);
}

TEST_F(AIGoalParamsTest, ConfigureAIPresetHostile) {
    auto typeId = EntityTypeId::fromName("test_params_zombie");
    MobEntity mob(1, typeId);

    configureAIPreset(mob, AIType::Hostile);
    EXPECT_GT(mob.brain().goalCount(), 0u);
}

// ============================================================================
// Priority passthrough
// ============================================================================

TEST_F(AIGoalParamsTest, GoalPriorityPassthrough) {
    IdleGoal idle(42);
    EXPECT_EQ(idle.priority(), 42);

    WanderGoal wander(17);
    EXPECT_EQ(wander.priority(), 17);

    ChaseGoal chase(99);
    EXPECT_EQ(chase.priority(), 99);

    AttackGoal attack(3);
    EXPECT_EQ(attack.priority(), 3);

    FleeGoal flee(8);
    EXPECT_EQ(flee.priority(), 8);

    LookAtPlayerGoal look(2);
    EXPECT_EQ(look.priority(), 2);

    PanicGoal panic(10);
    EXPECT_EQ(panic.priority(), 10);
}
