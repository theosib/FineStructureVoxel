/**
 * @file test_biome.cpp
 * @brief Unit tests for BiomeId, BiomeRegistry, BiomeMap, and BiomeLoader
 */

#include <gtest/gtest.h>

#include "finevox/biome.hpp"
#include "finevox/biome_map.hpp"
#include "finevox/biome_loader.hpp"
#include "finevox/config_parser.hpp"

#include <filesystem>
#include <fstream>

namespace finevox {
namespace {

// ============================================================================
// BiomeId Tests
// ============================================================================

class BiomeIdTest : public ::testing::Test {
protected:
    void TearDown() override {
        BiomeRegistry::global().clear();
    }
};

TEST_F(BiomeIdTest, FromNameDeterministic) {
    auto id1 = BiomeId::fromName("plains");
    auto id2 = BiomeId::fromName("plains");
    EXPECT_EQ(id1, id2);
}

TEST_F(BiomeIdTest, DifferentNamesDifferentIds) {
    auto id1 = BiomeId::fromName("plains");
    auto id2 = BiomeId::fromName("desert");
    EXPECT_NE(id1, id2);
}

TEST_F(BiomeIdTest, RoundTripName) {
    auto id = BiomeId::fromName("forest");
    EXPECT_EQ(id.name(), "forest");
}

TEST_F(BiomeIdTest, DefaultIsZero) {
    BiomeId id;
    EXPECT_EQ(id.id, 0u);
}

// ============================================================================
// BiomeRegistry Tests
// ============================================================================

class BiomeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry().clear();
    }
    void TearDown() override {
        registry().clear();
    }
    BiomeRegistry& registry() { return BiomeRegistry::global(); }
};

TEST_F(BiomeRegistryTest, InitiallyEmpty) {
    EXPECT_EQ(registry().size(), 0u);
}

TEST_F(BiomeRegistryTest, RegisterAndRetrieveById) {
    BiomeProperties props;
    props.displayName = "Plains";
    props.baseHeight = 64.0f;
    registry().registerBiome("plains", props);

    auto id = BiomeId::fromName("plains");
    const BiomeProperties* result = registry().getBiome(id);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->displayName, "Plains");
    EXPECT_FLOAT_EQ(result->baseHeight, 64.0f);
    EXPECT_EQ(result->id, id);
}

TEST_F(BiomeRegistryTest, RegisterAndRetrieveByName) {
    BiomeProperties props;
    props.displayName = "Desert";
    registry().registerBiome("desert", props);

    const BiomeProperties* result = registry().getBiome("desert");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->displayName, "Desert");
}

TEST_F(BiomeRegistryTest, GetNonExistent) {
    EXPECT_EQ(registry().getBiome("nonexistent"), nullptr);
    EXPECT_EQ(registry().getBiome(BiomeId::fromName("also_nonexistent")), nullptr);
}

TEST_F(BiomeRegistryTest, OverwriteExisting) {
    BiomeProperties props1;
    props1.displayName = "Plains v1";
    registry().registerBiome("plains", props1);

    BiomeProperties props2;
    props2.displayName = "Plains v2";
    registry().registerBiome("plains", props2);

    const BiomeProperties* result = registry().getBiome("plains");
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->displayName, "Plains v2");
    EXPECT_EQ(registry().size(), 1u);
}

TEST_F(BiomeRegistryTest, AllBiomes) {
    BiomeProperties p;
    registry().registerBiome("a", p);
    registry().registerBiome("b", p);
    registry().registerBiome("c", p);

    auto all = registry().allBiomes();
    EXPECT_EQ(all.size(), 3u);
}

TEST_F(BiomeRegistryTest, Clear) {
    BiomeProperties p;
    registry().registerBiome("plains", p);
    EXPECT_EQ(registry().size(), 1u);
    registry().clear();
    EXPECT_EQ(registry().size(), 0u);
}

// ============================================================================
// Biome Selection Tests
// ============================================================================

class BiomeSelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry().clear();

        // Register biomes with distinct climate ranges
        BiomeProperties plains;
        plains.displayName = "Plains";
        plains.temperatureMin = 0.3f;
        plains.temperatureMax = 0.7f;
        plains.humidityMin = 0.3f;
        plains.humidityMax = 0.6f;
        registry().registerBiome("plains", plains);

        BiomeProperties desert;
        desert.displayName = "Desert";
        desert.temperatureMin = 0.7f;
        desert.temperatureMax = 1.0f;
        desert.humidityMin = 0.0f;
        desert.humidityMax = 0.2f;
        registry().registerBiome("desert", desert);

        BiomeProperties tundra;
        tundra.displayName = "Tundra";
        tundra.temperatureMin = 0.0f;
        tundra.temperatureMax = 0.2f;
        tundra.humidityMin = 0.0f;
        tundra.humidityMax = 0.4f;
        registry().registerBiome("tundra", tundra);

        BiomeProperties jungle;
        jungle.displayName = "Jungle";
        jungle.temperatureMin = 0.7f;
        jungle.temperatureMax = 1.0f;
        jungle.humidityMin = 0.7f;
        jungle.humidityMax = 1.0f;
        registry().registerBiome("jungle", jungle);
    }

    void TearDown() override {
        registry().clear();
    }

    BiomeRegistry& registry() { return BiomeRegistry::global(); }
};

TEST_F(BiomeSelectionTest, SelectsPlainsInCenter) {
    auto id = registry().selectBiome(0.5f, 0.45f);
    EXPECT_EQ(id.name(), "plains");
}

TEST_F(BiomeSelectionTest, SelectsDesertHotDry) {
    auto id = registry().selectBiome(0.9f, 0.1f);
    EXPECT_EQ(id.name(), "desert");
}

TEST_F(BiomeSelectionTest, SelectsTundraCold) {
    auto id = registry().selectBiome(0.1f, 0.2f);
    EXPECT_EQ(id.name(), "tundra");
}

TEST_F(BiomeSelectionTest, SelectsJungleHotWet) {
    auto id = registry().selectBiome(0.9f, 0.9f);
    EXPECT_EQ(id.name(), "jungle");
}

TEST_F(BiomeSelectionTest, HandlesSingleBiome) {
    registry().clear();
    BiomeProperties solo;
    solo.displayName = "Only";
    registry().registerBiome("only", solo);

    auto id = registry().selectBiome(0.5f, 0.5f);
    EXPECT_EQ(id.name(), "only");
}

// ============================================================================
// BiomeMap Tests
// ============================================================================

class BiomeMapTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry().clear();

        // Register a few biomes with climate ranges
        BiomeProperties plains;
        plains.temperatureMin = 0.3f;
        plains.temperatureMax = 0.7f;
        plains.humidityMin = 0.2f;
        plains.humidityMax = 0.6f;
        plains.baseHeight = 64.0f;
        plains.heightVariation = 8.0f;
        registry().registerBiome("plains", plains);

        BiomeProperties desert;
        desert.temperatureMin = 0.7f;
        desert.temperatureMax = 1.0f;
        desert.humidityMin = 0.0f;
        desert.humidityMax = 0.2f;
        desert.baseHeight = 60.0f;
        desert.heightVariation = 4.0f;
        registry().registerBiome("desert", desert);

        BiomeProperties forest;
        forest.temperatureMin = 0.3f;
        forest.temperatureMax = 0.7f;
        forest.humidityMin = 0.5f;
        forest.humidityMax = 0.9f;
        forest.baseHeight = 68.0f;
        forest.heightVariation = 12.0f;
        registry().registerBiome("forest", forest);
    }

    void TearDown() override {
        registry().clear();
    }

    BiomeRegistry& registry() { return BiomeRegistry::global(); }
};

TEST_F(BiomeMapTest, DeterministicSameSeed) {
    BiomeMap map1(42, registry());
    BiomeMap map2(42, registry());

    for (float x = -100; x <= 100; x += 50) {
        for (float z = -100; z <= 100; z += 50) {
            EXPECT_EQ(map1.getBiome(x, z), map2.getBiome(x, z));
        }
    }
}

