/**
 * @file test_feature.cpp
 * @brief Unit tests for Feature system (tree, ore, schematic, registry, loader)
 */

#include <gtest/gtest.h>

#include "finevox/feature.hpp"
#include "finevox/feature_tree.hpp"
#include "finevox/feature_ore.hpp"
#include "finevox/feature_schematic.hpp"
#include "finevox/feature_registry.hpp"
#include "finevox/feature_loader.hpp"
#include "finevox/config_parser.hpp"
#include "finevox/world.hpp"
#include "finevox/block_type.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace finevox {
namespace {

// ============================================================================
// Helper: Create a small test world with some blocks
// ============================================================================

class FeatureTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        FeatureRegistry::global().clear();
        stoneId_ = BlockTypeId::fromName("stone");
        dirtId_ = BlockTypeId::fromName("dirt");
        grassId_ = BlockTypeId::fromName("grass");
        oakLogId_ = BlockTypeId::fromName("oak_log");
        oakLeavesId_ = BlockTypeId::fromName("oak_leaves");
        ironOreId_ = BlockTypeId::fromName("iron_ore");

        auto& reg = BlockRegistry::global();
        reg.registerType(stoneId_, BlockType().setOpaque(true));
        reg.registerType(dirtId_, BlockType().setOpaque(true));
        reg.registerType(grassId_, BlockType().setOpaque(true));
        reg.registerType(oakLogId_, BlockType().setOpaque(true));
        reg.registerType(oakLeavesId_, BlockType().setOpaque(true));
        reg.registerType(ironOreId_, BlockType().setOpaque(true));
    }

    void TearDown() override {
        FeatureRegistry::global().clear();
    }

    /// Create a world with a flat stone+dirt+grass surface at y=63
    std::unique_ptr<World> createFlatWorld() {
        auto world = std::make_unique<World>();
        auto& col = world->getOrCreateColumn(ColumnPos(0, 0));
        for (int32_t x = 0; x < 16; ++x) {
            for (int32_t z = 0; z < 16; ++z) {
                for (int32_t y = 0; y <= 60; ++y) {
                    col.setBlock(x, y, z, stoneId_);
                }
                col.setBlock(x, 61, z, dirtId_);
                col.setBlock(x, 62, z, dirtId_);
                col.setBlock(x, 63, z, grassId_);
            }
        }
        return world;
    }

    BlockTypeId stoneId_, dirtId_, grassId_, oakLogId_, oakLeavesId_, ironOreId_;
};

// ============================================================================
// TreeFeature Tests
// ============================================================================

class TreeFeatureTest : public FeatureTestBase {};

TEST_F(TreeFeatureTest, PlacesTreeOnSoil) {
    auto world = createFlatWorld();

    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    config.minTrunkHeight = 5;
    config.maxTrunkHeight = 5;
    config.leafRadius = 2;
    config.requiresSoil = true;

    TreeFeature tree("oak_tree", config);
    EXPECT_EQ(tree.name(), "oak_tree");

    FeaturePlacementContext ctx{*world, BlockPos(8, 64, 8), BiomeId{}, 42, nullptr};

    auto result = tree.place(ctx);
    EXPECT_EQ(result, FeatureResult::Placed);

    for (int32_t y = 0; y < 5; ++y) {
        EXPECT_EQ(world->getBlock(8, 64 + y, 8), oakLogId_)
            << "Missing trunk at y=" << (64 + y);
    }

    bool foundLeaves = false;
    for (int32_t dx = -2; dx <= 2; ++dx) {
        for (int32_t dz = -2; dz <= 2; ++dz) {
            for (int32_t dy = 3; dy <= 6; ++dy) {
                if (world->getBlock(8 + dx, 64 + dy, 8 + dz) == oakLeavesId_) {
                    foundLeaves = true;
                }
            }
        }
    }
    EXPECT_TRUE(foundLeaves);
}

