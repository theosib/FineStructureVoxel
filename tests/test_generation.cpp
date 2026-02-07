/**
 * @file test_generation.cpp
 * @brief Unit tests for the generation pipeline
 */

#include <gtest/gtest.h>

#include "finevox/world_generator.hpp"
#include "finevox/generation_passes.hpp"
#include "finevox/biome.hpp"
#include "finevox/biome_map.hpp"
#include "finevox/feature_registry.hpp"
#include "finevox/feature_tree.hpp"
#include "finevox/feature_ore.hpp"
#include "finevox/world.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/block_type.hpp"

#include <memory>

namespace finevox {
namespace {

// ============================================================================
// Test fixture
// ============================================================================

class GenerationTest : public ::testing::Test {
protected:
    void SetUp() override {
        BiomeRegistry::global().clear();
        FeatureRegistry::global().clear();

        stoneId_ = BlockTypeId::fromName("stone");
        dirtId_ = BlockTypeId::fromName("dirt");
        grassId_ = BlockTypeId::fromName("grass");
        sandId_ = BlockTypeId::fromName("sand");
        oakLogId_ = BlockTypeId::fromName("oak_log");
        oakLeavesId_ = BlockTypeId::fromName("oak_leaves");
        ironOreId_ = BlockTypeId::fromName("iron_ore");

        auto& reg = BlockRegistry::global();
        reg.registerType(stoneId_, BlockType().setOpaque(true));
        reg.registerType(dirtId_, BlockType().setOpaque(true));
        reg.registerType(grassId_, BlockType().setOpaque(true));
        reg.registerType(sandId_, BlockType().setOpaque(true));
        reg.registerType(oakLogId_, BlockType().setOpaque(true));
        reg.registerType(oakLeavesId_, BlockType().setOpaque(true));
        reg.registerType(ironOreId_, BlockType().setOpaque(true));

        // Register biomes
        BiomeProperties plains;
        plains.displayName = "Plains";
        plains.temperatureMin = 0.3f;
        plains.temperatureMax = 0.7f;
        plains.humidityMin = 0.2f;
        plains.humidityMax = 0.6f;
        plains.baseHeight = 64.0f;
        plains.heightVariation = 8.0f;
        plains.surfaceBlock = "grass";
        plains.fillerBlock = "dirt";
        plains.fillerDepth = 3;
        plains.treeDensity = 0.005f;
        BiomeRegistry::global().registerBiome("plains", plains);

        BiomeProperties desert;
        desert.displayName = "Desert";
        desert.temperatureMin = 0.7f;
        desert.temperatureMax = 1.0f;
        desert.humidityMin = 0.0f;
        desert.humidityMax = 0.3f;
        desert.baseHeight = 62.0f;
        desert.heightVariation = 4.0f;
        desert.surfaceBlock = "sand";
        desert.fillerBlock = "sand";
        desert.fillerDepth = 5;
        BiomeRegistry::global().registerBiome("desert", desert);
    }

    void TearDown() override {
        BiomeRegistry::global().clear();
        FeatureRegistry::global().clear();
    }

    BlockTypeId stoneId_, dirtId_, grassId_, sandId_;
    BlockTypeId oakLogId_, oakLeavesId_, ironOreId_;
};

// ============================================================================
// GenerationContext Tests
// ============================================================================

TEST_F(GenerationTest, ColumnSeedDeterministic) {
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(5, 10));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx1{col, col.position(), world, biomeMap, 42, {}, {}};
    GenerationContext ctx2{col, col.position(), world, biomeMap, 42, {}, {}};

