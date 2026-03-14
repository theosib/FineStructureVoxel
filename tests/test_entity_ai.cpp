#include <gtest/gtest.h>
#include "finevox/core/ai_goal.hpp"
#include "finevox/core/ai_brain.hpp"
#include "finevox/core/ai_goals.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/pathfinder.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"

using namespace finevox;

// ============================================================================
// Test helpers
// ============================================================================

class EntityAITest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();

        // Register a test mob type
        EntityTypeDef zombieDef;
        zombieDef.name = "test_ai_zombie";
        zombieDef.aiType = AIType::Hostile;
        zombieDef.maxHealth = 20.0f;
        zombieDef.maxSpeed = 4.0f;
        zombieDef.attackDamage = 3.0f;
        zombieDef.attackRange = 1.5f;
        zombieDef.followRange = 16.0f;
        EntityTypeRegistry::global().registerType("test_ai_zombie", std::move(zombieDef));

        EntityTypeDef pigDef;
        pigDef.name = "test_ai_pig";
        pigDef.aiType = AIType::Passive;
        pigDef.maxHealth = 10.0f;
        pigDef.maxSpeed = 2.5f;
        EntityTypeRegistry::global().registerType("test_ai_pig", std::move(pigDef));
    }

    void TearDown() override {
        EntityTypeRegistry::global().clear();
    }
};

// ============================================================================
// Simple test goal for brain testing
// ============================================================================

class TestGoal : public AIGoal {
public:
    TestGoal(int prio, bool canStartVal = true)
        : prio_(prio), canStartVal_(canStartVal) {}

    bool canStart(MobEntity&) override { return canStartVal_; }
    void start(MobEntity&) override { started_ = true; }
    void tick(MobEntity&, float dt) override { ticked_ = true; elapsed_ += dt; }
    bool isComplete(MobEntity&) override { return complete_; }
    void stop(MobEntity&) override { stopped_ = true; }
    int priority() const override { return prio_; }

    void setCanStart(bool v) { canStartVal_ = v; }
    void setComplete(bool v) { complete_ = v; }

    bool started_ = false;
    bool ticked_ = false;
    bool stopped_ = false;
    bool complete_ = false;
    float elapsed_ = 0.0f;

private:
    int prio_;
    bool canStartVal_;
};

// ============================================================================
// AIBrain Tests
// ============================================================================

TEST_F(EntityAITest, BrainNoGoals) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    // No goals added, tick should not crash
    mob->brain().tick(*mob, 0.05f);
    EXPECT_EQ(mob->brain().activeGoal(), nullptr);
}

TEST_F(EntityAITest, BrainSelectsHighestPriority) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));

    auto low = std::make_unique<TestGoal>(1);
    auto high = std::make_unique<TestGoal>(5);

    auto* lowPtr = low.get();
    auto* highPtr = high.get();

    mob->brain().addGoal(1, std::move(low));
    mob->brain().addGoal(5, std::move(high));

    mob->brain().tick(*mob, 0.05f);

    EXPECT_EQ(mob->brain().activeGoal(), highPtr);
    EXPECT_TRUE(highPtr->started_);
    EXPECT_TRUE(highPtr->ticked_);
    EXPECT_FALSE(lowPtr->started_);
}

TEST_F(EntityAITest, BrainFallsBackToLowerPriority) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));

    auto low = std::make_unique<TestGoal>(1, true);
    auto high = std::make_unique<TestGoal>(5, false);  // Can't start

    auto* lowPtr = low.get();
    auto* highPtr = high.get();

    mob->brain().addGoal(1, std::move(low));
    mob->brain().addGoal(5, std::move(high));

    mob->brain().tick(*mob, 0.05f);

    EXPECT_EQ(mob->brain().activeGoal(), lowPtr);
    EXPECT_TRUE(lowPtr->started_);
    EXPECT_FALSE(highPtr->started_);
}

