#include <gtest/gtest.h>
#include "finevox/core/navigation.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

#include <cmath>
#include <iostream>
#include <memory>

using namespace finevox;

namespace {

constexpr int GROUND_Y = 10;
constexpr float DT = 1.0f / 30.0f;

class NavigationTest : public ::testing::Test {
protected:
    void SetUp() override {
        stone = BlockTypeId::fromName("nav_stone");
        road = BlockTypeId::fromName("nav_road");
        BlockType stoneType;
        stoneType.setCollisionShape(CollisionShape::FULL_BLOCK);
        BlockRegistry::global().registerOrUpdateType(stone, stoneType);
        BlockType roadType;
        roadType.setCollisionShape(CollisionShape::FULL_BLOCK);
        roadType.setWalkSpeed(1.5f);
        BlockRegistry::global().registerOrUpdateType(road, roadType);

        EntityTypeRegistry::global().clear();
        EntityTypeDef def;
        def.name = "nav_walker";
        def.aiType = AIType::None;
        def.maxSpeed = 4.0f;
        def.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
        EntityTypeRegistry::global().registerType("nav_walker", std::move(def));

        for (int x = -8; x < 64; ++x) {
            for (int z = -8; z < 64; ++z) {
                world.setBlock(x, GROUND_Y - 1, z, stone);
            }
        }
    }

    void wall(int x, int z0, int z1, int height) {
        for (int z = z0; z <= z1; ++z) {
            for (int y = 0; y < height; ++y) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }

    bool solidAt(BlockCoord p) const { return !world.getBlock(p).isAir(); }

    /// Stand the path up against the raw world, performing its build steps. This
    /// deliberately doesn't use isStandable: it is the check on the search.
    void replay(const NavPath& path, BlockCoord from) {
        BlockCoord at = from;
        for (const auto& step : path.steps) {
            if (step.action == NavAction::Place) {
                ASSERT_FALSE(solidAt(step.actionPos)) << "Place into occupied cell";
                world.setBlock(step.actionPos, stone);
                continue;
            }
            if (step.action == NavAction::Dig) {
                ASSERT_TRUE(solidAt(step.actionPos)) << "Dig of empty cell";
                world.setBlock(step.actionPos, BlockTypeId());
                continue;
            }
            int dx = std::abs(step.pos.x - at.x);
            int dz = std::abs(step.pos.z - at.z);
            ASSERT_LE(dx, 1) << "step jumps cells";
            ASSERT_LE(dz, 1) << "step jumps cells";
            ASSERT_LE(step.pos.y - at.y, 1) << "step climbs more than one block";
            ASSERT_GE(at.y - step.pos.y, -1);
            ASSERT_LE(at.y - step.pos.y, 3) << "drop too far";
            ASSERT_TRUE(solidAt({step.pos.x, step.pos.y - 1, step.pos.z})) << "no floor";
            ASSERT_FALSE(solidAt(step.pos)) << "feet in block";
            ASSERT_FALSE(solidAt({step.pos.x, step.pos.y + 1, step.pos.z})) << "head in block";
            at = step.pos;
        }
    }

    NavQuery query(glm::dvec3 from, glm::dvec3 to) {
        NavQuery q;
        q.start = from;
        q.goal = to;
        return q;
    }

    static glm::dvec3 cell(int x, int y, int z) { return {x + 0.5, y + 0.05, z + 0.5}; }

    World world;
    BlockTypeId stone;
    BlockTypeId road;
};

// Integration fixture: real EntityManager physics walking a real mob.
class NavigationAgentTest : public NavigationTest {
protected:
    void SetUp() override {
        NavigationTest::SetUp();
        entities = std::make_unique<EntityManager>(world, graphics);
        nav = std::make_unique<NavigationSystem>(world, *entities);
    }

    EntityId spawn(int x, int z, int y = GROUND_Y) {
        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName("nav_walker"));
        mob->setPosition(Vec3(x + 0.5f, float(y), z + 0.5f));
        return entities->spawnEntity(std::move(mob));
    }

    void tick(int count = 1) {
        for (int i = 0; i < count; ++i) {
            nav->tick(DT);
            entities->tick(DT);
        }
    }

