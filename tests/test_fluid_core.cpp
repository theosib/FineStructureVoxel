#include <gtest/gtest.h>
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_interaction.hpp"
#include "finevox/core/fluid_loader.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

using namespace finevox;

// ============================================================================
// FluidTypeId Tests
// ============================================================================

TEST(FluidTypeIdTest, DefaultIsEmpty) {
    FluidTypeId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
    EXPECT_EQ(id, EMPTY_FLUID_TYPE);
}

TEST(FluidTypeIdTest, FromNameCreatesValid) {
    FluidTypeId water = FluidTypeId::fromName("water");
    EXPECT_FALSE(water.isEmpty());
    EXPECT_TRUE(water.isValid());
    EXPECT_EQ(water.name(), "water");
}

TEST(FluidTypeIdTest, SameNameSameId) {
    FluidTypeId a = FluidTypeId::fromName("water");
    FluidTypeId b = FluidTypeId::fromName("water");
    EXPECT_EQ(a, b);
}

TEST(FluidTypeIdTest, DifferentNamesDifferentIds) {
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");
    EXPECT_NE(water, lava);
}

TEST(FluidTypeIdTest, HashWorks) {
    FluidTypeId water = FluidTypeId::fromName("water");
    std::hash<FluidTypeId> hasher;
    size_t h = hasher(water);
    EXPECT_EQ(h, hasher(water));  // Deterministic
}

TEST(FluidTypeIdTest, Ordering) {
    FluidTypeId a = FluidTypeId::fromName("fluid_a_ordering");
    FluidTypeId b = FluidTypeId::fromName("fluid_b_ordering");
    // Just verify that ordering is consistent
    EXPECT_TRUE((a < b) || (b < a) || (a == b));
}

// ============================================================================
// FluidCell Tests
// ============================================================================

TEST(FluidCellTest, DefaultIsEmpty) {
    FluidCell cell;
    EXPECT_TRUE(cell.isEmpty());
    EXPECT_FALSE(cell.isSource());
    EXPECT_FALSE(cell.isFlowing());
    EXPECT_EQ(cell.paletteIndex, 0);
    EXPECT_EQ(cell.level, 0);
}

TEST(FluidCellTest, PackUnpackRoundTrip) {
    FluidCell cell{3, 10};
    uint8_t packed = cell.pack();
    FluidCell unpacked = FluidCell::unpack(packed);
    EXPECT_EQ(unpacked.paletteIndex, 3);
    EXPECT_EQ(unpacked.level, 10);
    EXPECT_EQ(cell, unpacked);
}

TEST(FluidCellTest, PackUnpackAllValues) {
    for (uint8_t idx = 0; idx <= 15; ++idx) {
        for (uint8_t lvl = 0; lvl <= 15; ++lvl) {
            FluidCell cell{idx, lvl};
            FluidCell roundTrip = FluidCell::unpack(cell.pack());
            EXPECT_EQ(roundTrip.paletteIndex, idx);
            EXPECT_EQ(roundTrip.level, lvl);
        }
    }
}

TEST(FluidCellTest, SourceDetection) {
    FluidCell source{1, FLUID_SOURCE_LEVEL};
    EXPECT_TRUE(source.isSource());
    EXPECT_FALSE(source.isFlowing());
    EXPECT_FALSE(source.isEmpty());
}

TEST(FluidCellTest, FlowingDetection) {
    FluidCell flowing{1, 7};
    EXPECT_TRUE(flowing.isFlowing());
    EXPECT_FALSE(flowing.isSource());
    EXPECT_FALSE(flowing.isEmpty());
}

// ============================================================================
// FluidPalette Tests
// ============================================================================

TEST(FluidPaletteTest, InitiallyEmpty) {
    FluidPalette palette;
    EXPECT_TRUE(palette.empty());
    EXPECT_EQ(palette.size(), 0);
    EXPECT_FALSE(palette.full());
}

