#include <gtest/gtest.h>
#include "finevox/core/spawn_predicate.hpp"
#include "finevox/core/spawn_rule.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/data_container.hpp"

using namespace finevox;

class SpawnPredicateTest : public ::testing::Test {
protected:
    void SetUp() override {
        SpawnPredicateRegistry::global().clear();
    }
    void TearDown() override {
        SpawnPredicateRegistry::global().clear();
    }
};

TEST_F(SpawnPredicateTest, RegisterAndEvaluate) {
    SpawnPredicateRegistry::global().registerPredicate("always_true",
        [](const SpawnRule&, const SpawnContext&) { return true; });

    EXPECT_TRUE(SpawnPredicateRegistry::global().hasPredicate("always_true"));
    EXPECT_EQ(SpawnPredicateRegistry::global().size(), 1u);
}

TEST_F(SpawnPredicateTest, EvaluateAll_AllPass) {
    SpawnPredicateRegistry::global().registerPredicate("pass1",
        [](const SpawnRule&, const SpawnContext&) { return true; });
    SpawnPredicateRegistry::global().registerPredicate("pass2",
        [](const SpawnRule&, const SpawnContext&) { return true; });

    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    rule.customPredicates = {"pass1", "pass2"};

    World world;
    SpawnContext ctx{world, BlockCoord(0, 64, 0), 0.25f, 50.0f};
    EXPECT_TRUE(SpawnPredicateRegistry::global().evaluateAll(rule, ctx));
}

TEST_F(SpawnPredicateTest, EvaluateAll_OneFails) {
    SpawnPredicateRegistry::global().registerPredicate("pass",
        [](const SpawnRule&, const SpawnContext&) { return true; });
    SpawnPredicateRegistry::global().registerPredicate("fail",
        [](const SpawnRule&, const SpawnContext&) { return false; });

    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    rule.customPredicates = {"pass", "fail"};

    World world;
    SpawnContext ctx{world, BlockCoord(0, 64, 0), 0.25f, 50.0f};
    EXPECT_FALSE(SpawnPredicateRegistry::global().evaluateAll(rule, ctx));
}

TEST_F(SpawnPredicateTest, UnknownPredicateFailsClosed) {
    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    rule.customPredicates = {"nonexistent"};

    World world;
    SpawnContext ctx{world, BlockCoord(0, 64, 0), 0.0f, 32.0f};
    EXPECT_FALSE(SpawnPredicateRegistry::global().evaluateAll(rule, ctx));
}

TEST_F(SpawnPredicateTest, NoPredicatesAlwaysPasses) {
    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    // No custom predicates

    World world;
    SpawnContext ctx{world, BlockCoord(0, 64, 0), 0.0f, 32.0f};
    EXPECT_TRUE(SpawnPredicateRegistry::global().evaluateAll(rule, ctx));
}

TEST_F(SpawnPredicateTest, PredicateReadsContext) {
    SpawnPredicateRegistry::global().registerPredicate("night_only",
        [](const SpawnRule&, const SpawnContext& ctx) {
            return ctx.timeOfDay > 0.5f;
        });

    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    rule.customPredicates = {"night_only"};

    World world;

    // Daytime — should fail
    SpawnContext dayCtx{world, BlockCoord(0, 64, 0), 0.25f, 50.0f};
    EXPECT_FALSE(SpawnPredicateRegistry::global().evaluateAll(rule, dayCtx));

    // Nighttime — should pass
    SpawnContext nightCtx{world, BlockCoord(0, 64, 0), 0.75f, 50.0f};
    EXPECT_TRUE(SpawnPredicateRegistry::global().evaluateAll(rule, nightCtx));
}

TEST_F(SpawnPredicateTest, PredicateReadsConditions) {
    SpawnPredicateRegistry::global().registerPredicate("min_height",
        [](const SpawnRule& rule, const SpawnContext& ctx) {
            if (!rule.conditions) return true;
            int minY = rule.conditions->get<int64_t>("min_y", 0);
            return ctx.pos.y >= minY;
        });

    SpawnRule rule;
    rule.entityType = EntityTypeId(InternedId(1));
    rule.customPredicates = {"min_height"};
    rule.conditions = std::make_unique<DataContainer>();
    rule.conditions->set<int64_t>("min_y", 100);

    World world;

    // Below min height
    SpawnContext lowCtx{world, BlockCoord(0, 50, 0), 0.0f, 50.0f};
    EXPECT_FALSE(SpawnPredicateRegistry::global().evaluateAll(rule, lowCtx));

    // Above min height
    SpawnContext highCtx{world, BlockCoord(0, 120, 0), 0.0f, 50.0f};
    EXPECT_TRUE(SpawnPredicateRegistry::global().evaluateAll(rule, highCtx));
}

TEST_F(SpawnPredicateTest, Unregister) {
    SpawnPredicateRegistry::global().registerPredicate("temp",
        [](const SpawnRule&, const SpawnContext&) { return true; });
    EXPECT_TRUE(SpawnPredicateRegistry::global().hasPredicate("temp"));

    SpawnPredicateRegistry::global().unregisterPredicate("temp");
    EXPECT_FALSE(SpawnPredicateRegistry::global().hasPredicate("temp"));
}