TEST_F(TreeFeatureTest, SkipsWithoutSoil) {
    World world;

    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    config.requiresSoil = true;

    TreeFeature tree("oak_tree", config);
    FeaturePlacementContext ctx{world, BlockPos(8, 64, 8), BiomeId{}, 42, nullptr};

    EXPECT_EQ(tree.place(ctx), FeatureResult::Skipped);
}

TEST_F(TreeFeatureTest, PlacesWithoutSoilCheck) {
    World world;

    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    config.minTrunkHeight = 4;
    config.maxTrunkHeight = 4;
    config.requiresSoil = false;

    TreeFeature tree("oak_tree", config);
    FeaturePlacementContext ctx{world, BlockPos(8, 64, 8), BiomeId{}, 42, nullptr};

    EXPECT_EQ(tree.place(ctx), FeatureResult::Placed);
    EXPECT_EQ(world.getBlock(8, 64, 8), oakLogId_);
}

TEST_F(TreeFeatureTest, MaxExtent) {
    TreeConfig config;
    config.maxTrunkHeight = 7;
    config.leafRadius = 2;

    TreeFeature tree("test", config);
    auto ext = tree.maxExtent();
    EXPECT_EQ(ext.x, 2);
    EXPECT_EQ(ext.y, 9);
    EXPECT_EQ(ext.z, 2);
}

TEST_F(TreeFeatureTest, DeterministicFromSeed) {
    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    config.minTrunkHeight = 4;
    config.maxTrunkHeight = 7;
    config.requiresSoil = false;

    World world1;
    World world2;
    TreeFeature tree("oak", config);

    FeaturePlacementContext ctx1{world1, BlockPos(8, 64, 8), BiomeId{}, 12345, nullptr};
    FeaturePlacementContext ctx2{world2, BlockPos(8, 64, 8), BiomeId{}, 12345, nullptr};

    (void)tree.place(ctx1);
    (void)tree.place(ctx2);

    for (int32_t y = 64; y < 72; ++y) {
        EXPECT_EQ(world1.getBlock(8, y, 8), world2.getBlock(8, y, 8))
            << "Mismatch at y=" << y;
    }
}

// ============================================================================
// OreFeature Tests
// ============================================================================

class OreFeatureTest : public FeatureTestBase {};

TEST_F(OreFeatureTest, PlacesOreInStone) {
    auto world = createFlatWorld();

    OreConfig config;
    config.oreBlock = ironOreId_;
    config.replaceBlock = stoneId_;
    config.veinSize = 8;
    config.minHeight = 0;
    config.maxHeight = 64;

    OreFeature ore("iron_ore", config);
    EXPECT_EQ(ore.name(), "iron_ore");

    FeaturePlacementContext ctx{*world, BlockPos(8, 30, 8), BiomeId{}, 42, nullptr};

    EXPECT_EQ(ore.place(ctx), FeatureResult::Placed);

    int oreCount = 0;
    for (int32_t dx = -8; dx <= 8; ++dx) {
        for (int32_t dy = -8; dy <= 8; ++dy) {
            for (int32_t dz = -8; dz <= 8; ++dz) {
                if (world->getBlock(8 + dx, 30 + dy, 8 + dz) == ironOreId_) {
                    ++oreCount;
                }
            }
        }
    }
    EXPECT_GT(oreCount, 0);
    EXPECT_LE(oreCount, config.veinSize);
}

TEST_F(OreFeatureTest, SkipsOutOfHeightRange) {
    auto world = createFlatWorld();

    OreConfig config;
    config.oreBlock = ironOreId_;
    config.replaceBlock = stoneId_;
    config.minHeight = 0;
    config.maxHeight = 20;

    OreFeature ore("iron_ore", config);
    FeaturePlacementContext ctx{*world, BlockPos(8, 50, 8), BiomeId{}, 42, nullptr};

    EXPECT_EQ(ore.place(ctx), FeatureResult::Skipped);
}

TEST_F(OreFeatureTest, DoesNotReplaceWrongBlock) {
    auto world = createFlatWorld();

    OreConfig config;
    config.oreBlock = ironOreId_;
    config.replaceBlock = dirtId_;
    config.veinSize = 8;
    config.minHeight = 0;
    config.maxHeight = 64;

    OreFeature ore("iron_ore", config);
    FeaturePlacementContext ctx{*world, BlockPos(8, 30, 8), BiomeId{}, 42, nullptr};

    EXPECT_EQ(ore.place(ctx), FeatureResult::Skipped);
}