TEST(FluidPaletteTest, AddAndRetrieve) {
    FluidPalette palette;
    FluidTypeId water = FluidTypeId::fromName("water");

    uint8_t idx = palette.addType(water);
    EXPECT_GT(idx, 0u);  // Not 0 (empty)
    EXPECT_EQ(palette.getType(idx), water);
    EXPECT_EQ(palette.getIndex(water), idx);
    EXPECT_TRUE(palette.contains(water));
    EXPECT_EQ(palette.size(), 1);
}

TEST(FluidPaletteTest, EmptyTypeReturnsZero) {
    FluidPalette palette;
    uint8_t idx = palette.addType(EMPTY_FLUID_TYPE);
    EXPECT_EQ(idx, 0);
}

TEST(FluidPaletteTest, SameTypeReturnsSameIndex) {
    FluidPalette palette;
    FluidTypeId water = FluidTypeId::fromName("water");

    uint8_t idx1 = palette.addType(water);
    uint8_t idx2 = palette.addType(water);
    EXPECT_EQ(idx1, idx2);
    EXPECT_EQ(palette.size(), 1);
}

TEST(FluidPaletteTest, MultipleTypes) {
    FluidPalette palette;
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");

    uint8_t waterIdx = palette.addType(water);
    uint8_t lavaIdx = palette.addType(lava);

    EXPECT_NE(waterIdx, lavaIdx);
    EXPECT_EQ(palette.size(), 2);
    EXPECT_EQ(palette.getType(waterIdx), water);
    EXPECT_EQ(palette.getType(lavaIdx), lava);
}

TEST(FluidPaletteTest, RemoveType) {
    FluidPalette palette;
    FluidTypeId water = FluidTypeId::fromName("water");

    uint8_t idx = palette.addType(water);
    EXPECT_EQ(palette.size(), 1);

    palette.removeType(water);
    EXPECT_EQ(palette.size(), 0);
    EXPECT_FALSE(palette.contains(water));
    EXPECT_EQ(palette.getType(idx), EMPTY_FLUID_TYPE);
}

TEST(FluidPaletteTest, MaxCapacity) {
    FluidPalette palette;

    // Add 15 different fluid types (max)
    for (int i = 0; i < FluidPalette::MAX_ENTRIES; ++i) {
        FluidTypeId type = FluidTypeId::fromName("palette_test_" + std::to_string(i));
        uint8_t idx = palette.addType(type);
        EXPECT_GT(idx, 0u);
    }

    EXPECT_TRUE(palette.full());
    EXPECT_EQ(palette.size(), FluidPalette::MAX_ENTRIES);

    // Adding one more should fail
    FluidTypeId overflow = FluidTypeId::fromName("palette_overflow");
    uint8_t overflowIdx = palette.addType(overflow);
    EXPECT_EQ(overflowIdx, 0);
}

TEST(FluidPaletteTest, ReuseRemovedSlot) {
    FluidPalette palette;
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");

    uint8_t waterIdx = palette.addType(water);
    palette.removeType(water);

    uint8_t lavaIdx = palette.addType(lava);
    EXPECT_EQ(lavaIdx, waterIdx);  // Reused the freed slot
}

TEST(FluidPaletteTest, ClearRemovesAll) {
    FluidPalette palette;
    (void)palette.addType(FluidTypeId::fromName("water"));
    (void)palette.addType(FluidTypeId::fromName("lava"));

    palette.clear();
    EXPECT_TRUE(palette.empty());
    EXPECT_EQ(palette.size(), 0);
}

// ============================================================================
// FluidLayer Tests
// ============================================================================

TEST(FluidLayerTest, InitiallyEmpty) {
    FluidLayer layer;
    EXPECT_TRUE(layer.isEmpty());
    EXPECT_EQ(layer.nonEmptyCount(), 0);
}

TEST(FluidLayerTest, SetAndGetFluid) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    bool changed = layer.setFluid(5, 3, 7, water, FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(layer.isEmpty());
    EXPECT_EQ(layer.nonEmptyCount(), 1);

    EXPECT_EQ(layer.getFluidType(5, 3, 7), water);
    EXPECT_EQ(layer.getLevel(5, 3, 7), FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(layer.hasFluid(5, 3, 7));
}

TEST(FluidLayerTest, SetFluidByIndex) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    // Index = y*256 + z*16 + x
    int32_t index = 3*256 + 7*16 + 5;
    bool changed = layer.setFluid(index, water, 10);
    EXPECT_TRUE(changed);

    EXPECT_EQ(layer.getFluidType(index), water);
    EXPECT_EQ(layer.getLevel(index), 10);
}

