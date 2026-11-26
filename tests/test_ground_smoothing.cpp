#include <gtest/gtest.h>
#include "finevox/core/ground_smoothing.hpp"
#include "finevox/core/entity_render_state.hpp"
#include "finevox/core/mesh.hpp"
#include "finevox/core/subchunk.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <map>
#include <random>
#include <vector>

using namespace finevox;

namespace {

// Ground up to (not including) height(x, z) in every column, and whatever `extra` says on top
struct Terrain {
    std::function<int(int, int)> height;
    std::function<GroundCell(const BlockCoord&)> extra;  // nullopt-like: Open means "use the field"

    [[nodiscard]] GroundCell at(const BlockCoord& p) const {
        if (extra) {
            GroundCell e = extra(p);
            if (e != GroundCell::Open) return e;
        }
        return p.y < height(p.x, p.z) ? GroundCell::Ground : GroundCell::Open;
    }
    [[nodiscard]] GroundCellProvider cells() const {
        return [this](const BlockCoord& p) { return at(p); };
    }
};

GroundPatch topOf(const Terrain& t, int x, int z) {
    auto patch = groundPatch(t.cells(), BlockCoord(x, t.height(x, z) - 1, z), 1);
    EXPECT_TRUE(patch.has_value());
    return patch.value_or(GroundPatch{});
}

float worldHeight(const GroundPatch& p, float u, float v) { return p.worldY(u, v); }

}  // namespace

// ============================================================================
// The surface's shape
// ============================================================================

TEST(GroundSmoothing, FlatGroundStaysFlat) {
    Terrain t{[](int, int) { return 5; }, nullptr};
    GroundPatch p = topOf(t, 3, 3);
    EXPECT_TRUE(p.flat());
    EXPECT_EQ(p.steps[0], 1);
    EXPECT_EQ(p.steps[1], 1);
    EXPECT_FLOAT_EQ(p.worldY(0.3f, 0.7f), 5.0f);
}

// A staircase is a straight 45 degree slope, through the middle of every tread
TEST(GroundSmoothing, StaircaseIsAStraightSlope) {
    Terrain t{[](int x, int) { return 10 + x; }, nullptr};
    for (int x = -3; x <= 3; ++x) {
        GroundPatch p = topOf(t, x, 0);
        EXPECT_EQ(p.steps[0], 1) << "a slope is one piece along it";
        EXPECT_EQ(p.steps[1], 1);
        for (float u = 0.0f; u <= 1.0f; u += 0.125f) {
            for (float v : {0.0f, 0.5f, 1.0f}) {
                EXPECT_NEAR(worldHeight(p, u, v), 10.0f + float(x) + u - 0.5f, 1e-5f) << x << " " << u;
            }
        }
    }
}

// A staircase on the diagonal is a plane too: all four columns at a corner count
TEST(GroundSmoothing, DiagonalStaircaseIsAPlane) {
    Terrain t{[](int x, int z) { return 10 + x + z; }, nullptr};
    GroundPatch p = topOf(t, 0, 0);
    for (float u = 0.0f; u <= 1.0f; u += 0.25f) {
        for (float v = 0.0f; v <= 1.0f; v += 0.25f) {
            EXPECT_NEAR(worldHeight(p, u, v), 9.0f + u + v, 1e-5f);
        }
    }
}

// One step on flat ground is an S-curve two blocks long: flat at the far edges, 45 degrees at
// the step, meeting at half height with no kink
TEST(GroundSmoothing, SingleStepIsARoundedRamp) {
    Terrain t{[](int x, int) { return x < 0 ? 5 : 6; }, nullptr};
    GroundPatch low = topOf(t, -1, 0), high = topOf(t, 0, 0);
    EXPECT_NEAR(worldHeight(low, 0.0f, 0.5f), 5.0f, 1e-5f);
    EXPECT_NEAR(worldHeight(low, 1.0f, 0.5f), 5.5f, 1e-5f);
    EXPECT_NEAR(worldHeight(high, 0.0f, 0.5f), 5.5f, 1e-5f);
    EXPECT_NEAR(worldHeight(high, 1.0f, 0.5f), 6.0f, 1e-5f);
    EXPECT_NEAR(low.slope(0.0f, 0.5f).x, 0.0f, 1e-5f) << "starts level";
    EXPECT_NEAR(low.slope(1.0f, 0.5f).x, 1.0f, 1e-5f);
    EXPECT_NEAR(high.slope(0.0f, 0.5f).x, 1.0f, 1e-5f) << "the same slope on both sides";
    EXPECT_NEAR(high.slope(1.0f, 0.5f).x, 0.0f, 1e-5f) << "ends level";
    // And the columns further out aren't touched
    EXPECT_TRUE(topOf(t, -2, 0).flat());
    EXPECT_TRUE(topOf(t, 1, 0).flat());
}