TEST_F(BiomeMapTest, DifferentSeedsDifferentResults) {
    BiomeMap map1(42, registry());
    BiomeMap map2(999, registry());

    // At least some positions should differ over a large area
    int differences = 0;
    for (float x = -500; x <= 500; x += 100) {
        for (float z = -500; z <= 500; z += 100) {
            if (map1.getBiome(x, z) != map2.getBiome(x, z)) {
                ++differences;
            }
        }
    }
    EXPECT_GT(differences, 0);
}

TEST_F(BiomeMapTest, TemperatureInRange) {
    BiomeMap map(42, registry());
    for (float x = -200; x <= 200; x += 50) {
        for (float z = -200; z <= 200; z += 50) {
            float temp = map.getTemperature(x, z);
            EXPECT_GE(temp, 0.0f);
            EXPECT_LE(temp, 1.0f);
        }
    }
}

TEST_F(BiomeMapTest, HumidityInRange) {
    BiomeMap map(42, registry());
    for (float x = -200; x <= 200; x += 50) {
        for (float z = -200; z <= 200; z += 50) {
            float hum = map.getHumidity(x, z);
            EXPECT_GE(hum, 0.0f);
            EXPECT_LE(hum, 1.0f);
        }
    }
}

TEST_F(BiomeMapTest, GetTerrainParamsReturnsValidValues) {
    BiomeMap map(42, registry());

    auto [baseHeight, heightVar] = map.getTerrainParams(100, 100);
    // Should be within the range of our biome definitions
    EXPECT_GE(baseHeight, 50.0f);
    EXPECT_LE(baseHeight, 80.0f);
    EXPECT_GE(heightVar, 0.0f);
    EXPECT_LE(heightVar, 20.0f);
}

TEST_F(BiomeMapTest, BlendedBiomePrimaryValid) {
    BiomeMap map(42, registry());

    auto blend = map.getBlendedBiome(100, 100);
    // Primary should be a registered biome
    EXPECT_NE(registry().getBiome(blend.primary), nullptr);
    // Blend weight should be in [0, 1]
    EXPECT_GE(blend.blendWeight, 0.0f);
    EXPECT_LE(blend.blendWeight, 1.0f);
}

TEST_F(BiomeMapTest, BlendedBiomeSecondaryValid) {
    BiomeMap map(42, registry());

    // Sample many positions to find one with blending
    bool foundBlend = false;
    for (float x = -500; x <= 500 && !foundBlend; x += 10) {
        for (float z = -500; z <= 500 && !foundBlend; z += 10) {
            auto blend = map.getBlendedBiome(x, z);
            if (blend.blendWeight > 0.0f) {
                EXPECT_NE(registry().getBiome(blend.secondary), nullptr);
                foundBlend = true;
            }
        }
    }
    // It's possible no blending is found in this range, that's OK
}

// ============================================================================
// BiomeLoader Tests
// ============================================================================

class BiomeLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        BiomeRegistry::global().clear();
        testDir_ = std::filesystem::temp_directory_path() / "finevox_biome_test";
        std::filesystem::create_directories(testDir_);
    }

    void TearDown() override {
        BiomeRegistry::global().clear();
        std::filesystem::remove_all(testDir_);
    }

    void writeFile(const std::string& filename, const std::string& content) {
        std::ofstream out(testDir_ / filename);
        out << content;
    }

    std::filesystem::path testDir_;
};

TEST_F(BiomeLoaderTest, LoadFromConfig) {
    ConfigParser parser;
    auto doc = parser.parseString(R"(
name: Plains Biome
temperature_min: 0.3
temperature_max: 0.7
humidity_min: 0.2
humidity_max: 0.6
base_height: 64.0
height_variation: 8.0
height_scale: 1.2
surface: grass_block
filler: dirt
filler_depth: 4
stone: stone
underwater: sand
tree_density: 0.1
ore_density: 1.5
decoration_density: 0.8
)");

    auto props = BiomeLoader::loadFromConfig("test:plains", doc);
    ASSERT_TRUE(props.has_value());
    EXPECT_EQ(props->displayName, "Plains Biome");
    EXPECT_FLOAT_EQ(props->temperatureMin, 0.3f);
    EXPECT_FLOAT_EQ(props->temperatureMax, 0.7f);
    EXPECT_FLOAT_EQ(props->humidityMin, 0.2f);
    EXPECT_FLOAT_EQ(props->humidityMax, 0.6f);
    EXPECT_FLOAT_EQ(props->baseHeight, 64.0f);
    EXPECT_FLOAT_EQ(props->heightVariation, 8.0f);
    EXPECT_FLOAT_EQ(props->heightScale, 1.2f);
    EXPECT_EQ(props->surfaceBlock, "grass_block");
    EXPECT_EQ(props->fillerBlock, "dirt");
    EXPECT_EQ(props->fillerDepth, 4);
    EXPECT_EQ(props->stoneBlock, "stone");
    EXPECT_EQ(props->underwaterBlock, "sand");
    EXPECT_FLOAT_EQ(props->treeDensity, 0.1f);
    EXPECT_FLOAT_EQ(props->oreDensity, 1.5f);
    EXPECT_FLOAT_EQ(props->decorationDensity, 0.8f);
}