TEST(FluidLayerTest, NoChangeReturnsFalse) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, water, 10);
    bool changed = layer.setFluid(0, water, 10);
    EXPECT_FALSE(changed);
}

TEST(FluidLayerTest, RemoveFluid) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    EXPECT_EQ(layer.nonEmptyCount(), 1);

    bool removed = layer.removeFluid(0, 0, 0);
    EXPECT_TRUE(removed);
    EXPECT_TRUE(layer.isEmpty());
    EXPECT_EQ(layer.nonEmptyCount(), 0);
    EXPECT_FALSE(layer.hasFluid(0, 0, 0));
}

TEST(FluidLayerTest, RemoveNonexistentReturnsFalse) {
    FluidLayer layer;
    bool removed = layer.removeFluid(0, 0, 0);
    EXPECT_FALSE(removed);
}

TEST(FluidLayerTest, MultipleFluidsCoexist) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");

    layer.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    layer.setFluid(1, 0, 0, lava, FLUID_SOURCE_LEVEL);

    EXPECT_EQ(layer.getFluidType(0, 0, 0), water);
    EXPECT_EQ(layer.getFluidType(1, 0, 0), lava);
    EXPECT_EQ(layer.nonEmptyCount(), 2);
}

TEST(FluidLayerTest, VersionBumpsOnChange) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    uint64_t v1 = layer.version();
    layer.setFluid(0, water, 10);
    uint64_t v2 = layer.version();
    EXPECT_GT(v2, v1);

    layer.setFluid(0, water, 5);  // Change level
    uint64_t v3 = layer.version();
    EXPECT_GT(v3, v2);

    layer.removeFluid(0);
    uint64_t v4 = layer.version();
    EXPECT_GT(v4, v3);
}

TEST(FluidLayerTest, VersionNoChangeOnSameValue) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, water, 10);
    uint64_t v1 = layer.version();

    layer.setFluid(0, water, 10);  // Same value
    uint64_t v2 = layer.version();
    EXPECT_EQ(v1, v2);
}

TEST(FluidLayerTest, ClearRemovesAllFluid) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    for (int i = 0; i < 100; ++i) {
        layer.setFluid(i, water, FLUID_SOURCE_LEVEL);
    }
    EXPECT_EQ(layer.nonEmptyCount(), 100);

    layer.clear();
    EXPECT_TRUE(layer.isEmpty());
    EXPECT_EQ(layer.nonEmptyCount(), 0);
}

TEST(FluidLayerTest, SetEmptyRemoves) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, water, 10);
    EXPECT_EQ(layer.nonEmptyCount(), 1);

    // Setting level 0 or empty type should remove
    layer.setFluid(0, water, 0);
    EXPECT_EQ(layer.nonEmptyCount(), 0);
}

TEST(FluidLayerTest, RawDataRoundTrip) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, water, FLUID_SOURCE_LEVEL);
    layer.setFluid(100, water, 7);

    // Get raw data and palette
    auto rawData = layer.rawData();

    // Create new layer with same palette and load data
    FluidLayer layer2;
    // We need to set up the same palette first
    layer2.setFluid(0, water, FLUID_SOURCE_LEVEL);  // Ensures palette is set up
    layer2.clear();
    layer2.setFluid(0, water, 1);  // Re-create palette entry
    layer2.removeFluid(0);

    // setRawData restores the raw packed bytes
    layer2.setRawData(rawData);
    EXPECT_EQ(layer2.nonEmptyCount(), layer.nonEmptyCount());
}

TEST(FluidLayerTest, PaletteAutoCleanup) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(0, water, FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(layer.palette().contains(water));

    layer.removeFluid(0);
    // After removing last reference, palette should be cleaned
    EXPECT_FALSE(layer.palette().contains(water));
}