// A step of two rounds off at the top and bottom, half a block each, and leaves a wall a
// block high between them: still too high to step up
TEST(GroundSmoothing, TwoBlockStepKeepsAWall) {
    Terrain t{[](int x, int) { return x < 0 ? 5 : 7; }, nullptr};
    GroundPatch low = topOf(t, -1, 0), high = topOf(t, 0, 0);
    EXPECT_EQ(low.edge[GROUND_EDGE_X_HIGH], GroundEdge::Up);
    EXPECT_EQ(high.edge[GROUND_EDGE_X_LOW], GroundEdge::Down);
    for (float v : {0.0f, 0.5f, 1.0f}) {
        EXPECT_NEAR(worldHeight(low, 1.0f, v), 5.5f, 1e-5f);
        EXPECT_NEAR(worldHeight(high, 0.0f, v), 6.5f, 1e-5f);
    }
    EXPECT_NEAR(worldHeight(low, 0.0f, 0.5f), 5.0f, 1e-5f);
    EXPECT_NEAR(worldHeight(high, 1.0f, 0.5f), 7.0f, 1e-5f);
}

// Something built beside ground: the ground meets it level (where the corners dip below it, the
// mesh has a wall to close the gap: MeshHasNoHolesOnRandomGround)
TEST(GroundSmoothing, BuiltThingsPinTheEdge) {
    // A flagstone floor at x >= 0, level with ground that slopes down away from it
    Terrain t{[](int x, int z) { return x < 0 ? 5 - (z < 0 ? 1 : 0) : 4; },
              [](const BlockCoord& p) { return p.x >= 0 && p.y == 4 ? GroundCell::Solid : GroundCell::Open; }};
    GroundPatch p = topOf(t, -1, 0);
    EXPECT_EQ(p.edge[GROUND_EDGE_X_HIGH], GroundEdge::Pin);
    EXPECT_NEAR(p.control[2][1], 0.0f, 1e-6f);
    EXPECT_NEAR(worldHeight(p, 1.0f, 0.5f), 5.0f - 0.125f, 1e-5f) << "the low corner pulls it down a little";
}

// Water against a dry bank leaves the bank's edge where it is
// A bank runs down into the water as into air: the fluid mesh draws water over what's under it
TEST(GroundSmoothing, BanksSlopeIntoWater) {
    Terrain t{[](int x, int) { return x < 0 ? 6 : 4; },
              [](const BlockCoord& p) { return p.x >= 0 && p.y >= 4 && p.y < 6 ? GroundCell::Wet : GroundCell::Open; }};
    GroundPatch bank = topOf(t, -1, 0);
    EXPECT_EQ(bank.edge[GROUND_EDGE_X_HIGH], GroundEdge::Down) << "two deep: it rounds off, as into air";
    EXPECT_NEAR(worldHeight(bank, 1.0f, 0.5f), 5.5f, 1e-5f);

    // A block lower, the bank and the bed meet in one slope
    Terrain shallow{[](int x, int) { return x < 0 ? 6 : 5; },
                    [](const BlockCoord& p) { return p.x >= 0 && p.y == 5 ? GroundCell::Wet : GroundCell::Open; }};
    GroundPatch shore = topOf(shallow, -1, 0);
    GroundPatch bed = topOf(shallow, 0, 0);
    EXPECT_EQ(shore.edge[GROUND_EDGE_X_HIGH], GroundEdge::Link);
    EXPECT_EQ(bed.edge[GROUND_EDGE_X_LOW], GroundEdge::Link);
    EXPECT_NEAR(worldHeight(shore, 1.0f, 0.5f), worldHeight(bed, 0.0f, 0.5f), 1e-5f);
}