    EXPECT_EQ(ctx1.columnSeed(), ctx2.columnSeed());
}

TEST_F(GenerationTest, DifferentColumnsDifferentSeeds) {
    World world;
    auto& col1 = world.getOrCreateColumn(ColumnPos(0, 0));
    auto& col2 = world.getOrCreateColumn(ColumnPos(1, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx1{col1, col1.position(), world, biomeMap, 42, {}, {}};
    GenerationContext ctx2{col2, col2.position(), world, biomeMap, 42, {}, {}};

    EXPECT_NE(ctx1.columnSeed(), ctx2.columnSeed());
}

// ============================================================================
// GenerationPipeline Tests
// ============================================================================

class CustomPass : public GenerationPass {
public:
    CustomPass(std::string name, int32_t prio, bool* ran = nullptr)
        : name_(std::move(name)), priority_(prio), ran_(ran) {}

    std::string_view name() const override { return name_; }
    int32_t priority() const override { return priority_; }
    void generate(GenerationContext&) override {
        if (ran_) *ran_ = true;
    }

private:
    std::string name_;
    int32_t priority_;
    bool* ran_;
};

TEST_F(GenerationTest, PipelineAddAndCount) {
    GenerationPipeline pipeline;
    EXPECT_EQ(pipeline.passCount(), 0u);

    pipeline.addPass(std::make_unique<CustomPass>("a", 1000));
    EXPECT_EQ(pipeline.passCount(), 1u);

    pipeline.addPass(std::make_unique<CustomPass>("b", 2000));
    EXPECT_EQ(pipeline.passCount(), 2u);
}

TEST_F(GenerationTest, PipelineRemovePass) {
    GenerationPipeline pipeline;
    pipeline.addPass(std::make_unique<CustomPass>("a", 1000));
    pipeline.addPass(std::make_unique<CustomPass>("b", 2000));

    EXPECT_TRUE(pipeline.removePass("a"));
    EXPECT_EQ(pipeline.passCount(), 1u);
    EXPECT_EQ(pipeline.getPass("a"), nullptr);
    EXPECT_NE(pipeline.getPass("b"), nullptr);

    EXPECT_FALSE(pipeline.removePass("nonexistent"));
}

TEST_F(GenerationTest, PipelineReplacePass) {
    GenerationPipeline pipeline;
    bool ran1 = false, ran2 = false;
    pipeline.addPass(std::make_unique<CustomPass>("test", 1000, &ran1));

    // Replace with different instance
    pipeline.addPass(std::make_unique<CustomPass>("other", 2000));
    EXPECT_TRUE(pipeline.replacePass(std::make_unique<CustomPass>("test", 1500, &ran2)));
    EXPECT_EQ(pipeline.passCount(), 2u);

    // Run and verify the replacement ran, not the original
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());
    pipeline.setWorldSeed(42);
    pipeline.generateColumn(col, world, biomeMap);

    EXPECT_FALSE(ran1);
    EXPECT_TRUE(ran2);
}

TEST_F(GenerationTest, PipelineRunsInPriorityOrder) {
    GenerationPipeline pipeline;
    std::vector<int> order;

    class OrderPass : public GenerationPass {
    public:
        OrderPass(std::string name, int32_t prio, std::vector<int>& order, int id)
            : name_(std::move(name)), priority_(prio), order_(order), id_(id) {}
        std::string_view name() const override { return name_; }
        int32_t priority() const override { return priority_; }
        void generate(GenerationContext&) override { order_.push_back(id_); }
    private:
        std::string name_;
        int32_t priority_;
        std::vector<int>& order_;
        int id_;
    };

    // Add out of order
    pipeline.addPass(std::make_unique<OrderPass>("c", 3000, order, 3));
    pipeline.addPass(std::make_unique<OrderPass>("a", 1000, order, 1));
    pipeline.addPass(std::make_unique<OrderPass>("b", 2000, order, 2));

    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());
    pipeline.setWorldSeed(42);
    pipeline.generateColumn(col, world, biomeMap);

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

// ============================================================================
// TerrainPass Tests
// ============================================================================

TEST_F(GenerationTest, TerrainPassFillsStone) {
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx{col, col.position(), world, biomeMap, 42, {}, {}};

    TerrainPass terrain(42);
    terrain.generate(ctx);

    // Every column should have stone from y=0 to some surface height
    bool hasStone = false;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            if (col.getBlock(lx, 0, lz) == stoneId_) {
                hasStone = true;
            }
            // Heightmap should be set
            int32_t idx = GenerationContext::hmIndex(lx, lz);
            EXPECT_GT(ctx.heightmap[idx], 0) << "No height at (" << lx << "," << lz << ")";
        }
    }
    EXPECT_TRUE(hasStone);
}

