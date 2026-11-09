#include <gtest/gtest.h>
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

#include <cmath>

using namespace finevox;

namespace {

class PersonalSpaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        EntityTypeRegistry::global().clear();
        EntityTypeDef spaced;
        spaced.name = "test_spaced";
        spaced.aiType = AIType::None;
        spaced.personalSpace = 0.4f;
        EntityTypeRegistry::global().registerType("test_spaced", std::move(spaced));

        EntityTypeDef crowded;
        crowded.name = "test_crowded";
        crowded.aiType = AIType::None;
        EntityTypeRegistry::global().registerType("test_crowded", std::move(crowded));

        auto stoneId = BlockTypeId::fromName("test_space_stone");
        BlockType stone;
        stone.setShape(CollisionShape::FULL_BLOCK).setOpaque(true);
        BlockRegistry::global().registerType(stoneId, stone);
        for (int x = -6; x <= 6; ++x) {
            for (int z = -6; z <= 6; ++z) world.setBlock({x, 0, z}, stoneId);
        }
    }

    void TearDown() override { EntityTypeRegistry::global().clear(); }

    EntityId spawn(EntityManager& em, const char* type, Vec3 at) {
        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName(type));
        mob->setPosition(at);
        mob->setOnGround(true);
        return em.spawnEntity(std::move(mob));
    }

    static float apart(EntityManager& em, EntityId a, EntityId b) {
        Vec3 p = em.getEntity(a)->position();
        Vec3 q = em.getEntity(b)->position();
        return std::sqrt((p.x - q.x) * (p.x - q.x) + (p.z - q.z) * (p.z - q.z));
    }

    World world;
    GraphicsEventQueue queue;
};

}  // namespace

TEST_F(PersonalSpaceTest, TwoOnOneSpotDriftApart) {
    EntityManager em(world, queue);
    EntityId a = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    EntityId b = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    for (int i = 0; i < 60; ++i) em.tick(0.05f);
    // Their two spaces add up to 0.8. The shove fades as they near it, so they come close.
    EXPECT_GT(apart(em, a, b), 0.7f);
    EXPECT_LE(apart(em, a, b), 0.85f);
}

TEST_F(PersonalSpaceTest, AShoveIsGentle) {
    EntityManager em(world, queue);
    EntityId a = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    EntityId b = spawn(em, "test_spaced", Vec3(0.55f, 1.0f, 0.5f));
    em.tick(0.05f);
    // Each moves at most 1.2 blocks a second, so a tick takes them no more than 0.06 each
    EXPECT_LE(apart(em, a, b), 0.05f + 2 * 0.06f + 1e-4f);
    EXPECT_GT(apart(em, a, b), 0.05f);
}

TEST_F(PersonalSpaceTest, NoSpaceMeansTheyStayPut) {
    EntityManager em(world, queue);
    EntityId a = spawn(em, "test_crowded", Vec3(0.5f, 1.0f, 0.5f));
    EntityId b = spawn(em, "test_crowded", Vec3(0.5f, 1.0f, 0.5f));
    for (int i = 0; i < 20; ++i) em.tick(0.05f);
    EXPECT_LT(apart(em, a, b), 1e-4f);
}

TEST_F(PersonalSpaceTest, OneWithoutSpaceIsntPushedAndDoesntPush) {
    EntityManager em(world, queue);
    EntityId spaced = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    EntityId crowded = spawn(em, "test_crowded", Vec3(0.6f, 1.0f, 0.5f));
    for (int i = 0; i < 20; ++i) em.tick(0.05f);
    EXPECT_NEAR(apart(em, spaced, crowded), 0.1f, 1e-4f);
}

TEST_F(PersonalSpaceTest, FarApartAreLeftAlone) {
    EntityManager em(world, queue);
    EntityId a = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    EntityId b = spawn(em, "test_spaced", Vec3(2.5f, 1.0f, 0.5f));
    for (int i = 0; i < 20; ++i) em.tick(0.05f);
    EXPECT_NEAR(apart(em, a, b), 2.0f, 1e-4f);
}

TEST_F(PersonalSpaceTest, OneOnTheMoveIsNeverPushed) {
    EntityManager em(world, queue);
    EntityId walker = spawn(em, "test_spaced", Vec3(0.5f, 1.0f, 0.5f));
    EntityId stander = spawn(em, "test_spaced", Vec3(0.6f, 1.0f, 0.5f));
    float startStander = em.getEntity(stander)->position().x;
    for (int i = 0; i < 10; ++i) {
        em.getEntity(walker)->setVelocity(Vec3(0.0f, 0.0f, 2.0f));
        em.tick(0.05f);
    }
    // Straight along its way, with nothing sideways. How far depends on friction, not on this.
    Vec3 p = em.getEntity(walker)->position();
    EXPECT_NEAR(p.x, 0.5f, 1e-4f);
    EXPECT_GT(p.z, 1.0f);
    // While the one standing was eased off towards +x at first, before the walker left it behind
    EXPECT_GT(em.getEntity(stander)->position().x, startStander);
}

// A block put where a body stands. These use the same floor, and units with no personal space.

TEST_F(PersonalSpaceTest, OneCaughtInABlockStepsUpOntoIt) {
    EntityManager em(world, queue);
    EntityId id = spawn(em, "test_crowded", Vec3(0.5f, 1.0f, 0.5f));
    world.setBlock({0, 1, 0}, BlockTypeId::fromName("test_space_stone"));
    em.tick(0.05f);
    Vec3 p = em.getEntity(id)->position();
    EXPECT_GE(p.y, 2.0f - 1e-3f);
    EXPECT_NEAR(p.x, 0.5f, 1e-4f);
}

TEST_F(PersonalSpaceTest, WithNoRoomAboveItsPutAside) {
    EntityManager em(world, queue);
    EntityId id = spawn(em, "test_crowded", Vec3(0.5f, 1.0f, 0.5f));
    auto stone = BlockTypeId::fromName("test_space_stone");
    for (int y = 1; y <= 4; ++y) world.setBlock({0, y, 0}, stone);
    em.tick(0.05f);
    Vec3 p = em.getEntity(id)->position();
    // Into a neighbouring cell, at the middle of it, on the floor
    bool aside = std::abs(p.x - 0.5f) > 0.9f || std::abs(p.z - 0.5f) > 0.9f;
    EXPECT_TRUE(aside) << p.x << " " << p.z;
    EXPECT_LT(p.y, 1.5f);
}

TEST_F(PersonalSpaceTest, StandingOnTheFloorAndAgainstAWallIsntCaught) {
    EntityManager em(world, queue);
    // Its side touching the block's face, its feet on the floor
    EntityId id = spawn(em, "test_crowded", Vec3(1.3f, 1.0f, 0.5f));
    world.setBlock({0, 1, 0}, BlockTypeId::fromName("test_space_stone"));
    for (int i = 0; i < 10; ++i) em.tick(0.05f);
    Vec3 p = em.getEntity(id)->position();
    EXPECT_NEAR(p.x, 1.3f, 1e-3f);
    EXPECT_NEAR(p.y, 1.0f, 1e-2f);
}