    /// Tick until the agent reaches a terminal state or the deadline
    NavState settle(EntityId id, float seconds) {
        for (int i = 0; i < int(seconds / DT); ++i) {
            tick();
            NavState s = nav->state(id);
            if (s == NavState::Arrived || s == NavState::Failed || s == NavState::Blocked) return s;
        }
        return nav->state(id);
    }

    GraphicsEventQueue graphics;
    std::unique_ptr<EntityManager> entities;
    std::unique_ptr<NavigationSystem> nav;
};

}  // namespace

// ============================================================================
// Search
// ============================================================================

TEST_F(NavigationTest, FindsPathsFarBeyondTheOldPathfinderLimit) {
    auto path = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(60, GROUND_Y, 50)));
    ASSERT_TRUE(path.has_value());
    replay(*path, {0, GROUND_Y, 0});
    EXPECT_EQ(path->steps.back().pos, BlockCoord(60, GROUND_Y, 50));
    EXPECT_LE(path->steps.size(), 62u) << "8-way movement should cut the corner";
}

TEST_F(NavigationTest, DetoursAroundAWall) {
    wall(10, -8, 20, 3);
    auto path = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(20, GROUND_Y, 0)));
    ASSERT_TRUE(path.has_value());
    replay(*path, {0, GROUND_Y, 0});
    int maxZ = 0;
    for (const auto& step : path->steps) maxZ = std::max(maxZ, step.pos.z);
    EXPECT_GT(maxZ, 20) << "must go round the end of the wall";
}

TEST_F(NavigationTest, ClimbsSingleBlockStepsButNotTwoBlockWalls) {
    wall(10, -8, 63, 1);
    auto path = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(20, GROUND_Y, 0)));
    ASSERT_TRUE(path.has_value());
    replay(*path, {0, GROUND_Y, 0});

    wall(12, -8, 63, 2);
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(20, GROUND_Y, 0));
    q.maxNodes = 4000;
    EXPECT_FALSE(findNavPath(world, q).has_value());
}

TEST_F(NavigationTest, TakesShortDropsButNotLongOnes) {
    for (int x = 10; x <= 20; ++x) {
        for (int z = -8; z < 64; ++z) {
            for (int y = 0; y < 4; ++y) world.setBlock(x, GROUND_Y - 1 - y, z, BlockTypeId());
        }
    }
    for (int z = -8; z < 64; ++z) {
        for (int y = 3; y < 12; ++y) world.setBlock(10, GROUND_Y + y - 3 - 1, z, BlockTypeId());
    }
    for (int x = 10; x <= 20; ++x) {
        for (int z = -8; z < 64; ++z) world.setBlock(x, GROUND_Y - 4, z, stone);
    }

    // A three block drop into the pit is fine; climbing back out isn't.
    auto in = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(15, GROUND_Y - 3, 0)));
    ASSERT_TRUE(in.has_value());
    replay(*in, {0, GROUND_Y, 0});

    NavQuery out = query(cell(15, GROUND_Y - 3, 0), cell(0, GROUND_Y, 0));
    out.maxNodes = 4000;
    EXPECT_FALSE(findNavPath(world, out).has_value());
}