TEST(FluidLayerTest, GetCellReturnsCorrectData) {
    FluidLayer layer;
    FluidTypeId water = FluidTypeId::fromName("water");

    layer.setFluid(5, 3, 7, water, 10);

    FluidCell cell = layer.getCell(5, 3, 7);
    EXPECT_FALSE(cell.isEmpty());
    EXPECT_EQ(cell.level, 10);
    EXPECT_TRUE(cell.isFlowing());
}

// ============================================================================
// FluidRegistry Tests
// ============================================================================

class FluidRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        FluidRegistry::global().clear();
    }

    void TearDown() override {
        FluidRegistry::global().clear();
    }
};

TEST_F(FluidRegistryTest, RegisterAndRetrieve) {
    FluidType waterType;
    waterType.name = "water";
    waterType.density = 1000.0f;

    EXPECT_TRUE(FluidRegistry::global().registerType("water", waterType));
    EXPECT_EQ(FluidRegistry::global().size(), 1);

    FluidTypeId waterId = FluidTypeId::fromName("water");
    const FluidType* retrieved = FluidRegistry::global().getType(waterId);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "water");
    EXPECT_FLOAT_EQ(retrieved->density, 1000.0f);
}

TEST_F(FluidRegistryTest, DuplicateRegistrationFails) {
    FluidType waterType;
    waterType.name = "water";

    EXPECT_TRUE(FluidRegistry::global().registerType("water", waterType));
    EXPECT_FALSE(FluidRegistry::global().registerType("water", waterType));
    EXPECT_EQ(FluidRegistry::global().size(), 1);
}

TEST_F(FluidRegistryTest, LookupByName) {
    FluidType lavaType;
    lavaType.name = "lava";
    lavaType.lightEmission = 15;

    FluidRegistry::global().registerType("lava", lavaType);

    const FluidType* retrieved = FluidRegistry::global().getType("lava");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->lightEmission, 15);
}

TEST_F(FluidRegistryTest, LookupNotFound) {
    const FluidType* result = FluidRegistry::global().getType("nonexistent");
    EXPECT_EQ(result, nullptr);
}

TEST_F(FluidRegistryTest, HasType) {
    FluidType waterType;
    waterType.name = "water";
    FluidRegistry::global().registerType("water", waterType);

    FluidTypeId waterId = FluidTypeId::fromName("water");
    EXPECT_TRUE(FluidRegistry::global().hasType(waterId));

    FluidTypeId lavaId = FluidTypeId::fromName("lava");
    EXPECT_FALSE(FluidRegistry::global().hasType(lavaId));
}

TEST_F(FluidRegistryTest, GetTypeId) {
    FluidType waterType;
    waterType.name = "water";
    FluidRegistry::global().registerType("water", waterType);

    FluidTypeId id = FluidRegistry::global().getTypeId("water");
    EXPECT_TRUE(id.isValid());

    FluidTypeId unknown = FluidRegistry::global().getTypeId("unknown");
    EXPECT_TRUE(unknown.isEmpty());
}