TEST_F(GenerationTest, TerrainPassPopulatesBiomes) {
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx{col, col.position(), world, biomeMap, 42, {}, {}};

    TerrainPass terrain(42);
    terrain.generate(ctx);

    // Each biome entry should be a valid registered biome
    for (int32_t i = 0; i < 256; ++i) {
        EXPECT_NE(BiomeRegistry::global().getBiome(ctx.biomes[i]), nullptr)
            << "Invalid biome at index " << i;
    }
}

TEST_F(GenerationTest, TerrainPassDeterministic) {
    World world1, world2;
    auto& col1 = world1.getOrCreateColumn(ColumnPos(3, 7));
    auto& col2 = world2.getOrCreateColumn(ColumnPos(3, 7));
    BiomeMap biomeMap1(42, BiomeRegistry::global());
    BiomeMap biomeMap2(42, BiomeRegistry::global());

    GenerationContext ctx1{col1, col1.position(), world1, biomeMap1, 42, {}, {}};
    GenerationContext ctx2{col2, col2.position(), world2, biomeMap2, 42, {}, {}};

    TerrainPass terrain1(42);
    TerrainPass terrain2(42);
    terrain1.generate(ctx1);
    terrain2.generate(ctx2);

    for (int32_t i = 0; i < 256; ++i) {
        EXPECT_EQ(ctx1.heightmap[i], ctx2.heightmap[i]) << "Height mismatch at " << i;
    }
}

// ============================================================================
// SurfacePass Tests
// ============================================================================

TEST_F(GenerationTest, SurfacePassAppliesBiomeBlocks) {
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx{col, col.position(), world, biomeMap, 42, {}, {}};

    // Run terrain first
    TerrainPass terrain(42);
    terrain.generate(ctx);

    // Run surface
    SurfacePass surface;
    surface.generate(ctx);

    // Check that surface has biome-appropriate blocks (not just stone)
    bool foundNonStone = false;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            int32_t idx = GenerationContext::hmIndex(lx, lz);
            int32_t surfaceY = ctx.heightmap[idx];
            BlockTypeId topBlock = col.getBlock(lx, surfaceY, lz);
            if (topBlock != stoneId_) {
                foundNonStone = true;
            }
        }
    }
    EXPECT_TRUE(foundNonStone);
}

// ============================================================================
// CavePass Tests
// ============================================================================

TEST_F(GenerationTest, CavePassCarves) {
    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    BiomeMap biomeMap(42, BiomeRegistry::global());

    GenerationContext ctx{col, col.position(), world, biomeMap, 42, {}, {}};

    TerrainPass terrain(42);
    terrain.generate(ctx);

    // Count stone blocks before caves
    int stoneBefore = 0;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 1; y < 60; ++y) {
                if (col.getBlock(lx, y, lz) == stoneId_) ++stoneBefore;
            }
        }
    }

    CavePass caves(42);
    caves.generate(ctx);

    // Count stone blocks after caves
    int stoneAfter = 0;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 1; y < 60; ++y) {
                if (col.getBlock(lx, y, lz) == stoneId_) ++stoneAfter;
            }
        }
    }

    // Caves should have removed some stone
    EXPECT_LT(stoneAfter, stoneBefore);
}

// ============================================================================
// Full Pipeline Tests
// ============================================================================

