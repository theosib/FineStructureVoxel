#include <gtest/gtest.h>
#include "finevox/core/ground_surface.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/navigation.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

using namespace finevox;

namespace {

constexpr float DT = 1.0f / 30.0f;
// Column heights are kept to the nearest 1/16, so within half of that of the drawn surface
constexpr double KEPT_WITHIN = 0.5 / GROUND_QUANTUM + 1e-3;

// Where steps climb diagonally, a corner of the drawn surface can reach a little more than a block
// past its face. Columns stop at a block.
double clampedY(const GroundPatch& patch, float u, float v) {
    const double level = patch.faceLevel();
    return std::clamp(double(patch.worldY(u, v)), level - 1.0, level + 1.0);
}
double beyondABlock = 0.0;  // The most it was past, for the record

class GroundSurfaceTest : public ::testing::Test {
protected:
    void SetUp() override {
        dirt = BlockTypeId::fromName("surface_test:dirt");
        stone = BlockTypeId::fromName("surface_test:stone");
        for (BlockTypeId type : {dirt, stone}) {
            BlockType block;
            block.setCollisionShape(CollisionShape::FULL_BLOCK);
            block.setOpaque(true);
            BlockRegistry::global().registerOrUpdateType(type, block);
        }
        water = FluidTypeId::fromName("surface_test:water");
        smooth = [this](BlockTypeId type) { return type == dirt; };
        world = std::make_unique<World>();
    }

    void makeSolid() {
        world->setGroundSurface(std::make_shared<GroundSurface>(*world, smooth));
        shapes = createBlockShapeProvider(*world);
    }

    /// Dirt from y = 0 up to (not including) height(x, z), over x and z in [0, size)
    void fill(int size, const std::function<int(int, int)>& height) {
        for (int x = 0; x < size; ++x) {
            for (int z = 0; z < size; ++z) {
                for (int y = 0; y < height(x, z); ++y) world->setBlock(x, y, z, dirt);
            }
        }
    }