TEST_F(FluidRegistryTest, ForEachType) {
    FluidType waterType;
    waterType.name = "water";
    FluidType lavaType;
    lavaType.name = "lava";

    FluidRegistry::global().registerType("water", waterType);
    FluidRegistry::global().registerType("lava", lavaType);

    int count = 0;
    FluidRegistry::global().forEachType([&](FluidTypeId, const FluidType&) {
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST_F(FluidRegistryTest, IdSetOnRegistration) {
    FluidType waterType;
    waterType.name = "water";
    FluidRegistry::global().registerType("water", waterType);

    const FluidType* retrieved = FluidRegistry::global().getType("water");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_TRUE(retrieved->id.isValid());
    EXPECT_EQ(retrieved->id, FluidTypeId::fromName("water"));
}

// ============================================================================
// FluidInteractionRegistry Tests
// ============================================================================

class FluidInteractionTest : public ::testing::Test {
protected:
    void SetUp() override {
        FluidInteractionRegistry::global().clear();
    }

    void TearDown() override {
        FluidInteractionRegistry::global().clear();
    }
};

TEST_F(FluidInteractionTest, RegisterAndLookup) {
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");
    BlockTypeId cobble = BlockTypeId::fromName("finevox:cobblestone");

    FluidInteraction interaction;
    interaction.fluidA = water;
    interaction.fluidB = lava;
    interaction.resultBlock = cobble;
    interaction.consumeA = false;
    interaction.consumeB = true;

    EXPECT_TRUE(FluidInteractionRegistry::global().registerInteraction(interaction));
    EXPECT_EQ(FluidInteractionRegistry::global().size(), 1);

    const FluidInteraction* result = FluidInteractionRegistry::global().getInteraction(water, lava);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->resultBlock, cobble);
    EXPECT_FALSE(result->consumeA);
    EXPECT_TRUE(result->consumeB);
}

TEST_F(FluidInteractionTest, SymmetricLookup) {
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");

    FluidInteraction interaction;
    interaction.fluidA = water;
    interaction.fluidB = lava;
    FluidInteractionRegistry::global().registerInteraction(interaction);

    // Should find it both ways
    EXPECT_NE(FluidInteractionRegistry::global().getInteraction(water, lava), nullptr);
    EXPECT_NE(FluidInteractionRegistry::global().getInteraction(lava, water), nullptr);
}

TEST_F(FluidInteractionTest, DuplicateRegistrationFails) {
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId lava = FluidTypeId::fromName("lava");

    FluidInteraction interaction;
    interaction.fluidA = water;
    interaction.fluidB = lava;

    EXPECT_TRUE(FluidInteractionRegistry::global().registerInteraction(interaction));
    EXPECT_FALSE(FluidInteractionRegistry::global().registerInteraction(interaction));
}

TEST_F(FluidInteractionTest, NoInteractionReturnsNull) {
    FluidTypeId water = FluidTypeId::fromName("water");
    FluidTypeId milk = FluidTypeId::fromName("milk");

    EXPECT_EQ(FluidInteractionRegistry::global().getInteraction(water, milk), nullptr);
    EXPECT_FALSE(FluidInteractionRegistry::global().hasInteraction(water, milk));
}

// ============================================================================
// FluidLoader Tests
// ============================================================================

class FluidLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        FluidRegistry::global().clear();
    }

    void TearDown() override {
        FluidRegistry::global().clear();
    }
};

TEST_F(FluidLoaderTest, LoadWaterFromString) {
    std::string_view content = R"(
name: water
spread_decay: 1
flow_speed: 5
density: 1000.0
viscosity: 1.0
opaque: false
tint_color: 0.2 0.4 0.9 0.6
light_emission: 0
custom_attenuation: true
attenuation_base: 0.85
units_per_source: 1000
)";

    auto result = FluidLoader::loadFromString(content);
    ASSERT_TRUE(result.has_value());

    const FluidType& ft = *result;
    EXPECT_EQ(ft.name, "water");
    EXPECT_EQ(ft.spreadDecay, 1);
    EXPECT_EQ(ft.flowSpeed, 5);
    EXPECT_FLOAT_EQ(ft.density, 1000.0f);
    EXPECT_FLOAT_EQ(ft.viscosity, 1.0f);
    EXPECT_FALSE(ft.opaque);
    EXPECT_TRUE(ft.customAttenuation);
    EXPECT_FLOAT_EQ(ft.attenuationBase, 0.85f);
    EXPECT_EQ(ft.unitsPerSource, 1000);
}

TEST_F(FluidLoaderTest, LoadLavaFromString) {
    std::string_view content = R"(
name: lava
spread_decay: 2
flow_speed: 30
source_formation: false
density: 3100.0
viscosity: 4.0
opaque: true
light_emission: 15
contact_damage: 4.0
submersion_damage: 8.0
infiltrates_non_full: false
)";

    auto result = FluidLoader::loadFromString(content);
    ASSERT_TRUE(result.has_value());

    const FluidType& ft = *result;
    EXPECT_EQ(ft.name, "lava");
    EXPECT_EQ(ft.spreadDecay, 2);
    EXPECT_EQ(ft.flowSpeed, 30);
    EXPECT_FALSE(ft.sourceFormation);
    EXPECT_FLOAT_EQ(ft.density, 3100.0f);
    EXPECT_TRUE(ft.opaque);
    EXPECT_EQ(ft.lightEmission, 15);
    EXPECT_FLOAT_EQ(ft.contactDamage, 4.0f);
    EXPECT_FLOAT_EQ(ft.submersionDamage, 8.0f);
    EXPECT_FALSE(ft.infiltratesNonFull);
}