TEST_F(OreFeatureTest, DeterministicFromSeed) {
    OreConfig config;
    config.oreBlock = ironOreId_;
    config.replaceBlock = stoneId_;
    config.veinSize = 10;
    config.minHeight = 0;
    config.maxHeight = 64;

    OreFeature ore("iron", config);

    auto world1 = createFlatWorld();
    auto world2 = createFlatWorld();

    FeaturePlacementContext ctx1{*world1, BlockPos(8, 30, 8), BiomeId{}, 999, nullptr};
    FeaturePlacementContext ctx2{*world2, BlockPos(8, 30, 8), BiomeId{}, 999, nullptr};

    (void)ore.place(ctx1);
    (void)ore.place(ctx2);

    for (int32_t dx = -10; dx <= 10; ++dx) {
        for (int32_t dy = -10; dy <= 10; ++dy) {
            for (int32_t dz = -10; dz <= 10; ++dz) {
                EXPECT_EQ(
                    world1->getBlock(8 + dx, 30 + dy, 8 + dz),
                    world2->getBlock(8 + dx, 30 + dy, 8 + dz))
                    << "Mismatch at (" << (8+dx) << "," << (30+dy) << "," << (8+dz) << ")";
            }
        }
    }
}

// ============================================================================
// SchematicFeature Tests
// ============================================================================

class SchematicFeatureTest : public FeatureTestBase {};

TEST_F(SchematicFeatureTest, PlacesSchematic) {
    auto schematic = std::make_shared<Schematic>(3, 3, 3);
    for (int32_t x = 0; x < 3; ++x) {
        for (int32_t y = 0; y < 3; ++y) {
            for (int32_t z = 0; z < 3; ++z) {
                schematic->at(x, y, z).typeName = "stone";
            }
        }
    }

    SchematicFeature feature("test_structure", schematic, true);
    EXPECT_EQ(feature.name(), "test_structure");

    World world;
    FeaturePlacementContext ctx{world, BlockPos(10, 64, 10), BiomeId{}, 42, nullptr};

    EXPECT_EQ(feature.place(ctx), FeatureResult::Placed);

    for (int32_t x = 0; x < 3; ++x) {
        for (int32_t y = 0; y < 3; ++y) {
            for (int32_t z = 0; z < 3; ++z) {
                EXPECT_EQ(world.getBlock(10 + x, 64 + y, 10 + z), stoneId_);
            }
        }
    }
}

TEST_F(SchematicFeatureTest, IgnoresAirBlocks) {
    auto schematic = std::make_shared<Schematic>(3, 1, 3);
    schematic->at(0, 0, 0).typeName = "stone";
    schematic->at(2, 0, 2).typeName = "stone";

    SchematicFeature feature("sparse", schematic, true);

    World world;
    auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
    for (int32_t x = 0; x < 16; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            col.setBlock(x, 64, z, dirtId_);
        }
    }

    FeaturePlacementContext ctx{world, BlockPos(5, 64, 5), BiomeId{}, 42, nullptr};
    (void)feature.place(ctx);

    EXPECT_EQ(world.getBlock(5, 64, 5), stoneId_);
    EXPECT_EQ(world.getBlock(7, 64, 7), stoneId_);
    EXPECT_EQ(world.getBlock(6, 64, 6), dirtId_);
}

TEST_F(SchematicFeatureTest, MaxExtent) {
    auto schematic = std::make_shared<Schematic>(5, 10, 3);
    SchematicFeature feature("test", schematic);

    auto ext = feature.maxExtent();
    EXPECT_EQ(ext.x, 5);
    EXPECT_EQ(ext.y, 10);
    EXPECT_EQ(ext.z, 3);
}