// A bank a block above the water doesn't round off and stand as a wall over the waterline: it runs
// down under the water
TEST(GroundSmoothing, BanksAboveTheWaterRunUnderIt) {
    Terrain t{[](int x, int) { return x < 0 ? 6 : 4; },
              [](const BlockCoord& p) { return p.x >= 0 && p.y == 4 ? GroundCell::Wet : GroundCell::Open; }};
    GroundPatch bank = topOf(t, -1, 0);
    GroundPatch bed = topOf(t, 0, 0);
    EXPECT_EQ(bank.edge[GROUND_EDGE_X_HIGH], GroundEdge::Shore);
    EXPECT_EQ(bed.edge[GROUND_EDGE_X_LOW], GroundEdge::Up);
    // Down under the waterline, but only a little: past a block from its face, a body wouldn't
    // stand on it as drawn
    const float waterline = 4.0f + 14.0f / 16.0f;
    for (float v : {0.0f, 0.5f, 1.0f}) {
        EXPECT_LT(worldHeight(bank, 1.0f, v), waterline - 0.05f) << v;
        EXPECT_GE(worldHeight(bank, 1.0f, v), 4.8f) << v;
    }
    EXPECT_LT(worldHeight(bed, 0.0f, 0.5f), worldHeight(bank, 1.0f, 0.5f)) << "the wall between is under water";

    // Over dry ground the same drop still rounds off, with a wall
    Terrain dry{[](int x, int) { return x < 0 ? 6 : 4; }, nullptr};
    GroundPatch cliff = topOf(dry, -1, 0);
    EXPECT_EQ(cliff.edge[GROUND_EDGE_X_HIGH], GroundEdge::Down);
    EXPECT_NEAR(worldHeight(cliff, 1.0f, 0.5f), 5.5f, 1e-5f);
}

// An underside is the same shape, upside down
TEST(GroundSmoothing, CeilingsSmoothToo) {
    // Solid ground from y = 20 up to the sky, its underside stepping up in x
    Terrain t{[](int, int) { return 0; },
              [](const BlockCoord& p) { return p.y >= 20 + p.x ? GroundCell::Ground : GroundCell::Open; }};
    auto p = groundPatch(t.cells(), BlockCoord(0, 20, 0), -1);
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->faceLevel(), 20);
    for (float u = 0.0f; u <= 1.0f; u += 0.25f) {
        EXPECT_NEAR(p->worldY(u, 0.5f), 20.0f + u - 0.5f, 1e-5f);
    }
    EXPECT_LT(p->normal(0.5f, 0.5f).y, 0.0f);
}

TEST(GroundSmoothing, NoFaceWhereItIsCovered) {
    Terrain t{[](int, int) { return 5; }, nullptr};
    EXPECT_FALSE(groundPatch(t.cells(), BlockCoord(0, 3, 0), 1).has_value()) << "under more ground";
    EXPECT_FALSE(groundPatch(t.cells(), BlockCoord(0, 6, 0), 1).has_value()) << "air";
}