TEST_F(NavigationTest, LongerMaxFallGetsAStrandedUnitDown) {
    // A tower too tall to step down, with nothing to walk to from its top
    for (int y = 0; y < 8; ++y) {
        for (int x = 10; x < 13; ++x) {
            for (int z = 10; z < 13; ++z) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }
    NavQuery query;
    query.start = glm::dvec3(11.5, GROUND_Y + 8, 11.5);
    query.goal = glm::dvec3(20.5, GROUND_Y, 11.5);
    EXPECT_FALSE(findNavPath(world, query).has_value()) << "out of reach at the default";

    query.maxFall = 24;
    auto path = findNavPath(world, query);
    ASSERT_TRUE(path.has_value());
    EXPECT_EQ(path->steps.back().pos.y, GROUND_Y);
}

TEST_F(NavigationTest, LongFallIsALastResortNotAShortcut) {
    // A cliff with a staircase a little way along it: walking to the stairs beats jumping
    for (int x = 0; x < 30; ++x) {
        for (int z = 0; z < 8; ++z) {
            for (int y = 0; y < 6; ++y) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }
    for (int x = 14; x <= 16; ++x) {
        for (int z = 8; z < 14; ++z) {
            int height = 13 - z;
            for (int y = 0; y < height; ++y) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }
    NavQuery query;
    query.start = glm::dvec3(10.5, GROUND_Y + 6, 6.5);
    query.goal = glm::dvec3(10.5, GROUND_Y, 16.5);
    query.maxFall = 24;
    auto path = findNavPath(world, query);
    ASSERT_TRUE(path.has_value());

    int largestDrop = 0;
    for (size_t i = 1; i < path->steps.size(); ++i) {
        largestDrop = std::max(largestDrop, path->steps[i - 1].pos.y - path->steps[i].pos.y);
    }
    EXPECT_LE(largestDrop, query.comfortableFall);
}

TEST_F(NavigationTest, DoesNotSqueezeThroughDiagonalGaps) {
    // Blocks touching only at their corners form a barrier: x=4 below z=5, then a
    // diagonal chain, then x=7 above z=6. The only way across would be between
    // corner-touching blocks, which a body can't fit through.
    wall(4, -8, 4, 3);
    wall(5, 5, 5, 3);
    wall(6, 6, 6, 3);
    wall(7, 7, 63, 3);
    NavQuery q = query(cell(5, GROUND_Y, 6), cell(6, GROUND_Y, 5));
    q.maxNodes = 20000;
    EXPECT_FALSE(findNavPath(world, q).has_value());

    // Open one link in the chain and the same trip is possible
    for (int y = 0; y < 3; ++y) world.setBlock(6, GROUND_Y + y, 6, BlockTypeId());
    auto path = findNavPath(world, q);
    ASSERT_TRUE(path.has_value());
    replay(*path, {5, GROUND_Y, 6});
}

TEST_F(NavigationTest, RangedGoalStopsNextToTheTarget) {
    wall(10, 0, 0, 5);  // A "tree"
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(10, GROUND_Y, 0));
    q.goalRange = 1.0f;
    auto path = findNavPath(world, q);
    ASSERT_TRUE(path.has_value());
    replay(*path, {0, GROUND_Y, 0});
    BlockCoord end = path->steps.back().pos;
    EXPECT_LE(std::abs(end.x - 10) + std::abs(end.z), 2);
    EXPECT_NE(end, BlockCoord(10, GROUND_Y, 0));
}

TEST_F(NavigationTest, PrefersRoadsWhenTheDetourIsWorthIt) {
    for (int x = 0; x <= 30; ++x) world.setBlock(x, GROUND_Y - 1, 3, road);
    auto path = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(30, GROUND_Y, 0)));
    ASSERT_TRUE(path.has_value());
    int onRoad = 0;
    for (const auto& step : path->steps) onRoad += step.pos.z == 3;
    EXPECT_GT(onRoad, 15);
}

TEST_F(NavigationTest, OpenFieldSearchStaysCheap) {
    int nodes = 0;
    auto path = findNavPath(world, query(cell(0, GROUND_Y, 0), cell(60, GROUND_Y, 50)), &nodes);
    ASSERT_TRUE(path.has_value());
    EXPECT_LT(nodes, 12000) << "default budget must cover a long cross-map trip";
    std::cout << "open-field 60x50 expansions: " << nodes << "\n";
}

TEST_F(NavigationTest, AvoidsWalkingThroughEmptySpaceAtMapEdge) {
    // Beyond the generated area there is nothing to stand on
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(100, GROUND_Y, 0));
    q.maxNodes = 3000;
    EXPECT_FALSE(findNavPath(world, q).has_value());
}

TEST_F(NavigationTest, ConstructivePathBuildsAStaircaseToAHighLedge) {
    // A tower top five blocks up, with sheer sides
    for (int x = 20; x < 24; ++x) {
        for (int z = 0; z < 4; ++z) {
            for (int y = 0; y < 5; ++y) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }
    glm::dvec3 goal = cell(21, GROUND_Y + 5, 1);

    NavQuery walk = query(cell(0, GROUND_Y, 0), goal);
    walk.maxNodes = 4000;
    EXPECT_FALSE(findNavPath(world, walk).has_value());

    NavQuery build = walk;
    build.allowPlace = true;
    build.maxNodes = 40000;
    auto path = findNavPath(world, build);
    ASSERT_TRUE(path.has_value());

    int places = 0;
    for (const auto& step : path->steps) places += step.action == NavAction::Place;
    EXPECT_GE(places, 3);
    replay(*path, {0, GROUND_Y, 0});
    EXPECT_EQ(path->steps.back().pos, BlockCoord(21, GROUND_Y + 5, 1));
}

TEST_F(NavigationTest, ConstructivePathBridgesAGap) {
    for (int x = 10; x < 14; ++x) {
        for (int z = -8; z < 64; ++z) {
            for (int y = GROUND_Y - 1; y > 0; --y) world.setBlock(x, y, z, BlockTypeId());
        }
    }
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(20, GROUND_Y, 0));
    q.maxNodes = 4000;
    EXPECT_FALSE(findNavPath(world, q).has_value());

    q.allowPlace = true;
    q.maxNodes = 40000;
    auto path = findNavPath(world, q);
    ASSERT_TRUE(path.has_value());
    replay(*path, {0, GROUND_Y, 0});
}