TEST_F(SchematicFeatureTest, NullSchematicFails) {
    SchematicFeature feature("null_test", nullptr);

    World world;
    FeaturePlacementContext ctx{world, BlockPos(0, 0, 0), BiomeId{}, 42, nullptr};

    EXPECT_EQ(feature.place(ctx), FeatureResult::Failed);
}

// ============================================================================
// FeatureRegistry Tests
// ============================================================================

class FeatureRegistryTest : public FeatureTestBase {};

TEST_F(FeatureRegistryTest, InitiallyEmpty) {
    EXPECT_EQ(FeatureRegistry::global().featureCount(), 0u);
    EXPECT_EQ(FeatureRegistry::global().placementCount(), 0u);
}

TEST_F(FeatureRegistryTest, RegisterAndRetrieve) {
    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;

    FeatureRegistry::global().registerFeature(
        std::make_shared<TreeFeature>("oak_tree", config));

    EXPECT_EQ(FeatureRegistry::global().featureCount(), 1u);
    EXPECT_NE(FeatureRegistry::global().getFeature("oak_tree"), nullptr);
    EXPECT_EQ(FeatureRegistry::global().getFeature("nonexistent"), nullptr);
}

TEST_F(FeatureRegistryTest, AddPlacement) {
    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    FeatureRegistry::global().registerFeature(
        std::make_shared<TreeFeature>("oak_tree", config));

    FeaturePlacement placement;
    placement.featureName = "oak_tree";
    placement.density = 0.02f;
    FeatureRegistry::global().addPlacement(placement);

    EXPECT_EQ(FeatureRegistry::global().placementCount(), 1u);
    auto all = FeatureRegistry::global().allPlacements();
    EXPECT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].featureName, "oak_tree");
}

TEST_F(FeatureRegistryTest, PlacementsForBiome) {
    TreeConfig treeConfig;
    treeConfig.trunkBlock = oakLogId_;
    treeConfig.leavesBlock = oakLeavesId_;
    FeatureRegistry::global().registerFeature(
        std::make_shared<TreeFeature>("oak_tree", treeConfig));

    OreConfig oreConfig;
    oreConfig.oreBlock = ironOreId_;
    oreConfig.replaceBlock = stoneId_;
    FeatureRegistry::global().registerFeature(
        std::make_shared<OreFeature>("iron_ore", oreConfig));

    BiomeId forestId = BiomeId::fromName("forest");
    BiomeId desertId = BiomeId::fromName("desert");

    FeaturePlacement treePlacement;
    treePlacement.featureName = "oak_tree";
    treePlacement.biomes = {forestId};
    FeatureRegistry::global().addPlacement(treePlacement);

    FeaturePlacement orePlacement;
    orePlacement.featureName = "iron_ore";
    FeatureRegistry::global().addPlacement(orePlacement);

    auto forestFeatures = FeatureRegistry::global().placementsForBiome(forestId);
    EXPECT_EQ(forestFeatures.size(), 2u);

    auto desertFeatures = FeatureRegistry::global().placementsForBiome(desertId);
    EXPECT_EQ(desertFeatures.size(), 1u);
    EXPECT_EQ(desertFeatures[0]->featureName, "iron_ore");
}

TEST_F(FeatureRegistryTest, Clear) {
    TreeConfig config;
    config.trunkBlock = oakLogId_;
    config.leavesBlock = oakLeavesId_;
    FeatureRegistry::global().registerFeature(
        std::make_shared<TreeFeature>("oak", config));
    FeatureRegistry::global().addPlacement(FeaturePlacement{"oak"});

    EXPECT_EQ(FeatureRegistry::global().featureCount(), 1u);
    EXPECT_EQ(FeatureRegistry::global().placementCount(), 1u);

    FeatureRegistry::global().clear();

    EXPECT_EQ(FeatureRegistry::global().featureCount(), 0u);
    EXPECT_EQ(FeatureRegistry::global().placementCount(), 0u);
}

// ============================================================================
// FeatureLoader Tests
// ============================================================================