TEST_F(FluidLoaderTest, EmptyContentReturnsNullopt) {
    auto result = FluidLoader::loadFromString("");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FluidLoaderTest, NoNameReturnsNullopt) {
    auto result = FluidLoader::loadFromString("density: 1000.0\n");
    EXPECT_FALSE(result.has_value());
}

TEST_F(FluidLoaderTest, DefaultValues) {
    auto result = FluidLoader::loadFromString("name: minimal\n");
    ASSERT_TRUE(result.has_value());

    const FluidType& ft = *result;
    EXPECT_EQ(ft.spreadDecay, 1);
    EXPECT_EQ(ft.flowSpeed, 5);
    EXPECT_TRUE(ft.sourceFormation);
    EXPECT_EQ(ft.sourceFormationCount, 2);
    EXPECT_TRUE(ft.infiltratesNonFull);
    EXPECT_TRUE(ft.infiltratesBelow);
    EXPECT_FLOAT_EQ(ft.density, 1000.0f);
    EXPECT_FLOAT_EQ(ft.viscosity, 1.0f);
    EXPECT_FALSE(ft.opaque);
    EXPECT_EQ(ft.lightEmission, 0);
    EXPECT_EQ(ft.unitsPerSource, 1000);
}

// ============================================================================
// SubChunk Fluid Layer Tests
// ============================================================================

TEST(SubChunkFluidTest, NoFluidLayerByDefault) {
    SubChunk sc;
    EXPECT_FALSE(sc.hasFluidLayer());
    EXPECT_EQ(sc.fluidLayer(), nullptr);
    EXPECT_EQ(sc.fluidVersion(), 0);
}

TEST(SubChunkFluidTest, GetFluidOnEmptyReturnsEmpty) {
    SubChunk sc;
    EXPECT_EQ(sc.getFluid(0, 0, 0), EMPTY_FLUID_TYPE);
    EXPECT_EQ(sc.getFluidLevel(0, 0, 0), 0);
    EXPECT_FALSE(sc.hasFluid(0, 0, 0));
}