TEST_F(NavigationTest, DiggingPathTunnelsThroughAWall) {
    wall(10, -8, 63, 3);
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(20, GROUND_Y, 0));
    q.maxNodes = 4000;
    EXPECT_FALSE(findNavPath(world, q).has_value());

    q.allowDig = true;
    q.maxNodes = 40000;
    auto path = findNavPath(world, q);
    ASSERT_TRUE(path.has_value());
    int digs = 0;
    for (const auto& step : path->steps) digs += step.action == NavAction::Dig;
    EXPECT_EQ(digs, 2);
    replay(*path, {0, GROUND_Y, 0});
}

// ============================================================================
// Agents, under real physics
// ============================================================================

TEST_F(NavigationAgentTest, WalksAlongAPathAndArrives) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(30, GROUND_Y, 20);
    nav->order(id, order);

    EXPECT_EQ(settle(id, 20.0f), NavState::Arrived);
    Vec3 pos = entities->getMob(id)->position();
    EXPECT_NEAR(pos.x, 30.5f, 1.0f);
    EXPECT_NEAR(pos.z, 20.5f, 1.0f);
    EXPECT_FALSE(entities->getMob(id)->hasMoveTarget());
}

TEST_F(NavigationAgentTest, ClimbsAStepUnderRealPhysics) {
    wall(10, -8, 63, 1);
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(20, GROUND_Y, 0);
    nav->order(id, order);

    EXPECT_EQ(settle(id, 15.0f), NavState::Arrived);
    EXPECT_NEAR(entities->getMob(id)->position().x, 20.5f, 1.0f);
}

TEST_F(NavigationAgentTest, RoadsMakeAgentsFaster) {
    auto timeTo = [&](int laneZ) {
        EntityId id = spawn(0, laneZ);
        tick(5);
        NavOrder order;
        order.goal = cell(40, GROUND_Y, laneZ);
        nav->order(id, order);
        int ticks = 0;
        while (nav->state(id) != NavState::Arrived && ticks < 1000) {
            tick();
            ++ticks;
        }
        return ticks;
    };

    int plain = timeTo(0);
    for (int x = 0; x <= 40; ++x) world.setBlock(x, GROUND_Y - 1, 10, road);
    int paved = timeTo(10);
    EXPECT_LT(paved, plain * 0.8f) << "plain=" << plain << " paved=" << paved;
}

TEST_F(NavigationAgentTest, StopsAtBuildStepsUntilResumed) {
    for (int x = 8; x < 12; ++x) {
        for (int z = -8; z < 64; ++z) {
            for (int y = 0; y < 3; ++y) world.setBlock(x, GROUND_Y + y, z, stone);
        }
    }
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(9, GROUND_Y + 3, 0);
    order.allowPlace = true;
    order.maxNodes = 40000;
    nav->order(id, order);

    int built = 0;
    for (int i = 0; i < 2000 && nav->state(id) != NavState::Arrived; ++i) {
        tick();
        if (nav->state(id) == NavState::Failed) break;
        if (nav->state(id) == NavState::Blocked) {
            auto action = nav->currentAction(id);
            ASSERT_TRUE(action.has_value());
            ASSERT_EQ(action->action, NavAction::Place);
            EXPECT_FALSE(entities->getMob(id)->hasMoveTarget()) << "must wait, not keep walking";
            world.setBlock(action->actionPos, stone);
            ++built;
            nav->resume(id);
        }
    }
    EXPECT_EQ(nav->state(id), NavState::Arrived);
    EXPECT_GE(built, 2);
    EXPECT_NEAR(entities->getMob(id)->position().y, GROUND_Y + 3, 0.2f);
}