TEST_F(EntityAITest, BrainSwitchesOnCompletion) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));

    auto goal = std::make_unique<TestGoal>(1);
    auto* goalPtr = goal.get();

    mob->brain().addGoal(1, std::move(goal));

    mob->brain().tick(*mob, 0.05f);
    EXPECT_TRUE(goalPtr->started_);

    // Mark as complete
    goalPtr->setComplete(true);
    mob->brain().tick(*mob, 0.05f);

    EXPECT_TRUE(goalPtr->stopped_);
}

TEST_F(EntityAITest, BrainInterruptsForHigherPriority) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));

    auto low = std::make_unique<TestGoal>(1);
    auto high = std::make_unique<TestGoal>(5, false);  // Starts disabled

    auto* lowPtr = low.get();
    auto* highPtr = high.get();

    mob->brain().addGoal(1, std::move(low));
    mob->brain().addGoal(5, std::move(high));

    // Low priority starts
    mob->brain().tick(*mob, 0.05f);
    EXPECT_EQ(mob->brain().activeGoal(), lowPtr);

    // Enable high priority
    highPtr->setCanStart(true);
    mob->brain().tick(*mob, 0.05f);

    EXPECT_EQ(mob->brain().activeGoal(), highPtr);
    EXPECT_TRUE(lowPtr->stopped_);
    EXPECT_TRUE(highPtr->started_);
}

TEST_F(EntityAITest, BrainGoalCount) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_EQ(mob->brain().goalCount(), 0u);

    mob->brain().addGoal(1, std::make_unique<TestGoal>(1));
    EXPECT_EQ(mob->brain().goalCount(), 1u);

    mob->brain().addGoal(2, std::make_unique<TestGoal>(2));
    EXPECT_EQ(mob->brain().goalCount(), 2u);

    mob->brain().clear();
    EXPECT_EQ(mob->brain().goalCount(), 0u);
}

// ============================================================================
// MobEntity Tests
// ============================================================================

TEST_F(EntityAITest, MobEntityCreation) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_EQ(mob->id(), 1u);
    EXPECT_EQ(mob->typeId(), EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_FALSE(mob->isDead());
    EXPECT_FLOAT_EQ(mob->health(), 20.0f);
    EXPECT_FLOAT_EQ(mob->maxHealth(), 20.0f);
}

TEST_F(EntityAITest, MobEntityTypeDef) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    const auto* def = mob->typeDef();
    ASSERT_NE(def, nullptr);
    EXPECT_EQ(def->aiType, AIType::Hostile);
    EXPECT_FLOAT_EQ(def->maxHealth, 20.0f);
}

TEST_F(EntityAITest, MobEntityDamage) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_FLOAT_EQ(mob->health(), 20.0f);

    mob->damage(5.0f, 42);
    EXPECT_FLOAT_EQ(mob->health(), 15.0f);
    EXPECT_EQ(mob->lastAttacker(), 42u);
    EXPECT_TRUE(mob->wasRecentlyDamaged());

    mob->damage(20.0f);
    EXPECT_FLOAT_EQ(mob->health(), 0.0f);
    EXPECT_TRUE(mob->isDead());
}

TEST_F(EntityAITest, MobEntityHeal) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    mob->damage(10.0f);
    EXPECT_FLOAT_EQ(mob->health(), 10.0f);

    mob->heal(5.0f);
    EXPECT_FLOAT_EQ(mob->health(), 15.0f);

    // Can't heal above max
    mob->heal(100.0f);
    EXPECT_FLOAT_EQ(mob->health(), 20.0f);
}

TEST_F(EntityAITest, MobEntityMoveTo) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_FALSE(mob->hasMoveTarget());

    mob->moveTo(glm::dvec3(10.0, 0.0, 10.0));
    EXPECT_TRUE(mob->hasMoveTarget());
    EXPECT_DOUBLE_EQ(mob->moveTarget().x, 10.0);
    EXPECT_DOUBLE_EQ(mob->moveTarget().z, 10.0);

    mob->clearMoveTarget();
    EXPECT_FALSE(mob->hasMoveTarget());
}