TEST(SubChunkFluidTest, SetFluidCreatesLayer) {
    SubChunk sc;
    FluidTypeId water = FluidTypeId::fromName("water");

    bool changed = sc.setFluid(5, 3, 7, water, FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(sc.hasFluidLayer());
    EXPECT_EQ(sc.getFluid(5, 3, 7), water);
    EXPECT_EQ(sc.getFluidLevel(5, 3, 7), FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(sc.hasFluid(5, 3, 7));
    EXPECT_GT(sc.fluidVersion(), 0u);
}

TEST(SubChunkFluidTest, RemoveFluidDeallocatesEmptyLayer) {
    SubChunk sc;
    FluidTypeId water = FluidTypeId::fromName("water");

    sc.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(sc.hasFluidLayer());

    sc.removeFluid(0, 0, 0);
    // Layer auto-deallocated when empty
    EXPECT_FALSE(sc.hasFluidLayer());
}

TEST(SubChunkFluidTest, SetEmptyTypeNoOpWithoutLayer) {
    SubChunk sc;
    bool changed = sc.setFluid(0, 0, 0, EMPTY_FLUID_TYPE, 0);
    EXPECT_FALSE(changed);
    EXPECT_FALSE(sc.hasFluidLayer());
}

TEST(SubChunkFluidTest, RemoveFluidNoLayerReturnsFalse) {
    SubChunk sc;
    bool removed = sc.removeFluid(0, 0, 0);
    EXPECT_FALSE(removed);
}

TEST(SubChunkFluidTest, FluidCoexistsWithBlock) {
    SubChunk sc;
    FluidTypeId water = FluidTypeId::fromName("water");
    BlockTypeId stone = BlockTypeId::fromName("finevox:stone");

    sc.setBlock(5, 3, 7, stone);
    sc.setFluid(5, 3, 7, water, FLUID_SOURCE_LEVEL);

    // Both exist at the same position
    EXPECT_EQ(sc.getBlock(5, 3, 7), stone);
    EXPECT_EQ(sc.getFluid(5, 3, 7), water);
    EXPECT_EQ(sc.getFluidLevel(5, 3, 7), FLUID_SOURCE_LEVEL);
}

TEST(SubChunkFluidTest, GetOrCreateFluidLayer) {
    SubChunk sc;
    EXPECT_FALSE(sc.hasFluidLayer());

    FluidLayer& layer = sc.getOrCreateFluidLayer();
    EXPECT_TRUE(sc.hasFluidLayer());
    EXPECT_TRUE(layer.isEmpty());
}

TEST(SubChunkFluidTest, RemoveFluidLayer) {
    SubChunk sc;
    sc.getOrCreateFluidLayer();
    EXPECT_TRUE(sc.hasFluidLayer());

    sc.removeFluidLayer();
    EXPECT_FALSE(sc.hasFluidLayer());
}

TEST(SubChunkFluidTest, SetFluidByIndex) {
    SubChunk sc;
    FluidTypeId water = FluidTypeId::fromName("water");

    int32_t index = 3*256 + 7*16 + 5;  // y=3, z=7, x=5
    bool changed = sc.setFluid(index, water, 10);
    EXPECT_TRUE(changed);
    EXPECT_EQ(sc.getFluid(index), water);
    EXPECT_EQ(sc.getFluidLevel(index), 10);
}

// ============================================================================
// World Fluid Access Tests
// ============================================================================

TEST(WorldFluidTest, GetFluidFromEmptyWorld) {
    World world;
    BlockCoord pos{10, 20, 30};

    EXPECT_EQ(world.getFluid(pos), EMPTY_FLUID_TYPE);
    EXPECT_EQ(world.getFluidLevel(pos), 0);
    EXPECT_FALSE(world.hasFluid(pos));
}

TEST(WorldFluidTest, SetAndGetFluid) {
    World world;
    FluidTypeId water = FluidTypeId::fromName("water");
    BlockCoord pos{10, 20, 30};

    bool changed = world.setFluid(pos, water, FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(changed);
    EXPECT_EQ(world.getFluid(pos), water);
    EXPECT_EQ(world.getFluidLevel(pos), FLUID_SOURCE_LEVEL);
    EXPECT_TRUE(world.hasFluid(pos));
}

TEST(WorldFluidTest, RemoveFluid) {
    World world;
    FluidTypeId water = FluidTypeId::fromName("water");
    BlockCoord pos{10, 20, 30};

    world.setFluid(pos, water, FLUID_SOURCE_LEVEL);
    bool removed = world.removeFluid(pos);
    EXPECT_TRUE(removed);
    EXPECT_FALSE(world.hasFluid(pos));
}

TEST(WorldFluidTest, RemoveFluidFromUnloadedChunk) {
    World world;
    BlockCoord pos{10, 20, 30};
    bool removed = world.removeFluid(pos);
    EXPECT_FALSE(removed);
}

TEST(WorldFluidTest, FluidAndBlockCoexist) {
    World world;
    FluidTypeId water = FluidTypeId::fromName("water");
    BlockTypeId stone = BlockTypeId::fromName("finevox:stone");
    BlockCoord pos{10, 20, 30};

    world.setBlock(pos, stone);
    world.setFluid(pos, water, FLUID_SOURCE_LEVEL);

    EXPECT_EQ(world.getBlock(pos), stone);
    EXPECT_EQ(world.getFluid(pos), water);
}

TEST(WorldFluidTest, NegativeCoordinates) {
    World world;
    FluidTypeId water = FluidTypeId::fromName("water");
    BlockCoord pos{-10, 20, -30};

    world.setFluid(pos, water, 7);
    EXPECT_EQ(world.getFluid(pos), water);
    EXPECT_EQ(world.getFluidLevel(pos), 7);
}