TEST_F(GenerationTest, FullPipelineProducesPlayableColumn) {
    uint64_t seed = 42;

    // Register a tree feature
    TreeConfig treeConfig;
    treeConfig.trunkBlock = oakLogId_;
    treeConfig.leavesBlock = oakLeavesId_;
    treeConfig.minTrunkHeight = 4;
    treeConfig.maxTrunkHeight = 6;
    treeConfig.requiresSoil = true;
    FeatureRegistry::global().registerFeature(
        std::make_shared<TreeFeature>("oak_tree", treeConfig));

    FeaturePlacement treePlacement;
    treePlacement.featureName = "oak_tree";
    treePlacement.density = 0.02f;
    treePlacement.requiresSurface = true;
    FeatureRegistry::global().addPlacement(treePlacement);

    // Register ore
    OreConfig oreConfig;
    oreConfig.oreBlock = ironOreId_;
    oreConfig.replaceBlock = stoneId_;
    oreConfig.veinSize = 8;
    oreConfig.minHeight = 0;
    oreConfig.maxHeight = 48;
    oreConfig.veinsPerChunk = 8;
    FeatureRegistry::global().registerFeature(
        std::make_shared<OreFeature>("iron_ore", oreConfig));

    FeaturePlacement orePlacement;
    orePlacement.featureName = "iron_ore";
    orePlacement.density = 0.03f;
    orePlacement.minHeight = 0;
    orePlacement.maxHeight = 48;
    FeatureRegistry::global().addPlacement(orePlacement);

    // Build pipeline
    GenerationPipeline pipeline;
    pipeline.setWorldSeed(seed);
    pipeline.addPass(std::make_unique<TerrainPass>(seed));
    pipeline.addPass(std::make_unique<SurfacePass>());
    pipeline.addPass(std::make_unique<CavePass>(seed));
    pipeline.addPass(std::make_unique<OrePass>());
    pipeline.addPass(std::make_unique<StructurePass>());
    pipeline.addPass(std::make_unique<DecorationPass>());

    EXPECT_EQ(pipeline.passCount(), 6u);

    // Generate a column
    World world;
    BiomeMap biomeMap(seed, BiomeRegistry::global());
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    pipeline.generateColumn(col, world, biomeMap);

    // Verify: column has blocks
    EXPECT_GT(col.nonAirCount(), 0);

    // Verify: y=0 has solid blocks
    bool hasY0Solid = false;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            if (!col.getBlock(lx, 0, lz).isAir()) {
                hasY0Solid = true;
                break;
            }
        }
        if (hasY0Solid) break;
    }
    EXPECT_TRUE(hasY0Solid);

    // Verify: some surface blocks are not stone (surface pass worked)
    int nonStoneCount = 0;
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            // Check blocks around expected surface height
            for (int32_t y = 55; y <= 75; ++y) {
                BlockTypeId b = col.getBlock(lx, y, lz);
                if (!b.isAir() && b != stoneId_) {
                    ++nonStoneCount;
                }
            }
        }
    }
    EXPECT_GT(nonStoneCount, 0);
}

TEST_F(GenerationTest, FullPipelineDeterministic) {
    uint64_t seed = 12345;

    auto buildPipeline = [&]() {
        GenerationPipeline pipeline;
        pipeline.setWorldSeed(seed);
        pipeline.addPass(std::make_unique<TerrainPass>(seed));
        pipeline.addPass(std::make_unique<SurfacePass>());
        pipeline.addPass(std::make_unique<CavePass>(seed));
        return pipeline;
    };

    World world1, world2;
    BiomeMap biomeMap1(seed, BiomeRegistry::global());
    BiomeMap biomeMap2(seed, BiomeRegistry::global());

    auto& col1 = world1.getOrCreateColumn(ColumnPos(5, 5));
    auto& col2 = world2.getOrCreateColumn(ColumnPos(5, 5));

    auto pipeline1 = buildPipeline();
    auto pipeline2 = buildPipeline();

    pipeline1.generateColumn(col1, world1, biomeMap1);
    pipeline2.generateColumn(col2, world2, biomeMap2);

    // Verify identical results
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 0; y < 80; ++y) {
                EXPECT_EQ(col1.getBlock(lx, y, lz), col2.getBlock(lx, y, lz))
                    << "Mismatch at (" << lx << "," << y << "," << lz << ")";
            }
        }
    }
}

}  // namespace
}  // namespace finevox