class FeatureLoaderTest : public FeatureTestBase {
protected:
    void SetUp() override {
        FeatureTestBase::SetUp();
        testDir_ = std::filesystem::temp_directory_path() / "finevox_feature_test";
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override {
        FeatureTestBase::TearDown();
        std::filesystem::remove_all(testDir_);
    }

    void writeFile(const std::string& filename, const std::string& content) {
        std::ofstream out(testDir_ / filename);
        out << content;
    }

    std::filesystem::path testDir_;
};

TEST_F(FeatureLoaderTest, ParseTreeConfig) {
    ConfigParser parser;
    auto doc = parser.parseString(R"(
type: tree
trunk: oak_log
leaves: oak_leaves
min_trunk_height: 5
max_trunk_height: 8
leaf_radius: 3
requires_soil: true
)");

    auto config = FeatureLoader::parseTreeConfig(doc);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->trunkBlock, oakLogId_);
    EXPECT_EQ(config->leavesBlock, oakLeavesId_);
    EXPECT_EQ(config->minTrunkHeight, 5);
    EXPECT_EQ(config->maxTrunkHeight, 8);
    EXPECT_EQ(config->leafRadius, 3);
    EXPECT_TRUE(config->requiresSoil);
}

TEST_F(FeatureLoaderTest, ParseTreeConfigMissingTrunk) {
    ConfigParser parser;
    auto doc = parser.parseString("leaves: oak_leaves\n");

    EXPECT_FALSE(FeatureLoader::parseTreeConfig(doc).has_value());
}

TEST_F(FeatureLoaderTest, ParseOreConfig) {
    ConfigParser parser;
    auto doc = parser.parseString(R"(
block: iron_ore
replace: stone
vein_size: 10
min_height: 5
max_height: 50
veins_per_chunk: 12
)");

    auto config = FeatureLoader::parseOreConfig(doc);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->oreBlock, ironOreId_);
    EXPECT_EQ(config->replaceBlock, stoneId_);
    EXPECT_EQ(config->veinSize, 10);
    EXPECT_EQ(config->minHeight, 5);
    EXPECT_EQ(config->maxHeight, 50);
    EXPECT_EQ(config->veinsPerChunk, 12);
}

TEST_F(FeatureLoaderTest, ParseOreConfigMissingBlock) {
    ConfigParser parser;
    auto doc = parser.parseString("replace: stone\n");

    EXPECT_FALSE(FeatureLoader::parseOreConfig(doc).has_value());
}

TEST_F(FeatureLoaderTest, LoadFeatureFile) {
    writeFile("oak.feature", R"(
type: tree
trunk: oak_log
leaves: oak_leaves
min_trunk_height: 4
max_trunk_height: 7
)");

    auto feature = FeatureLoader::loadFeatureFile(
        "test:oak", (testDir_ / "oak.feature").string());
    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->name(), "test:oak");
}

TEST_F(FeatureLoaderTest, LoadOreFile) {
    writeFile("iron.ore", R"(
block: iron_ore
replace: stone
vein_size: 8
)");

    auto feature = FeatureLoader::loadOreFile(
        "test:iron", (testDir_ / "iron.ore").string());
    ASSERT_NE(feature, nullptr);
    EXPECT_EQ(feature->name(), "test:iron");
}

TEST_F(FeatureLoaderTest, LoadDirectory) {
    writeFile("oak.feature", R"(
type: tree
trunk: oak_log
leaves: oak_leaves
)");
    writeFile("iron.ore", R"(
block: iron_ore
replace: stone
)");
    writeFile("readme.txt", "not a feature");

    size_t count = FeatureLoader::loadDirectory(testDir_.string(), "demo");
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(FeatureRegistry::global().featureCount(), 2u);
    EXPECT_NE(FeatureRegistry::global().getFeature("demo:oak"), nullptr);
    EXPECT_NE(FeatureRegistry::global().getFeature("demo:iron"), nullptr);
}

TEST_F(FeatureLoaderTest, LoadDirectoryNonExistent) {
    EXPECT_EQ(FeatureLoader::loadDirectory("/nonexistent/path"), 0u);
}

}  // namespace
}  // namespace finevox