// Two faces that run on into each other meet exactly along their shared edge, whatever the
// ground around them: anywhere they didn't, a sliver of wall would show between them
TEST(GroundSmoothing, LinkedFacesMeetExactly) {
    constexpr int SIZE = 20;
    int edges = 0, gaps = 0;
    for (uint32_t seed = 1; seed <= 200; ++seed) {
        std::mt19937 rng(seed);
        std::vector<int> heights(SIZE * SIZE);
        for (int& h : heights) h = 10 + int(rng() % 4);
        // Most worlds with most steps one high, as in real ground; the rest anything
        if (seed % 4 != 0) {
            for (int z = 0; z < SIZE; ++z) {
                for (int x = 0; x < SIZE; ++x) {
                    int& h = heights[z * SIZE + x];
                    if (x > 0) h = std::clamp(h, heights[z * SIZE + x - 1] - 1, heights[z * SIZE + x - 1] + 1);
                    if (z > 0) h = std::clamp(h, heights[(z - 1) * SIZE + x] - 1, heights[(z - 1) * SIZE + x] + 1);
                }
            }
        }
        auto height = [&](int x, int z) { return heights[std::clamp(z, 0, SIZE - 1) * SIZE + std::clamp(x, 0, SIZE - 1)]; };
        Terrain t{height, [&](const BlockCoord& p) {
                      // A built block here and there, and water in some hollows
                      if (p.y == height(p.x, p.z) && (p.x * 7 + p.z * 13 + int(seed)) % 29 == 0) return GroundCell::Solid;
                      if (seed % 3 == 0 && p.y >= height(p.x, p.z) && p.y < 11) return GroundCell::Wet;
                      return GroundCell::Open;
                  }};
        auto cells = t.cells();
        for (int z = 2; z < SIZE - 2; ++z) {
            for (int x = 2; x < SIZE - 2; ++x) {
                auto p = groundPatch(cells, BlockCoord(x, height(x, z) - 1, z), 1);
                if (!p) continue;
                static constexpr int STEP[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
                for (int e = 0; e < 4; ++e) {
                    if (p->edge[e] != GroundEdge::Link) continue;
                    BlockCoord next(x + STEP[e][0], p->block.y + p->link[e], z + STEP[e][1]);
                    auto q = groundPatch(cells, next, 1);
                    ASSERT_TRUE(q.has_value()) << "a link to nothing";
                    EXPECT_EQ(q->edge[e ^ 1], GroundEdge::Link) << "links go both ways";
                    ++edges;
                    for (float s = 0.0f; s <= 1.0f; s += 0.25f) {
                        float mine = p->worldY(e < 2 ? float(e) : s, e < 2 ? s : float(e - 2));
                        float theirs = q->worldY(e < 2 ? float(e ^ 1) : s, e < 2 ? s : float((e ^ 1) - 2));
                        if (std::abs(mine - theirs) > 1e-5f) {
                            ++gaps;
                            if (gaps < 5) {
                                ADD_FAILURE() << "seed " << seed << " at " << x << "," << z << " edge " << e
                                              << " s " << s << ": " << mine << " against " << theirs;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    EXPECT_GT(edges, 10000);
    EXPECT_EQ(gaps, 0);
}

// ============================================================================
// Bodies on it
// ============================================================================

// Walking up a staircase, the way physics has it: a body steps up a whole block while its
// middle is still 0.3 short of the riser. Drawn on the surface, it goes up smoothly.
TEST(GroundSmoothing, BodyClimbingAStaircaseRisesSmoothly) {
    Terrain t{[](int x, int) { return 10 + x; }, nullptr};
    auto cells = t.cells();
    double last = 0.0;
    bool first = true;
    for (double x = 0.0; x <= 6.0; x += 0.01) {
        const double ahead = x + 0.3;
        const double feet = std::max(t.height(int(std::floor(x)), 0), t.height(int(std::floor(ahead)), 0));
        auto drawn = groundSurfaceY(cells, glm::dvec3(x, feet, 0.5));
        ASSERT_TRUE(drawn.has_value()) << x;
        EXPECT_NEAR(*drawn, 10.0 + x - 0.5, 1e-4) << x;
        if (!first) EXPECT_LT(std::abs(*drawn - last), 0.02) << x;
        last = *drawn;
        first = false;
    }
}

TEST(GroundSmoothing, NoSurfaceOnBuiltFloors) {
    Terrain t{[](int, int) { return 5; },
              [](const BlockCoord& p) { return p.y == 4 && p.x >= 0 ? GroundCell::Solid : GroundCell::Open; }};
    EXPECT_FALSE(groundSurfaceY(t.cells(), glm::dvec3(0.5, 5.0, 0.5)).has_value());
    EXPECT_TRUE(groundSurfaceY(t.cells(), glm::dvec3(-0.5, 5.0, 0.5)).has_value());
}

// A body on the ground is drawn on the slope; one jumping is drawn where it is
TEST(GroundSmoothing, RenderStateFollowsGroundOnlyOnTheGround) {
    Terrain t{[](int x, int) { return x < 0 ? 5 : 6; }, nullptr};
    auto cells = t.cells();
    EntityRenderState state;
    state.prevPosition = state.currentPosition = glm::dvec3(-0.2, 5.0, 0.5);
    state.onGround = true;
    for (int i = 0; i < 60; ++i) state.followGround(cells, 1.0f, 1.0f / 60.0f);
    const double surface = *groundSurfaceY(cells, state.currentPosition);
    EXPECT_GT(surface, 5.2);
    EXPECT_NEAR(state.currentPosition.y + state.groundLift, surface, 1e-3);

    state.onGround = false;
    state.velocity = glm::dvec3(0.0, 6.0, 0.0);
    for (int i = 0; i < 60; ++i) state.followGround(cells, 1.0f, 1.0f / 60.0f);
    EXPECT_NEAR(state.groundLift, 0.0f, 1e-3f);

    state.followGround(nullptr, 1.0f, 1.0f / 60.0f);
    EXPECT_EQ(state.groundLift, 0.0f);
}

// ============================================================================
// The mesh: no holes, whatever the ground
// ============================================================================

namespace {

struct Hit {
    float t = 1e30f;
    bool front = false;
    bool any = false;
    glm::vec3 normal{0.0f};
};

// Nearest triangle along a ray, and whether it faces the ray (FACE_DATA winding: (c-a)x(b-a) is out)
Hit castRay(const MeshData& mesh, glm::vec3 origin, glm::vec3 dir) {
    Hit best;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
        const glm::vec3 e1 = b - a, e2 = c - a;
        const glm::vec3 p = glm::cross(dir, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 1e-9f) continue;
        const float inv = 1.0f / det;
        const glm::vec3 s = origin - a;
        const float u = glm::dot(s, p) * inv;
        if (u < -1e-5f || u > 1.0f + 1e-5f) continue;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(dir, q) * inv;
        if (v < -1e-5f || u + v > 1.0f + 1e-5f) continue;
        const float t = glm::dot(e2, q) * inv;
        if (t <= 1e-4f || t >= best.t) continue;
        const glm::vec3 out = glm::cross(c - a, b - a);
        best = {t, glm::dot(out, dir) < 0.0f, true, glm::normalize(out)};
    }
    return best;
}

// Every face along a ray, nearest first: t, and the face's outward normal
std::vector<std::pair<float, glm::vec3>> allHits(const MeshData& mesh, glm::vec3 origin, glm::vec3 dir) {
    std::vector<std::pair<float, glm::vec3>> hits;
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const glm::vec3 a = mesh.vertices[mesh.indices[i]].position;
        const glm::vec3 b = mesh.vertices[mesh.indices[i + 1]].position;
        const glm::vec3 c = mesh.vertices[mesh.indices[i + 2]].position;
        const glm::vec3 e1 = b - a, e2 = c - a;
        const glm::vec3 p = glm::cross(dir, e2);
        const float det = glm::dot(e1, p);
        if (std::abs(det) < 1e-9f) continue;
        const float inv = 1.0f / det;
        const glm::vec3 s = origin - a;
        const float u = glm::dot(s, p) * inv;
        if (u < -1e-5f || u > 1.0f + 1e-5f) continue;
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(dir, q) * inv;
        if (v < -1e-5f || u + v > 1.0f + 1e-5f) continue;
        const float t = glm::dot(e2, q) * inv;
        if (t > 1e-4f) hits.emplace_back(t, glm::normalize(glm::cross(c - a, b - a)));
    }
    std::sort(hits.begin(), hits.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    return hits;
}

// Where a ray leaves the subchunk's columns
float exitDistance(glm::vec3 origin, glm::vec3 dir) {
    float t = 1e30f;
    for (int axis : {0, 2}) {
        if (dir[axis] > 1e-6f) t = std::min(t, (16.0f - origin[axis]) / dir[axis]);
        if (dir[axis] < -1e-6f) t = std::min(t, (0.0f - origin[axis]) / dir[axis]);
    }
    return t;
}

}  // namespace

// Random hills, cliffs, overhangs, built posts and ponds. From anywhere in open air, whatever a ray
// meets first is the outside of something: a hole would show it the inside of a face.
TEST(GroundSmoothing, MeshHasNoHolesOnRandomGround) {
    const BlockTypeId ground = BlockTypeId::fromName("test:smooth_ground");
    const BlockTypeId post = BlockTypeId::fromName("test:built_post");
    // SMOOTH_SEEDS=first-last runs other worlds, for a longer search
    uint32_t first = 1, last = 24;
    if (const char* seeds = std::getenv("SMOOTH_SEEDS")) std::sscanf(seeds, "%u-%u", &first, &last);
    int rays = 0, holes = 0;
    for (uint32_t seed = first; seed <= last; ++seed) {
        std::mt19937 rng(seed);
        // Heights from a few bumps, so there are slopes, single steps and cliffs
        std::vector<int> heights(24 * 24);
        std::uniform_int_distribution<int> step(-2, 2);
        for (int z = 0; z < 24; ++z) {
            for (int x = 0; x < 24; ++x) {
                int h = 5;
                if (x > 0) h = heights[z * 24 + x - 1];
                if (z > 0) h = (h + heights[(z - 1) * 24 + x] + 1) / 2;
                int d = step(rng);
                if (std::abs(d) == 2 && rng() % 3 != 0) d /= 2;
                heights[z * 24 + x] = std::clamp(h + d, 1, 8);
            }
        }
        std::vector<BlockCoord> posts, overhang;
        for (int i = 0; i < 6; ++i) {
            int x = int(rng() % 16), z = int(rng() % 16);
            int h = heights[(z + 4) * 24 + x + 4];
            posts.emplace_back(x, h, z);
        }
        const int overhangX = int(rng() % 8), overhangZ = int(rng() % 8);
        auto heightAt = [&](int x, int z) {
            x = std::clamp(x + 4, 0, 23);
            z = std::clamp(z + 4, 0, 23);
            return heights[z * 24 + x];
        };
        // A shelf of ground hanging over part of it, its underside stepping
        auto shelf = [&](int x, int z) {
            if (x < overhangX || x >= overhangX + 7 || z < overhangZ || z >= overhangZ + 6) return 99;
            return 11 + int((x * 7 + z * 3 + seed) % 3);
        };
        Terrain t{heightAt, [&](const BlockCoord& p) {
                      for (const auto& q : posts) {
                          if (p.x == q.x && p.z == q.z && p.y >= q.y && p.y < q.y + 2) return GroundCell::Solid;
                      }
                      if (p.y >= shelf(p.x, p.z) && p.y < 14) return GroundCell::Ground;
                      // Water in the hollows, every other world
                      if (seed % 2 == 1 && p.y < 4 && p.y >= heightAt(p.x, p.z)) return GroundCell::Wet;
                      return GroundCell::Open;
                  }};
        auto cells = t.cells();

        SubChunk sub;
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    GroundCell c = t.at(BlockCoord(x, y, z));
                    if (c == GroundCell::Ground) sub.setBlock(x, y, z, ground);
                    if (c == GroundCell::Solid) sub.setBlock(x, y, z, post);
                }
            }
        }
        BlockOpaqueProvider opaque = [&](const BlockCoord& p) {
            GroundCell c = t.at(p);
            return c == GroundCell::Ground || c == GroundCell::Solid;
        };
        MeshBuilder builder;
        builder.setGreedyMeshing(seed % 2 == 0);
        builder.setGroundSmoothing(cells);
        MeshData mesh = builder.buildSubChunkMesh(sub, ChunkPos(0, 0, 0), opaque,
                                                  [](BlockTypeId, Face) { return glm::vec4(0, 0, 1, 1); });
        ASSERT_FALSE(mesh.isEmpty());

        // Start in air with nothing solid within a cell, so off every surface, curved ones too
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        int cast = 0;
        for (int tries = 0; tries < 20000 && cast < 400; ++tries) {
            glm::vec3 origin(1.0f + 14.0f * unit(rng), 1.0f + 14.0f * unit(rng), 1.0f + 14.0f * unit(rng));
            bool clear = true;
            for (int dx = -1; dx <= 1 && clear; ++dx) {
                for (int dy = -1; dy <= 1 && clear; ++dy) {
                    for (int dz = -1; dz <= 1 && clear; ++dz) {
                        BlockCoord c(int(std::floor(origin.x)) + dx, int(std::floor(origin.y)) + dy,
                                     int(std::floor(origin.z)) + dz);
                        clear = t.at(c) == GroundCell::Open || t.at(c) == GroundCell::Wet;
                    }
                }
            }
            if (!clear) continue;
            glm::vec3 dir(unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f, unit(rng) * 2.0f - 1.0f);
            if (glm::dot(dir, dir) < 0.01f) continue;
            dir = glm::normalize(dir);
            Hit hit = castRay(mesh, origin, dir);
            float leaves = exitDistance(origin, dir);
            if (!hit.any || hit.t > leaves) continue;
            ++cast;
            if (!hit.front) {
                ++holes;
                // Where it got in: the first solid cell along it, and the heights around
                for (float d = 0.0f; d < hit.t; d += 0.01f) {
                    glm::vec3 at = origin + dir * d;
                    BlockCoord c(int(std::floor(at.x)), int(std::floor(at.y)), int(std::floor(at.z)));
                    GroundCell k = t.at(c);
                    if (k == GroundCell::Ground || k == GroundCell::Solid) {
                        std::cout << "first solid cell " << c.x << "," << c.y << "," << c.z << " ("
                                  << (k == GroundCell::Solid ? "built" : "ground") << ") at " << at.x << ","
                                  << at.y << "," << at.z << "\n";
                        for (int dz = -1; dz <= 1; ++dz) {
                            std::cout << "  z" << c.z + dz << ":";
                            for (int dx = -1; dx <= 1; ++dx) {
                                std::cout << " " << heightAt(c.x + dx, c.z + dz);
                                for (const auto& q : posts) {
                                    if (q.x == c.x + dx && q.z == c.z + dz) std::cout << "P" << q.y;
                                }
                            }
                            std::cout << "\n";
                        }
                        break;
                    }
                }
                // SMOOTH_DEBUG=1 lists every face the ray goes through
                if (std::getenv("SMOOTH_DEBUG")) {
                    for (const auto& [ht, n] : allHits(mesh, origin, dir)) {
                        glm::vec3 at = origin + dir * ht;
                        std::cout << "  hit t " << ht << " at " << at.x << "," << at.y << "," << at.z << " n " << n.x
                                  << "," << n.y << "," << n.z << "\n";
                    }
                }
                ADD_FAILURE() << "seed " << seed << ": from (" << origin.x << ", " << origin.y << ", " << origin.z
                              << ") along (" << dir.x << ", " << dir.y << ", " << dir.z << ") the first face, "
                              << hit.t << " away, faces away: at " << origin.x + dir.x * hit.t << ", "
                              << origin.y + dir.y * hit.t << ", " << origin.z + dir.z * hit.t << " facing "
                              << hit.normal.x << ", " << hit.normal.y << ", " << hit.normal.z;
            }
        }
        rays += cast;
    }
    EXPECT_GT(rays, int(last - first + 1) * 300);
    EXPECT_EQ(holes, 0);
}

// A curved face is drawn bent, and an unchanged one as the cube face it was
TEST(GroundSmoothing, MeshBendsOnlyWhatCurves) {
    const BlockTypeId ground = BlockTypeId::fromName("test:smooth_ground");
    Terrain t{[](int x, int) { return x < 8 ? 5 : 6; }, nullptr};
    SubChunk sub;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if (y < t.height(x, z)) sub.setBlock(x, y, z, ground);
            }
        }
    }
    BlockOpaqueProvider opaque = [&](const BlockCoord& p) { return t.at(p) == GroundCell::Ground; };
    auto texture = [](BlockTypeId, Face) { return glm::vec4(0, 0, 1, 1); };
    MeshBuilder plain;
    MeshData cubes = plain.buildSubChunkMesh(sub, ChunkPos(0, 0, 0), opaque, texture);
    MeshBuilder smooth;
    smooth.setGroundSmoothing(t.cells());
    MeshData bent = smooth.buildSubChunkMesh(sub, ChunkPos(0, 0, 0), opaque, texture);

    float lowest = 99.0f, highest = -99.0f;
    bool sloped = false;
    for (const auto& v : bent.vertices) {
        if (v.normal.y > 0.0f && v.normal.y < 0.99f) sloped = true;
        if (v.position.x > 6.9f && v.position.x < 9.1f && v.normal.y > 0.0f) {
            lowest = std::min(lowest, v.position.y);
            highest = std::max(highest, v.position.y);
        }
    }
    EXPECT_TRUE(sloped);
    EXPECT_NEAR(lowest, 5.0f, 1e-4f);
    EXPECT_NEAR(highest, 6.0f, 1e-4f);
    // Nothing of the riser stands out: the step is all slope
    for (const auto& v : bent.vertices) {
        if (std::abs(v.normal.x) > 0.99f && v.position.x > 0.5f && v.position.x < 15.5f) {
            ADD_FAILURE() << "a wall at x " << v.position.x << " y " << v.position.y;
            break;
        }
    }
    // Away from the step it's the same flat ground as before, merged as before
    EXPECT_GT(bent.triangleCount(), cubes.triangleCount());
    EXPECT_LT(bent.triangleCount(), cubes.triangleCount() + 16 * 2 * 9 + 64);
}

// Ground level with something built beside it keeps its edge there level, but its corners can dip.
// What shows down to them is the built thing's side, and it's drawn as that, not as the ground's.
TEST(GroundSmoothing, ABuiltBlocksSideShowsWhereGroundBesideItDips) {
    const BlockTypeId ground = BlockTypeId::fromName("test:smooth_ground");
    const BlockTypeId post = BlockTypeId::fromName("test:built_post");
    const glm::vec4 groundTile(0.0f, 0.0f, 0.25f, 0.25f), postTile(0.5f, 0.5f, 0.75f, 0.75f);
    // Ground up to y = 5, one block of it a step higher at (8, 5, 7), against a post at (8, 5, 8)
    std::map<BlockCoord, BlockTypeId> blocks;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y < 5; ++y) blocks[BlockCoord(x, y, z)] = ground;
        }
    }
    blocks[BlockCoord(8, 5, 7)] = ground;
    blocks[BlockCoord(8, 5, 8)] = post;
    auto at = [&](const BlockCoord& p) {
        auto it = blocks.find(p);
        return it == blocks.end() ? AIR_BLOCK_TYPE : it->second;
    };
    Terrain t{[](int, int) { return 0; }, [&](const BlockCoord& p) {
                  const BlockTypeId b = at(p);
                  return b == ground ? GroundCell::Ground : b == post ? GroundCell::Solid : GroundCell::Open;
              }};
    const auto raised = groundPatch(t.cells(), BlockCoord(8, 5, 7), 1);
    ASSERT_TRUE(raised.has_value());
    ASSERT_EQ(raised->edge[GROUND_EDGE_Z_HIGH], GroundEdge::Pin);
    ASSERT_LT(raised->edgeHeight(GROUND_EDGE_Z_HIGH, 0.0f), 0.0f) << "no dip by the post, so this checks nothing";

    SubChunk sub;
    for (const auto& [p, b] : blocks) sub.setBlock(p.x, p.y, p.z, b);
    MeshBuilder builder;
    builder.setGroundSmoothing(t.cells(), at);
    BlockOpaqueProvider opaque = [&](const BlockCoord& p) { return !at(p).isAir(); };
    MeshData mesh = builder.buildSubChunkMesh(sub, ChunkPos(0, 0, 0), opaque, [&](BlockTypeId b, Face) {
        return b == post ? postTile : groundTile;
    });

    // Facing back over the ground, in the plane of the post's near side, below its top
    int postSide = 0, groundSide = 0;
    for (const ChunkVertex& v : mesh.vertices) {
        if (v.normal != glm::vec3(0, 0, -1) || std::abs(v.position.z - 8.0f) > 1e-4f) continue;
        if (v.position.x < 8.0f || v.position.x > 9.0f || v.position.y < 5.0f || v.position.y > 6.0f) continue;
        postSide += v.tileBounds == postTile;
        groundSide += v.tileBounds == groundTile;
    }
    EXPECT_GT(postSide, 0) << "nothing drawn where the ground dips below the post";
    EXPECT_EQ(groundSide, 0) << "the post's side is drawn as the ground's";
}
