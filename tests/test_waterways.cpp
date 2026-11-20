#include <gtest/gtest.h>
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"
#include "finevox/worldgen/biome_map.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/world.hpp"

#include <cstdlib>
#include <optional>

using namespace finevox;
using namespace finevox::worldgen;

namespace {

constexpr int32_t SEA = 58;
constexpr int32_t SPAN = 480;  // blocks either way swept

WaterwaySettings riversAndLakes() {
    WaterwaySettings w;
    w.riverScale = 0.0028f;
    w.riverWidth = 7.0f;
    w.riverDepth = 3;
    w.riverMarshes = -0.45f;
    w.marshWidth = 3.0f;
    w.riverFords = 0.45f;
    w.lakeScale = 0.004f;
    w.lakeThreshold = 0.5f;
    w.lakeDepth = 5;
    w.bankSlope = 0.35f;
    return w;
}

}  // namespace

TEST(WaterwaysTest, WaterIsNeverDeeperThanItsSettingsAndCrossingsAreABlockDeep) {
    // Rivers alone: where one runs into a lake, the lake is as deep as it is
    WaterwaySettings rivers = riversAndLakes();
    rivers.lakeScale = 0.0f;
    for (uint64_t seed : {1ull, 4ull, 20261030ull}) {
        Waterways ways(seed, SEA, rivers);
        Waterways withLakes(seed, SEA, riversAndLakes());
        int wet = 0, marsh = 0, ford = 0, tussocks = 0;
        for (int32_t x = -SPAN; x <= SPAN; x += 2) {
            for (int32_t z = -SPAN; z <= SPAN; z += 2) {
                const float fx = static_cast<float>(x), fz = static_cast<float>(z);
                std::optional<int32_t> c = ways.ceilingAt(fx, fz);
                ASSERT_TRUE(c.has_value());
                const bool inMarsh = ways.marshAt(fx, fz);
                const bool inFord = ways.fordAt(fx, fz);
                ASSERT_FALSE(inMarsh && inFord) << "a marsh and a ford at " << x << "," << z;
                ASSERT_GE(*c, SEA - 3) << "deeper than a river at " << x << "," << z;
                std::optional<int32_t> either = withLakes.ceilingAt(fx, fz);
                ASSERT_TRUE(either.has_value());
                if (*either < SEA) {
                    ++wet;
                    ASSERT_GE(*either, SEA - 5) << "deeper than any river or lake at " << x << "," << z;
                }
                if (inMarsh) {
                    ++marsh;
                    ASSERT_GE(*c, SEA - 1) << "a marsh too deep to wade at " << x << "," << z;
                    if (*c == SEA) ++tussocks;
                }
                if (inFord) {
                    ++ford;
                    ASSERT_EQ(*c, SEA - 1) << "a ford not a block deep at " << x << "," << z;
                }
            }
        }
        const int all = (SPAN + 1) * (SPAN + 1);
        EXPECT_GT(wet, all / 50) << "seed " << seed << ": hardly any water";
        EXPECT_LT(wet, all / 4) << "seed " << seed << ": mostly water";
        EXPECT_GT(marsh, 0) << "seed " << seed;
        EXPECT_GT(ford, 0) << "seed " << seed;
        EXPECT_GT(tussocks, 0) << "seed " << seed;
        EXPECT_LT(tussocks, marsh) << "seed " << seed;
    }
}

TEST(WaterwaysTest, BanksRiseGently) {
    // The ceiling between neighbouring blocks on land changes by about the bank slope. The
    // distance to water is an estimate, so this allows a block over.
    Waterways ways(7, SEA, riversAndLakes());
    int steep = 0, compared = 0;
    for (int32_t x = -SPAN; x < SPAN; ++x) {
        for (int32_t z = -SPAN; z < SPAN; z += 7) {
            int32_t a = *ways.ceilingAt(static_cast<float>(x), static_cast<float>(z));
            int32_t b = *ways.ceilingAt(static_cast<float>(x + 1), static_cast<float>(z));
            if (a < SEA || b < SEA || a > SEA + 20 || b > SEA + 20) continue;
            ++compared;
            if (std::abs(a - b) > 2) ++steep;
        }
    }
    ASSERT_GT(compared, 1000);
    EXPECT_LT(steep * 200, compared) << steep << " of " << compared << " steps are cliffs";
}

TEST(WaterwaysTest, OnlyRiversOrOnlyLakes) {
    WaterwaySettings rivers = riversAndLakes();
    rivers.lakeScale = 0.0f;
    WaterwaySettings lakes = riversAndLakes();
    lakes.riverScale = 0.0f;
    Waterways r(3, SEA, rivers), l(3, SEA, lakes), both(3, SEA, riversAndLakes());
    for (int32_t x = -SPAN; x <= SPAN; x += 5) {
        for (int32_t z = -SPAN; z <= SPAN; z += 5) {
            const float fx = static_cast<float>(x), fz = static_cast<float>(z);
            EXPECT_FALSE(l.marshAt(fx, fz));
            EXPECT_FALSE(l.fordAt(fx, fz));
            // Together, the land is as low as the lower of the two
            EXPECT_EQ(*both.ceilingAt(fx, fz), std::min(*r.ceilingAt(fx, fz), *l.ceilingAt(fx, fz)));
        }
    }
    EXPECT_FALSE(WaterwaySettings{}.any());
}

TEST(WaterwaysTest, PassCutsColumnsDownToTheCeilingAndNoFurther) {
    const int32_t TOP = 80;
    World world;
    BlockTypeId stone = BlockTypeId::fromName("stone");
    auto ways = std::make_shared<const Waterways>(4, SEA, riversAndLakes());
    BiomeMap biomeMap(4, BiomeRegistry::global());
    int cut = 0;
    for (int32_t cx = -20; cx <= 20; cx += 4) {
        for (int32_t cz = -20; cz <= 20; cz += 4) {
            ChunkColumn& col = world.getOrCreateColumn({cx, cz});
            for (int32_t lx = 0; lx < 16; ++lx) {
                for (int32_t lz = 0; lz < 16; ++lz) {
                    for (int32_t y = 0; y <= TOP; ++y) col.setBlock(lx, y, lz, stone);
                }
            }
            GenerationContext ctx{col, {cx, cz}, world, biomeMap, 4};
            for (auto& h : ctx.heightmap) h = TOP;
            WaterwayPass pass(ways);
            pass.generate(ctx);
            for (int32_t lx = 0; lx < 16; ++lx) {
                for (int32_t lz = 0; lz < 16; ++lz) {
                    int32_t want = std::min(TOP, *ways->ceilingAt(static_cast<float>(cx * 16 + lx),
                                                                  static_cast<float>(cz * 16 + lz)));
                    want = std::max(want, 1);
                    if (want < TOP) ++cut;
                    int32_t h = ctx.heightmap[GenerationContext::hmIndex(lx, lz)];
                    ASSERT_EQ(h, want);
                    EXPECT_EQ(col.getBlock(lx, h, lz), stone);
                    EXPECT_TRUE(col.getBlock(lx, h + 1, lz).isAir());
                    EXPECT_TRUE(col.getBlock(lx, TOP, lz).isAir() || h == TOP);
                }
            }
        }
    }
    EXPECT_GT(cut, 0);
}