TEST_F(EntityAITest, MobEntityLookAt) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    mob->setPosition(Vec3(0, 0, 0));

    // Look north (positive Z)
    mob->lookAt(glm::dvec3(0.0, 0.0, 10.0));
    EXPECT_NEAR(mob->yaw(), 0.0f, 0.01f);

    // Look east (positive X)
    mob->lookAt(glm::dvec3(10.0, 0.0, 0.0));
    EXPECT_NEAR(mob->yaw(), static_cast<float>(M_PI / 2.0), 0.01f);
}

TEST_F(EntityAITest, MobEntityTypeName) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_EQ(mob->typeName(), "test_ai_zombie");
}

TEST_F(EntityAITest, MobEntitySpeedMultiplier) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_FLOAT_EQ(mob->speedMultiplier(), 1.0f);

    mob->setSpeedMultiplier(1.5f);
    EXPECT_FLOAT_EQ(mob->speedMultiplier(), 1.5f);
}

// ============================================================================
// AI Preset Tests
// ============================================================================

TEST_F(EntityAITest, PassivePreset) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_pig"));
    configureAIPreset(*mob, AIType::Passive);
    EXPECT_EQ(mob->brain().goalCount(), 4u);  // Idle, Wander, LookAtPlayer, Panic
}

TEST_F(EntityAITest, HostilePreset) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    configureAIPreset(*mob, AIType::Hostile);
    EXPECT_EQ(mob->brain().goalCount(), 5u);  // Idle, Wander, LookAtPlayer, Chase, Attack
}

TEST_F(EntityAITest, NeutralPreset) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    configureAIPreset(*mob, AIType::Neutral);
    EXPECT_EQ(mob->brain().goalCount(), 5u);  // Same goals as hostile
}

TEST_F(EntityAITest, NonePreset) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    configureAIPreset(*mob, AIType::None);
    EXPECT_EQ(mob->brain().goalCount(), 0u);
}

// ============================================================================
// Built-in Goal Tests (basic behavior)
// ============================================================================

TEST_F(EntityAITest, IdleGoalAlwaysCanStart) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    IdleGoal goal(0);
    EXPECT_TRUE(goal.canStart(*mob));
}

TEST_F(EntityAITest, IdleGoalCompletes) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    IdleGoal goal(0);
    goal.start(*mob);

    // Tick for more than max idle duration
    for (int i = 0; i < 120; ++i) {
        goal.tick(*mob, 0.05f);
    }
    EXPECT_TRUE(goal.isComplete(*mob));
}

TEST_F(EntityAITest, FleeGoalPassiveMob) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_pig"));
    FleeGoal goal(7);

    // Not recently damaged
    EXPECT_FALSE(goal.canStart(*mob));

    // Take damage
    mob->damage(1.0f, 99);
    EXPECT_TRUE(goal.canStart(*mob));
}

TEST_F(EntityAITest, PanicGoalRecentDamage) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_pig"));
    PanicGoal goal(8);

    EXPECT_FALSE(goal.canStart(*mob));

    mob->damage(1.0f);
    EXPECT_TRUE(goal.canStart(*mob));
}

// ============================================================================
// Pathfinder Tests (with a simple mock world)
// ============================================================================

TEST(PathfinderTest, IsWalkableRequiresSolidBelow) {
    // Create a tiny world
    auto world = std::make_unique<World>();
    auto stoneId = BlockTypeId::fromName("test_path_stone");
    BlockType stone;
    stone.setOpaque(true);
    BlockRegistry::global().registerType(stoneId, stone);

    // Place a floor
    world->setBlock({0, 0, 0}, stoneId);

    // Block above the floor should be walkable (solid below, air above)
    EXPECT_TRUE(Pathfinder::isWalkable(*world, {0, 1, 0}));

    // Block at the floor level shouldn't be walkable (solid at feet)
    EXPECT_FALSE(Pathfinder::isWalkable(*world, {0, 0, 0}));

    // Block with no floor below shouldn't be walkable
    EXPECT_FALSE(Pathfinder::isWalkable(*world, {5, 1, 5}));
}