    /// Bumpy ground: slopes, single steps and cliffs, like the smoothing tests
    std::vector<int> randomHeights(uint32_t seed, int size) {
        std::mt19937 rng(seed);
        std::vector<int> heights(size * size);
        std::uniform_int_distribution<int> step(-2, 2);
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                int h = 6;
                if (x > 0) h = heights[z * size + x - 1];
                if (z > 0) h = (h + heights[(z - 1) * size + x] + 1) / 2;
                int d = step(rng);
                if (std::abs(d) == 2 && rng() % 3 != 0) d /= 2;
                heights[z * size + x] = std::clamp(h + d, 2, 11);
            }
        }
        return heights;
    }

    /// The surface as the renderer draws it: the oracle
    [[nodiscard]] std::optional<GroundPatch> drawnTop(BlockCoord block) const {
        return groundPatch(groundCells(*world, smooth), block, 1);
    }

    /// Solid by the surface, or by the block where there's no surface in the cell
    [[nodiscard]] bool solid(const glm::dvec3& p) const {
        if (auto ground = world->groundSurface()->solidAt(p)) return *ground;
        return !world->getBlock(BlockCoord(int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z)))).isAir();
    }

    /// Every drawn face of a block in [0, size) across and [0, 16) up, top and underside, keeps to
    /// the drawn surface at the middle of each of its columns, by what's solid and by what a ray
    /// from the open side hits
    void expectSurfaceAsDrawn(int size, const std::string& when) {
        int checked = expectSurfaceAsDrawn(BlockCoord(0, 0, 0), BlockCoord(size - 1, 15, size - 1), when);
        EXPECT_GT(checked, size * size * GROUND_COLUMNS * GROUND_COLUMNS / 2) << when;
    }

    /// The same for the blocks in a box, inclusive, giving how many columns it checked
    int expectSurfaceAsDrawn(BlockCoord low, BlockCoord high, const std::string& when) {
        int checked = 0;
        for (int x = low.x; x <= high.x; ++x) {
            for (int z = low.z; z <= high.z; ++z) {
                for (int y = low.y; y <= high.y; ++y) {
                    for (int up : {1, -1}) {
                        const BlockCoord block(x, y, z);
                        const auto patch = groundPatch(groundCells(*world, smooth), block, up);
                        if (!patch) continue;
                        const char* side = up > 0 ? "top" : "underside";
                        for (int i = 0; i < GROUND_COLUMNS; ++i) {
                            for (int j = 0; j < GROUND_COLUMNS; ++j) {
                                const float u = (i + 0.5f) / GROUND_COLUMNS, v = (j + 0.5f) / GROUND_COLUMNS;
                                const double surface = clampedY(*patch, u, v);
                                beyondABlock = std::max(beyondABlock, std::abs(patch->worldY(u, v) - surface));
                                const glm::dvec3 at(x + u, surface, z + v);
                                const glm::dvec3 out(0, up * KEPT_WITHIN, 0);
                                // A block a cell thick can have its two faces meet: solid, it's then
                                // the thinnest step, a little either side of them
                                double thickness = 1.0;
                                if (auto back = groundPatch(groundCells(*world, smooth), block, -up)) {
                                    thickness = std::abs(surface - clampedY(*back, u, v));
                                }
                                const bool thin = thickness < 2.0 * KEPT_WITHIN;
                                EXPECT_TRUE(thin || solid(at - out)) << when << ": open behind the " << side << " at " << x
                                                             << "," << y << "," << z << " [" << i << "][" << j
                                                             << "], drawn at " << surface;
                                // In front of it is open, unless that's past the cell it looks into,
                                // or past the face across a gap a cell high, which can meet it
                                const glm::dvec3 front = at + out;
                                bool inGap = up > 0 ? front.y < y + 2 : front.y > y - 1;
                                double gap = 1.0;  // To the face across, if there is one
                                if (auto across = groundPatch(groundCells(*world, smooth), BlockCoord(x, y + 2 * up, z), -up)) {
                                    const double facing = clampedY(*across, u, v);
                                    gap = std::abs(facing - surface);
                                    // Each is kept to within KEPT_WITHIN, so a gap narrower than both can close
                                    if (gap < 2.0 * KEPT_WITHIN || (up > 0 ? front.y >= facing : front.y <= facing)) {
                                        inGap = false;
                                    }
                                }
                                if (inGap) {
                                    EXPECT_FALSE(solid(front)) << when << ": solid in front of the " << side << " at "
                                                               << x << "," << y << "," << z << " [" << i << "][" << j
                                                               << "], drawn at " << surface;
                                }
                                // A ray from a little way out in the open meets it
                                const glm::dvec3 from = at + glm::dvec3(0, up * std::min(0.25, gap * 0.5), 0);
                                if (gap > 2.0 * KEPT_WITHIN && !solid(from)) {
                                    const RaycastResult hit = raycastBlocks(Vec3(glm::vec3(from)), Vec3(0, float(-up), 0),
                                                                            1.0f, RaycastMode::Collision, shapes);
                                    EXPECT_TRUE(hit.hit) << when;
                                    EXPECT_NEAR(hit.hitPoint.y, surface, thin ? 2.0 * KEPT_WITHIN : KEPT_WITHIN)
                                        << when << ": " << side << " at " << x << "," << y << "," << z << " [" << i
                                        << "][" << j << "]";
                                }
                                ++checked;
                                if (HasFailure()) return checked;
                            }
                        }
                    }
                }
            }
        }
        return checked;
    }

    /// Walk a body along x on the ground for `seconds`, returning how many moves left it in the air
    int walk(SimplePhysicsBody& body, PhysicsSystem& physics, float speed, float seconds) {
        int airborne = 0;
        for (int t = 0; t < int(seconds / DT); ++t) {
            Vec3 velocity = body.velocity();
            body.setVelocity(Vec3(speed, velocity.y, 0.0f));
            physics.update(body, DT);
            airborne += !body.isOnGround();
        }
        return airborne;
    }

    /// A unit under navigation, with physics as a game with solid slopes has it
    struct Walker {
        GraphicsEventQueue graphics;
        EntityManager entities;
        NavigationSystem nav;
        EntityId id = INVALID_ENTITY_ID;

        Walker(World& world, Vec3 at) : entities(world, graphics), nav(world, entities) {
            EntityTypeRegistry::global().clear();
            EntityTypeDef def;
            def.name = "surface_walker";
            def.aiType = AIType::None;
            def.maxSpeed = 4.0f;
            def.halfExtents = glm::vec3(0.3f, 0.9f, 0.3f);
            EntityTypeRegistry::global().registerType("surface_walker", std::move(def));
            entities.physics().setGroundSnap(GROUND_SNAP);
            auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId::fromName("surface_walker"));
            mob->setPosition(at);
            id = entities.spawnEntity(std::move(mob));
            for (int t = 0; t < 5; ++t) entities.tick(DT);
        }

        [[nodiscard]] MobEntity& mob() { return *entities.getMob(id); }

        /// Send it to `goal`, and tick until it's there or `seconds` have gone, `each` seeing every tick
        NavState go(glm::dvec3 goal, float seconds, const std::function<void(const MobEntity&)>& each = nullptr) {
            NavOrder order;
            order.goal = goal;
            nav.order(id, order);
            for (int t = 0; t < int(seconds / DT) && nav.state(id) != NavState::Arrived; ++t) {
                nav.tick(DT);
                entities.tick(DT);
                if (each) each(mob());
            }
            return nav.state(id);
        }
    };

    std::unique_ptr<World> world;
    BlockTypeId dirt;
    BlockTypeId stone;
    FluidTypeId water;
    std::function<bool(BlockTypeId)> smooth;
    BlockShapeProvider shapes;
};

}  // namespace

