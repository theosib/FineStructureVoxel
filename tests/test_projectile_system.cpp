#include <gtest/gtest.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/projectile_system.hpp"
#include "finevox/core/world.hpp"

#include <cmath>

using namespace finevox;

namespace {

constexpr int GROUND_Y = 10;
constexpr float DT = 1.0f / 30.0f;
constexpr float GRAVITY = 14.0f;

/// Flies a shot with plain Euler steps, independent of ProjectileSystem, and
/// reports how close it passes to the target.
double missDistance(glm::dvec3 from, glm::dvec3 velocity, glm::dvec3 to) {
    double best = 1e9;
    glm::dvec3 p = from;
    double dt = 1.0 / 600.0;
    for (int i = 0; i < 60000; ++i) {
        velocity.y -= GRAVITY * dt;
        p += velocity * dt;
        best = std::min(best, glm::length(p - to));
    }
    return best;
}

class ProjectileTest : public ::testing::Test {
protected:
    void SetUp() override {
        stone = BlockTypeId::fromName("proj_stone");
        BlockType type;
        type.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, type);

        EntityTypeRegistry::global().clear();
        EntityTypeDef def;
        def.name = "proj_target";
        def.aiType = AIType::None;
        def.maxHealth = 20.0f;
        def.hasGravity = false;
        EntityTypeRegistry::global().registerType("proj_target", std::move(def));

        for (int x = -4; x < 96; ++x) {
            for (int z = -8; z < 8; ++z) world.setBlock(x, GROUND_Y - 1, z, stone);
        }
        entities = std::make_unique<EntityManager>(world, graphics);
        system = std::make_unique<ProjectileSystem>(world, *entities);
    }

    EntityId spawnTarget(float x, float z, uint16_t faction) {
        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName("proj_target"));
        mob->setPosition(Vec3(x, GROUND_Y, z));
        mob->setFaction(faction);
        return entities->spawnEntity(std::move(mob));
    }

    ProjectileSpec shot(glm::dvec3 from, glm::dvec3 to, uint16_t faction = 1) {
        auto velocity = solveBallistic(from, to, 24.0f, GRAVITY);
        EXPECT_TRUE(velocity.has_value());
        ProjectileSpec spec;
        spec.origin = from;
        spec.velocity = velocity.value_or(glm::dvec3(0.0));
        spec.gravity = GRAVITY;
        spec.damage = 5.0f;
        spec.faction = faction;
        return spec;
    }

    void run(float seconds) {
        for (int i = 0; i < int(seconds / DT); ++i) system->tick(DT);
    }

    World world;
    GraphicsEventQueue graphics;
    std::unique_ptr<EntityManager> entities;
    std::unique_ptr<ProjectileSystem> system;
    BlockTypeId stone;
};

}  // namespace

TEST(Ballistics, SolutionsActuallyReachTheTarget) {
    glm::dvec3 from(0.0, 12.0, 0.0);
    for (glm::dvec3 to : {glm::dvec3(20, 12, 0), glm::dvec3(10, 5, 15), glm::dvec3(-25, 18, 8),
                          glm::dvec3(30, 0, -12), glm::dvec3(1, 12, 1)}) {
        auto v = solveBallistic(from, to, 24.0f, GRAVITY);
        ASSERT_TRUE(v.has_value());
        EXPECT_NEAR(glm::length(*v), 24.0, 1e-6);
        EXPECT_LT(missDistance(from, *v, to), 0.1);

        auto lob = solveBallistic(from, to, 24.0f, GRAVITY, true);
        ASSERT_TRUE(lob.has_value());
        EXPECT_GT(lob->y, v->y) << "the lob goes higher";
        EXPECT_LT(missDistance(from, *lob, to), 0.1);
    }
}

TEST(Ballistics, TargetsBeyondRangeHaveNoSolution) {
    EXPECT_FALSE(solveBallistic(glm::dvec3(0.0), glm::dvec3(200, 0, 0), 24.0f, GRAVITY).has_value());
}

TEST_F(ProjectileTest, StrikesAnEnemyAndDamagesIt) {
    EntityId target = spawnTarget(20.0f, 0.0f, 2);
    system->fire(shot(glm::dvec3(0.0, GROUND_Y + 1.2, 0.0), glm::dvec3(20.0, GROUND_Y + 1.0, 0.0)));
    run(2.0f);

    EXPECT_FLOAT_EQ(entities->getMob(target)->health(), 15.0f);
    auto hits = system->drainHits();
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].entity, target);
    EXPECT_EQ(system->activeCount(), 0u);
}

TEST_F(ProjectileTest, WallsStopShots) {
    EntityId target = spawnTarget(20.0f, 0.0f, 2);
    for (int y = 0; y < 5; ++y) {
        for (int z = -4; z < 4; ++z) world.setBlock(12, GROUND_Y + y, z, stone);
    }
    system->fire(shot(glm::dvec3(0.0, GROUND_Y + 1.2, 0.0), glm::dvec3(20.0, GROUND_Y + 1.0, 0.0)));
    run(2.0f);

    EXPECT_FLOAT_EQ(entities->getMob(target)->health(), 20.0f);
    auto hits = system->drainHits();
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].entity, INVALID_ENTITY_ID);
    EXPECT_EQ(hits[0].block.x, 12);
}

TEST_F(ProjectileTest, DoesNotHitItsOwnSide) {
    EntityId ally = spawnTarget(10.0f, 0.0f, 1);
    EntityId enemy = spawnTarget(20.0f, 0.0f, 2);
    system->fire(shot(glm::dvec3(0.0, GROUND_Y + 1.2, 0.0), glm::dvec3(20.0, GROUND_Y + 1.0, 0.0)));
    run(2.0f);

    EXPECT_FLOAT_EQ(entities->getMob(ally)->health(), 20.0f);
    EXPECT_LT(entities->getMob(enemy)->health(), 20.0f);
}

TEST_F(ProjectileTest, FastShotsDoNotTunnelThroughThinWalls) {
    for (int z = -4; z < 4; ++z) world.setBlock(30, GROUND_Y + 1, z, stone);
    ProjectileSpec spec;
    spec.origin = glm::dvec3(0.0, GROUND_Y + 1.5, 0.0);
    spec.velocity = glm::dvec3(120.0, 0.0, 0.0);
    spec.gravity = 0.0f;
    system->fire(spec);
    run(1.0f);

    auto hits = system->drainHits();
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].block.x, 30);
}

TEST_F(ProjectileTest, ExpiresWhenItHitsNothing) {
    ProjectileSpec spec;
    spec.origin = glm::dvec3(0.0, GROUND_Y + 50.0, 0.0);
    spec.velocity = glm::dvec3(0.0, 30.0, 0.0);
    spec.gravity = 0.0f;
    spec.lifetime = 0.5f;
    system->fire(spec);
    run(1.0f);
    EXPECT_EQ(system->activeCount(), 0u);
    EXPECT_TRUE(system->drainHits().empty());
}

TEST_F(ProjectileTest, ViewReflectsFlight) {
    system->fire(shot(glm::dvec3(0.0, GROUND_Y + 1.2, 0.0), glm::dvec3(30.0, GROUND_Y + 1.0, 0.0)));
    system->tick(DT);
    auto view = system->snapshot();
    ASSERT_EQ(view.size(), 1u);
    EXPECT_GT(view[0].position.x, 0.1);
    EXPECT_EQ(view[0].faction, 1);
}