TEST(PathfinderTest, FindPathSimple) {
    auto world = std::make_unique<World>();
    auto stoneId = BlockTypeId::fromName("test_path_stone2");
    BlockType stone;
    stone.setOpaque(true);
    BlockRegistry::global().registerType(stoneId, stone);

    // Create a flat floor
    for (int x = -5; x <= 5; ++x) {
        for (int z = -5; z <= 5; ++z) {
            world->setBlock({x, 0, z}, stoneId);
        }
    }

    // Find path on flat ground
    auto path = Pathfinder::findPath(
        *world,
        glm::dvec3(0.5, 1.0, 0.5),
        glm::dvec3(3.5, 1.0, 3.5),
        0.6f, 1.8f, 32, 200
    );

    EXPECT_TRUE(path.has_value());
    if (path) {
        EXPECT_FALSE(path->empty());
        // Last node should be at or near goal
        auto& last = path->back();
        EXPECT_EQ(last.pos.x, 3);
        EXPECT_EQ(last.pos.z, 3);
    }
}

TEST(PathfinderTest, NoPathIfBlocked) {
    auto world = std::make_unique<World>();
    auto stoneId = BlockTypeId::fromName("test_path_stone3");
    BlockType stone;
    stone.setOpaque(true);
    BlockRegistry::global().registerType(stoneId, stone);

    // Create a floor with a wall
    for (int x = -5; x <= 5; ++x) {
        for (int z = -5; z <= 5; ++z) {
            world->setBlock({x, 0, z}, stoneId);
        }
    }
    // Wall across z=2
    for (int x = -5; x <= 5; ++x) {
        world->setBlock({x, 1, 2}, stoneId);
        world->setBlock({x, 2, 2}, stoneId);
    }

    // Try to find path through wall
    auto path = Pathfinder::findPath(
        *world,
        glm::dvec3(0.5, 1.0, 0.5),
        glm::dvec3(0.5, 1.0, 4.5),
        0.6f, 1.8f, 32, 200
    );

    EXPECT_FALSE(path.has_value());
}

TEST(PathfinderTest, PathWithStepUp) {
    auto world = std::make_unique<World>();
    auto stoneId = BlockTypeId::fromName("test_path_stone4");
    BlockType stone;
    stone.setOpaque(true);
    BlockRegistry::global().registerType(stoneId, stone);

    // Flat floor then a step up
    for (int x = -1; x <= 5; ++x) {
        for (int z = -1; z <= 5; ++z) {
            world->setBlock({x, 0, z}, stoneId);
        }
    }
    // Step up at x=2
    for (int z = -1; z <= 5; ++z) {
        world->setBlock({2, 1, z}, stoneId);
        // Higher floor continues
        for (int x = 2; x <= 5; ++x) {
            world->setBlock({x, 1, z}, stoneId);
        }
    }

    auto path = Pathfinder::findPath(
        *world,
        glm::dvec3(0.5, 1.0, 0.5),
        glm::dvec3(4.5, 2.0, 0.5),
        0.6f, 1.8f, 32, 200
    );

    EXPECT_TRUE(path.has_value());
}

// ============================================================================
// EntitySenses Tests
// ============================================================================

TEST_F(EntityAITest, SensesDetectsNothing) {
    auto mob = std::make_unique<MobEntity>(1, EntityTypeId::fromName("test_ai_zombie"));
    EXPECT_EQ(mob->senses().visibleCount(), 0u);
    EXPECT_EQ(mob->senses().nearestPlayer(), nullptr);
}