// ============================================================================
// The surface
// ============================================================================

// The example that asked for it: a hole a block deep is drawn as a shallow dip, and a body that
// walks in stands in the dip, not at the bottom of the hole
TEST_F(GroundSurfaceTest, OneBlockHoleIsAShallowDip) {
    fill(16, [](int, int) { return 10; });
    world->setBlock(8, 9, 8, AIR_BLOCK_TYPE);
    makeSolid();

    PhysicsSystem physics(shapes);
    SimplePhysicsBody body(Vec3(8.5f, 11.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    for (int t = 0; t < 60; ++t) physics.update(body, DT);

    const double bottom = drawnTop(BlockCoord(8, 8, 8))->worldY(0.5f, 0.5f);
    EXPECT_TRUE(body.isOnGround());
    EXPECT_GT(body.position().y, bottom - KEPT_WITHIN) << "it sank below the drawn surface";
    EXPECT_LT(body.position().y, 9.8f) << "it's held up by the ground round the hole";
    EXPECT_GT(body.position().y, 9.3f) << "it fell to the bottom of the hole, at 9";
}

// Without one, the world collides as cubes, as it always did
TEST_F(GroundSurfaceTest, CubesWithoutASurface) {
    fill(16, [](int, int) { return 10; });
    world->setBlock(8, 9, 8, AIR_BLOCK_TYPE);
    shapes = createBlockShapeProvider(*world);

    PhysicsSystem physics(shapes);
    SimplePhysicsBody body(Vec3(8.5f, 11.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    for (int t = 0; t < 60; ++t) physics.update(body, DT);
    EXPECT_NEAR(body.position().y, 9.0f, 0.01f);
}

// Every face of random ground, with water in its hollows, collides where it's drawn
TEST_F(GroundSurfaceTest, CollidesWhereItsDrawnOnRandomGround) {
    constexpr int SIZE = 20;
    for (uint32_t seed = 1; seed <= 6; ++seed) {
        world = std::make_unique<World>();
        auto heights = randomHeights(seed, SIZE);
        fill(SIZE, [&](int x, int z) { return heights[z * SIZE + x]; });
        for (int x = 0; x < SIZE; ++x) {
            for (int z = 0; z < SIZE; ++z) {
                for (int y = heights[z * SIZE + x]; y < 5; ++y) world->setFluid(BlockCoord(x, y, z), water, 8);
            }
        }
        // A shelf of ground hanging over part of it, its underside stepping
        std::mt19937 rng(seed);
        const int shelfX = int(rng() % 8), shelfZ = int(rng() % 8);
        for (int x = shelfX; x < shelfX + 9; ++x) {
            for (int z = shelfZ; z < shelfZ + 8; ++z) {
                for (int y = 13 + int((x * 7 + z * 3 + seed) % 2); y < 16; ++y) world->setBlock(x, y, z, dirt);
            }
        }
        // And a few posts, which pin the ground round them
        for (int i = 0; i < 8; ++i) {
            int x = 2 + int(rng() % (SIZE - 4)), z = 2 + int(rng() % (SIZE - 4));
            world->setBlock(x, heights[z * SIZE + x], z, stone);
        }
        makeSolid();
        expectSurfaceAsDrawn(SIZE, "seed " + std::to_string(seed));
    }
    std::cout << "  drawn surface past a block from its face by up to " << beyondABlock << "\n";
    EXPECT_LT(beyondABlock, 0.2);
}

// A kept face is forgotten when anything it reads changes: blocks dug and placed, overhangs, water
// coming and going, near and a few cells off. A face can read three cells below itself, and it
// takes some hundreds of changes to find one that does, so after each change only the faces that
// could read it, and a cell more each way, are checked again.
TEST_F(GroundSurfaceTest, KeepsUpWithChanges) {
    constexpr int SIZE = 14;
    auto heights = randomHeights(7, SIZE);
    fill(SIZE, [&](int x, int z) { return heights[z * SIZE + x]; });
    makeSolid();
    expectSurfaceAsDrawn(SIZE, "before");
    ASSERT_GT(world->groundSurface()->facesKept(), 0u);

    std::mt19937 rng(99);
    for (int change = 0; change < 3000; ++change) {
        const int x = int(rng() % SIZE), z = int(rng() % SIZE);
        const int top = heights[z * SIZE + x];
        const int y = std::clamp(top - 3 + int(rng() % 8), 1, 14);
        const BlockCoord at(x, y, z);
        switch (rng() % 4) {
            case 0:
            case 1:
                world->setBlock(at, world->getBlock(at).isAir() ? dirt : AIR_BLOCK_TYPE);
                break;
            case 2:
                world->setBlock(at, world->getBlock(at).isAir() ? stone : AIR_BLOCK_TYPE);
                break;
            default:
                if (world->hasFluid(at)) {
                    world->removeFluid(at);
                } else if (world->getBlock(at).isAir()) {
                    world->setFluid(at, water, 8);
                }
                break;
        }
        const BlockCoord low(std::max(x - 2, 0), std::max(y - 5, 0), std::max(z - 2, 0));
        const BlockCoord high(std::min(x + 2, SIZE - 1), std::min(y + 4, 15), std::min(z + 2, SIZE - 1));
        expectSurfaceAsDrawn(low, high, "after change " + std::to_string(change));
        if (HasFailure()) return;
    }
    expectSurfaceAsDrawn(SIZE, "after them all");
}

// A column that comes in after the faces beside it were worked out reshapes them
TEST_F(GroundSurfaceTest, KeepsUpWithColumnsArriving) {
    fill(16, [](int, int) { return 6; });
    makeSolid();
    expectSurfaceAsDrawn(16, "alone");
    // The next column over, higher: the edge of this one now rises to meet it
    for (int x = 16; x < 32; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 7; ++y) world->setBlock(x, y, z, dirt);
        }
    }
    expectSurfaceAsDrawn(17, "with the next column");
    EXPECT_TRUE(solid(glm::dvec3(15.9, 6.1, 8.5))) << "only checks something if the edge has moved";
}

// ============================================================================
// Bodies on it
// ============================================================================

// Down a staircase drawn as a slope, a walking body stays on its feet the whole way, as long as
// it's put down onto each small step; without that, it's in the air most of the way
TEST_F(GroundSurfaceTest, WalkingDownASlopeKeepsItsFeet) {
    fill(24, [](int x, int) { return std::clamp(16 - x, 6, 14); });
    makeSolid();

    PhysicsSystem physics(shapes);
    physics.setGroundSnap(GROUND_SNAP);
    SimplePhysicsBody body(Vec3(2.5f, 14.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    for (int t = 0; t < 30; ++t) physics.update(body, DT);
    ASSERT_TRUE(body.isOnGround());
    EXPECT_EQ(walk(body, physics, 3.0f, 2.5f), 0);
    EXPECT_GT(body.position().x, 9.5f);
    EXPECT_LT(body.position().y, 8.0f) << "it came down the slope";

    PhysicsSystem falling(shapes);
    SimplePhysicsBody other(Vec3(2.5f, 14.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    for (int t = 0; t < 30; ++t) falling.update(other, DT);
    EXPECT_GT(walk(other, falling, 3.0f, 2.5f), 20) << "the test needs the snap to pass";
}

// Up the same slope, a body with the usual step height walks up it, and stands on it as drawn
TEST_F(GroundSurfaceTest, WalkingUpASlopeStandsOnIt) {
    fill(24, [](int x, int) { return std::clamp(16 - x, 6, 14); });
    makeSolid();

    PhysicsSystem physics(shapes);
    physics.setGroundSnap(GROUND_SNAP);
    SimplePhysicsBody body(Vec3(12.5f, 8.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    for (int t = 0; t < 30; ++t) physics.update(body, DT);
    walk(body, physics, -3.0f, 2.0f);
    EXPECT_LT(body.position().x, 7.0f) << "it didn't get up the slope";

    // Its feet rest on the highest column under it: never below the drawn surface at its middle,
    // and no more above it than half its width up a 45 degree slope, and a column's width more
    const Vec3 at = body.position();
    const int x = int(std::floor(at.x)), z = int(std::floor(at.z));
    const double surface = drawnTop(BlockCoord(x, std::clamp(16 - x, 6, 14) - 1, z))->worldY(at.x - x, at.z - z);
    EXPECT_GT(at.y, surface - KEPT_WITHIN);
    EXPECT_LT(at.y, surface + 0.3 + 1.0 / GROUND_COLUMNS + KEPT_WITHIN);
}

// ============================================================================
// Paths
// ============================================================================

// Where the ground dips, a body's feet are inside the block it stands on. A search from there
// starts from the cell over it, as from a slab.
TEST_F(GroundSurfaceTest, PathsStartOverTheBlockUnderADip) {
    fill(24, [](int, int) { return 10; });
    world->setBlock(8, 9, 8, AIR_BLOCK_TYPE);
    makeSolid();

    NavQuery query;
    query.start = glm::dvec3(9.05, 9.7, 8.5);  // On the near edge of the hole, which dips into (9, 9, 8)
    ASSERT_TRUE(solid(query.start - glm::dvec3(0, 0.05, 0))) << "the start isn't in the dip";
    query.goal = glm::dvec3(18.5, 10.0, 8.5);
    auto path = findNavPath(*world, query);
    ASSERT_TRUE(path.has_value());
    ASSERT_FALSE(path->steps.empty());
    for (const NavStep& step : path->steps) EXPECT_EQ(step.pos.y, 10);
}

// A unit sent across ground pocked with holes, each drawn as a dip, walks through them and gets there
TEST_F(GroundSurfaceTest, UnitsWalkAcrossPockedGround) {
    fill(40, [](int, int) { return 10; });
    for (int x = 4; x < 30; x += 3) {
        for (int z = 2; z < 14; z += 4) world->setBlock(x, 9, z + (x % 2), AIR_BLOCK_TYPE);
    }
    world->setBlock(16, 9, 8, AIR_BLOCK_TYPE);  // One right on the way
    makeSolid();

    Walker walker(*world, Vec3(1.5f, 10.0f, 8.5f));
    float lowest = 99.0f;
    NavState state = walker.go(glm::dvec3(34.5, 10.0, 8.5), 20.0f,
                               [&](const MobEntity& mob) { lowest = std::min(lowest, mob.position().y); });
    EXPECT_EQ(state, NavState::Arrived);
    EXPECT_GT(lowest, 9.3f) << "it went down a hole rather than through a dip";
    EXPECT_LT(lowest, 9.95f) << "it never went through a dip, so this checks nothing";
}

// Where ground meets something built, the edge stays level but the corners beside it can sit lower.
// A unit climbing a block onto it from there climbs more than a block, and still gets up. Here the
// only way onto a post two high is from a bump of ground a block high against it.
TEST_F(GroundSurfaceTest, UnitsStepUpOntoBuiltBlocksFromADip) {
    fill(24, [](int x, int z) { return x == 9 && z == 8 ? 10 : 9; });
    world->setBlock(9, 9, 9, stone);
    world->setBlock(9, 10, 9, stone);
    makeSolid();

    // How high it stands on the bump, against the post
    const auto bump = drawnTop(BlockCoord(9, 9, 8));
    ASSERT_TRUE(bump.has_value());
    float highest = -1.0f;
    for (float u = 0.2f; u <= 0.8f; u += 0.05f) highest = std::max(highest, bump->worldY(u, 0.9f));
    ASSERT_LT(highest, 11.0f - 1.1f) << "the step up from the bump is no more than a block, so this checks nothing";

    Walker walker(*world, Vec3(9.5f, 9.0f, 3.5f));
    EXPECT_EQ(walker.go(glm::dvec3(9.5, 11.0, 9.5), 15.0f), NavState::Arrived);
    EXPECT_GT(walker.mob().position().y, 10.95f) << "it isn't on the post";
}

// A ceiling is solid as it's drawn too. A block hanging a step lower than the ceiling round it is
// drawn as a shallow dome, and a body jumping under it goes up into the dome, past where the cube's
// underside was, and stops at the drawn ceiling.
TEST_F(GroundSurfaceTest, HeadsMeetTheCeilingAsDrawn) {
    fill(16, [](int, int) { return 10; });
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 14; y < 17; ++y) world->setBlock(x, y, z, dirt);
        }
    }
    world->setBlock(8, 13, 8, dirt);
    makeSolid();

    const auto dome = groundPatch(groundCells(*world, smooth), BlockCoord(8, 13, 8), -1);
    ASSERT_TRUE(dome.has_value());
    const double middle = dome->worldY(0.5f, 0.5f);
    ASSERT_GT(middle, 13.3) << "it isn't drawn as a dome, so this checks nothing";

    PhysicsSystem physics(shapes);
    SimplePhysicsBody body(Vec3(8.5f, 10.0f, 8.5f), Vec3(0.3f, 0.9f, 0.3f));
    body.setVelocity(Vec3(0.0f, 12.0f, 0.0f));
    float peak = 0.0f;
    for (int t = 0; t < 30; ++t) {
        physics.update(body, DT);
        peak = std::max(peak, body.position().y + 1.8f);
    }
    EXPECT_GT(peak, 13.2f) << "its head stopped at the cube's underside, at 13";
    EXPECT_LT(peak, middle + KEPT_WITHIN) << "its head went past the drawn ceiling";
}