TEST_F(NavigationAgentTest, RepathsWhenAWallAppearsMidRoute) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(30, GROUND_Y, 0);
    nav->order(id, order);
    tick(30);
    ASSERT_EQ(nav->state(id), NavState::Moving);

    wall(15, -8, 10, 3);
    EXPECT_EQ(settle(id, 30.0f), NavState::Arrived);
    EXPECT_NEAR(entities->getMob(id)->position().x, 30.5f, 1.0f);
}

TEST_F(NavigationTest, PartialSearchReturnsTheClosestRouteWhenItRunsOutOfNodes) {
    NavQuery q = query(cell(0, GROUND_Y, 0), cell(60, GROUND_Y, 50));
    q.maxNodes = 200;
    EXPECT_FALSE(findNavPath(world, q).has_value());

    q.allowPartial = true;
    auto path = findNavPath(world, q);
    ASSERT_TRUE(path.has_value());
    EXPECT_TRUE(path->partial);
    replay(*path, {0, GROUND_Y, 0});
    EXPECT_GT(path->steps.back().pos.x, 5) << "must make headway";
}

TEST_F(NavigationAgentTest, LongTripsArriveInHopsOfBoundedCost) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(60, GROUND_Y, 50);
    order.maxNodes = 250;   // Far too small for the whole trip at once
    order.allowPartial = true;
    nav->order(id, order);

    EXPECT_EQ(settle(id, 40.0f), NavState::Arrived);
    Vec3 pos = entities->getMob(id)->position();
    EXPECT_NEAR(pos.x, 60.5f, 1.0f);
    EXPECT_NEAR(pos.z, 50.5f, 1.0f);
}

TEST_F(NavigationAgentTest, PartialTripsGiveUpAtADeadEnd) {
    // Sealed in: walls all round a small yard, goal outside
    wall(10, -8, 20, 3);
    wall(-6, -8, 20, 3);
    for (int x = -6; x <= 10; ++x) {
        for (int y = 0; y < 3; ++y) {
            world.setBlock(x, GROUND_Y + y, 20, stone);
            world.setBlock(x, GROUND_Y + y, -8, stone);
        }
    }
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(50, GROUND_Y, 40);
    order.maxNodes = 300;
    order.allowPartial = true;
    nav->order(id, order);

    EXPECT_EQ(settle(id, 30.0f), NavState::Failed);
}

TEST_F(NavigationAgentTest, FailsWhenThereIsNoRoute) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(200, GROUND_Y, 0);
    order.maxNodes = 2000;
    nav->order(id, order);
    EXPECT_EQ(settle(id, 2.0f), NavState::Failed);

    nav->acknowledge(id);
    EXPECT_EQ(nav->state(id), NavState::Idle);
}

TEST_F(NavigationAgentTest, SearchBudgetSpreadsWorkAcrossTicks) {
    std::vector<EntityId> ids;
    for (int i = 0; i < 12; ++i) ids.push_back(spawn(i % 4, i / 4));
    tick(5);
    nav->setNodeBudget(600);
    for (EntityId id : ids) {
        NavOrder order;
        order.goal = cell(55, GROUND_Y, 40);
        nav->order(id, order);
    }

    tick(1);
    int pending = 0;
    for (EntityId id : ids) pending += nav->state(id) == NavState::Pending;
    EXPECT_GT(pending, 0) << "budget should defer some searches";

    tick(60);
    for (EntityId id : ids) EXPECT_NE(nav->state(id), NavState::Pending);
}

TEST_F(NavigationAgentTest, DeadAgentsAreForgotten) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(30, GROUND_Y, 0);
    nav->order(id, order);
    tick(10);
    EXPECT_EQ(nav->activeAgents(), 1u);

    entities->despawnEntity(id);
    tick(2);
    EXPECT_EQ(nav->activeAgents(), 0u);
}

TEST_F(NavigationAgentTest, StopCancelsTheOrder) {
    EntityId id = spawn(0, 0);
    tick(5);
    NavOrder order;
    order.goal = cell(30, GROUND_Y, 0);
    nav->order(id, order);
    tick(10);
    nav->stop(id);
    EXPECT_EQ(nav->state(id), NavState::Idle);
    EXPECT_FALSE(entities->getMob(id)->hasMoveTarget());
}
