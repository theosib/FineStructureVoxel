// Terrain from a graph of numbers

#include <gtest/gtest.h>

#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/world.hpp"
#include "finevox/worldgen/biome.hpp"
#include "finevox/worldgen/biome_map.hpp"
#include "finevox/worldgen/density_graph.hpp"
#include "finevox/worldgen/generation_passes.hpp"

using namespace finevox;
using namespace finevox::worldgen;

TEST(DensityGraph, ArithmeticAndCurves) {
    auto g = DensityGraph::parse(
        "node:a: const 3\n"
        "node:b: add a 2 y\n"          // 5 + y
        "node:c: curve b 0:0 10:100\n"  // straight between, flat beyond
        "node:d: clamp c 10 40\n"
        "node:e: lerp 0.25 0 8\n"
        "terrain: d\n", 1);
    ASSERT_TRUE(g.has_value());
    EXPECT_FLOAT_EQ(g->evaluate(g->find("b"), 0, 1, 0), 6.0f);
    EXPECT_FLOAT_EQ(g->evaluate(g->find("c"), 0, 0, 0), 50.0f);   // b = 5
    EXPECT_FLOAT_EQ(g->evaluate(g->find("c"), 0, 20, 0), 100.0f); // b = 25, past the last point
    EXPECT_FLOAT_EQ(g->evaluate(g->find("c"), 0, -20, 0), 0.0f);
    EXPECT_FLOAT_EQ(g->evaluate(g->find("d"), 0, -20, 0), 10.0f);
    EXPECT_FLOAT_EQ(g->evaluate(g->find("e"), 0, 0, 0), 2.0f);
    EXPECT_TRUE(g->dependsOnHeight(g->find("b")));
    EXPECT_FALSE(g->dependsOnHeight(g->find("a")));
}

TEST(DensityGraph, SettingsAndErrors) {
    auto g = DensityGraph::parse("fill: rock\nbottom: -32\ntop: 96\ncell: 8 4\nfloor: base 3\n"
                                 "node:g: sub 40 y\nterrain: g\nfield:wet: noise scale=0.01 seed=2\n", 1);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->fillBlock(), "rock");
    EXPECT_EQ(g->bottom(), -32);
    EXPECT_EQ(g->top(), 96);
    EXPECT_EQ(g->cellAcross(), 8);
    EXPECT_EQ(g->cellUp(), 4);
    EXPECT_EQ(g->floorBlock(), "base");
    ASSERT_EQ(g->fields().size(), 1u);
    EXPECT_EQ(g->fields()[0].first, "wet");

    std::string error;
    EXPECT_FALSE(DensityGraph::parse("node:a: add nope 1\nterrain: a\n", 1, &error));
    EXPECT_NE(error.find("nope"), std::string::npos) << error;
    EXPECT_FALSE(DensityGraph::parse("node:a: const 1\n", 1, &error));  // no terrain
    EXPECT_FALSE(DensityGraph::parse("node:a: sub 1 y\nterrain: a\nfield:f: a\n", 1, &error));
    EXPECT_NE(error.find("height"), std::string::npos) << error;  // a field can't vary with height
    EXPECT_FALSE(DensityGraph::parse("cell: 3 8\nnode:a: const 1\nterrain: a\n", 1, &error));
}

TEST(DensityGraph, NoiseIsDeterministicAndSeeded) {
    const char* text = "node:n: noise scale=0.05 octaves=2 seed=4\nterrain: n\n";
    auto a = DensityGraph::parse(text, 7), b = DensityGraph::parse(text, 7), c = DensityGraph::parse(text, 8);
    float va = a->evaluate(0, 13.5f, 0, -7.25f);
    EXPECT_EQ(va, b->evaluate(0, 13.5f, 0, -7.25f));
    EXPECT_NE(va, c->evaluate(0, 13.5f, 0, -7.25f));
}


TEST(DensityTerrainPass, FillsBelowTheSurfaceBetweenItsLimits) {
    World world;
    BlockTypeId rock = BlockTypeId::fromName("dg_rock");
    BlockRegistry::global().registerOrUpdateType(rock, BlockType{});
    ChunkColumn& column = world.getOrCreateColumn(ColumnPos(2, -1));
    // A slope: the ground is at 40 at x = 0 and rises one a block east
    auto graph = std::make_shared<const DensityGraph>(*DensityGraph::parse(
        "fill: dg_rock\nbottom: -16\ntop: 80\nnode:h: add 40 x\nnode:g: sub h y\nterrain: g\n", 3));
    DensityTerrainPass pass(graph);
    BiomeMap biomes(3, BiomeRegistry::global());
    GenerationContext context{column, column.position(), world, biomes, 3, {}, {}};
    GenerationContext* ctx = &context;
    pass.generate(context);
    // Column x = 32..47: ground at 72..87, clipped by the top at 80
    EXPECT_EQ(column.getBlock(0, 71, 0), rock);
    EXPECT_TRUE(column.getBlock(0, 72, 0).isAir());
    EXPECT_EQ(column.getBlock(0, -16, 0), rock);
    EXPECT_TRUE(column.getBlock(0, -17, 0).isAir());  // nothing below the bottom
    EXPECT_EQ(ctx->heightmap[GenerationContext::hmIndex(0, 0)], 71);
    EXPECT_EQ(ctx->heightmap[GenerationContext::hmIndex(15, 0)], 79);  // the top cuts it off
}

TEST(DensityTerrainPass, ChoosesBiomesByFields) {
    auto& registry = BiomeRegistry::global();
    BiomeProperties wet, dry;
    wet.ranges = {{"wet", {0.0f, 1.0f}}};
    dry.ranges = {{"wet", {-1.0f, 0.0f}}};
    registry.registerBiome("dg:wet", wet);
    registry.registerBiome("dg:dry", dry);
    registry.setAllowed({BiomeId::fromName("dg:wet"), BiomeId::fromName("dg:dry")});
    auto graph = std::make_shared<const DensityGraph>(*DensityGraph::parse(
        "node:g: sub 10 y\nterrain: g\nfield:wet: div x 100\n", 1));
    DensityTerrainPass pass(graph);
    std::vector<float> values;
    graph->evaluateColumn(30, 0, values);
    EXPECT_EQ(pass.chooseBiome(values), BiomeId::fromName("dg:wet"));
    graph->evaluateColumn(-30, 0, values);
    EXPECT_EQ(pass.chooseBiome(values), BiomeId::fromName("dg:dry"));
    graph->evaluateColumn(500, 0, values);  // outside both: the nearer
    EXPECT_EQ(pass.chooseBiome(values), BiomeId::fromName("dg:wet"));
    registry.setAllowed({});
}