TEST_F(BiomeLoaderTest, LoadFromConfigMinimal) {
    ConfigParser parser;
    auto doc = parser.parseString("name: Simple\n");

    auto props = BiomeLoader::loadFromConfig("simple", doc);
    ASSERT_TRUE(props.has_value());
    EXPECT_EQ(props->displayName, "Simple");
    // Defaults
    EXPECT_FLOAT_EQ(props->temperatureMin, 0.0f);
    EXPECT_FLOAT_EQ(props->temperatureMax, 1.0f);
    EXPECT_FLOAT_EQ(props->baseHeight, 64.0f);
    EXPECT_EQ(props->surfaceBlock, "grass");
}

TEST_F(BiomeLoaderTest, LoadFromConfigNoName) {
    ConfigParser parser;
    auto doc = parser.parseString("base_height: 70.0\n");

    auto props = BiomeLoader::loadFromConfig("unnamed_biome", doc);
    ASSERT_TRUE(props.has_value());
    EXPECT_EQ(props->displayName, "unnamed_biome");
}

TEST_F(BiomeLoaderTest, LoadFromFile) {
    writeFile("test.biome", R"(
name: Test Biome
temperature_min: 0.5
base_height: 72.0
)");

    auto props = BiomeLoader::loadFromFile("test", (testDir_ / "test.biome").string());
    ASSERT_TRUE(props.has_value());
    EXPECT_EQ(props->displayName, "Test Biome");
    EXPECT_FLOAT_EQ(props->temperatureMin, 0.5f);
    EXPECT_FLOAT_EQ(props->baseHeight, 72.0f);
}

TEST_F(BiomeLoaderTest, LoadFromFileMissing) {
    auto props = BiomeLoader::loadFromFile("missing", "/nonexistent/path/missing.biome");
    EXPECT_FALSE(props.has_value());
}

TEST_F(BiomeLoaderTest, LoadDirectory) {
    writeFile("plains.biome", R"(
name: Plains
temperature_min: 0.3
temperature_max: 0.7
)");
    writeFile("desert.biome", R"(
name: Desert
temperature_min: 0.7
temperature_max: 1.0
)");
    writeFile("not_a_biome.txt", "ignored");

    size_t count = BiomeLoader::loadDirectory(testDir_.string(), "demo");
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(BiomeRegistry::global().size(), 2u);

    // Check the names are prefixed
    auto* plains = BiomeRegistry::global().getBiome("demo:plains");
    ASSERT_NE(plains, nullptr);
    EXPECT_EQ(plains->displayName, "Plains");

    auto* desert = BiomeRegistry::global().getBiome("demo:desert");
    ASSERT_NE(desert, nullptr);
    EXPECT_EQ(desert->displayName, "Desert");
}

TEST_F(BiomeLoaderTest, LoadDirectoryNoPrefix) {
    writeFile("forest.biome", "name: Forest\n");

    size_t count = BiomeLoader::loadDirectory(testDir_.string());
    EXPECT_EQ(count, 1u);

    auto* forest = BiomeRegistry::global().getBiome("forest");
    ASSERT_NE(forest, nullptr);
}

TEST_F(BiomeLoaderTest, LoadDirectoryNonExistent) {
    size_t count = BiomeLoader::loadDirectory("/nonexistent/path");
    EXPECT_EQ(count, 0u);
}

}  // namespace
}  // namespace finevox
