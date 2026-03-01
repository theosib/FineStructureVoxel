#include <gtest/gtest.h>
#include "finevox/core/serialization.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/string_interner.hpp"

using namespace finevox;

// ============================================================================
// Helpers
// ============================================================================

static FluidTypeId registerTestFluid(const std::string& name) {
    FluidTypeId fid = FluidTypeId::fromName(name);
    if (!FluidRegistry::global().getType(fid)) {
        FluidType ft;
        ft.name = name;
        ft.id = fid;
        FluidRegistry::global().registerType(name, ft);
    }
    return fid;
}

// ============================================================================
// SubChunk Fluid Serialization
// ============================================================================

TEST(FluidSerialization, NoFluidNoFields) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test_fs:stone");
    chunk.setBlock(0, 0, 0, stone);

    auto serialized = SubChunkSerializer::serialize(chunk, 0);
    EXPECT_TRUE(serialized.fluidPalette.empty());
    EXPECT_TRUE(serialized.fluidData.empty());

    // Round-trip: no fluid restored
    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);
    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->hasFluidLayer());
}

TEST(FluidSerialization, SingleWaterSource) {
    FluidTypeId water = registerTestFluid("test_fs_water");

    SubChunk chunk;
    FluidLayer& fl = chunk.getOrCreateFluidLayer();
    fl.setFluid(5, 3, 7, water, FLUID_SOURCE_LEVEL);

    // Serialize
    auto serialized = SubChunkSerializer::serialize(chunk, 2);
    EXPECT_FALSE(serialized.fluidPalette.empty());
    EXPECT_EQ(serialized.fluidData.size(), FluidLayer::VOLUME);

    // Round-trip via CBOR
    auto bytes = SubChunkSerializer::toCBOR(chunk, 2);
    int32_t yLevel = -1;
    auto restored = SubChunkSerializer::fromCBOR(bytes, &yLevel);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(yLevel, 2);
    ASSERT_TRUE(restored->hasFluidLayer());

    const FluidLayer* rfl = restored->fluidLayer();
    EXPECT_FALSE(rfl->isEmpty());
    EXPECT_EQ(rfl->nonEmptyCount(), 1);

    FluidCell cell = rfl->getCell(5, 3, 7);
    EXPECT_FALSE(cell.isEmpty());
    EXPECT_EQ(cell.level, FLUID_SOURCE_LEVEL);
    EXPECT_EQ(rfl->getFluidType(5, 3, 7), water);
}

TEST(FluidSerialization, MultipleFluidTypes) {
    FluidTypeId water = registerTestFluid("test_fs_water2");
    FluidTypeId lava = registerTestFluid("test_fs_lava2");

    SubChunk chunk;
    FluidLayer& fl = chunk.getOrCreateFluidLayer();
    fl.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    fl.setFluid(1, 0, 0, lava, FLUID_SOURCE_LEVEL);
    fl.setFluid(2, 0, 0, water, 7);  // flowing water

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(restored->hasFluidLayer());

    const FluidLayer* rfl = restored->fluidLayer();
    EXPECT_EQ(rfl->nonEmptyCount(), 3);

    EXPECT_EQ(rfl->getFluidType(0, 0, 0), water);
    EXPECT_EQ(rfl->getLevel(0, 0, 0), FLUID_SOURCE_LEVEL);

    EXPECT_EQ(rfl->getFluidType(1, 0, 0), lava);
    EXPECT_EQ(rfl->getLevel(1, 0, 0), FLUID_SOURCE_LEVEL);

    EXPECT_EQ(rfl->getFluidType(2, 0, 0), water);
    EXPECT_EQ(rfl->getLevel(2, 0, 0), 7);
}

TEST(FluidSerialization, MixedBlocksAndFluid) {
    FluidTypeId water = registerTestFluid("test_fs_water3");
    BlockTypeId stone = BlockTypeId::fromName("test_fs:stone3");

    SubChunk chunk;
    chunk.setBlock(3, 3, 3, stone);
    chunk.setBlock(4, 3, 3, stone);

    FluidLayer& fl = chunk.getOrCreateFluidLayer();
    fl.setFluid(3, 3, 3, water, FLUID_SOURCE_LEVEL);  // water in stone (stairs-like)
    fl.setFluid(5, 3, 3, water, 10);                   // water in air

    auto bytes = SubChunkSerializer::toCBOR(chunk, 1);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);

    // Check blocks preserved
    EXPECT_EQ(restored->getBlock(3, 3, 3), stone);
    EXPECT_EQ(restored->getBlock(4, 3, 3), stone);

    // Check fluid preserved
    ASSERT_TRUE(restored->hasFluidLayer());
    const FluidLayer* rfl = restored->fluidLayer();
    EXPECT_EQ(rfl->getFluidType(3, 3, 3), water);
    EXPECT_EQ(rfl->getLevel(3, 3, 3), FLUID_SOURCE_LEVEL);
    EXPECT_EQ(rfl->getFluidType(5, 3, 3), water);
    EXPECT_EQ(rfl->getLevel(5, 3, 3), 10);
}

TEST(FluidSerialization, EmptyFluidLayerNotSerialized) {
    SubChunk chunk;
    FluidTypeId water = registerTestFluid("test_fs_water4");

    // Create fluid then remove it
    FluidLayer& fl = chunk.getOrCreateFluidLayer();
    fl.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    fl.removeFluid(0, 0, 0);
    EXPECT_TRUE(fl.isEmpty());

    auto serialized = SubChunkSerializer::serialize(chunk, 0);
    EXPECT_TRUE(serialized.fluidPalette.empty());
    EXPECT_TRUE(serialized.fluidData.empty());
}

// ============================================================================
// Column-level Fluid Serialization
// ============================================================================

TEST(FluidSerialization, ColumnRoundTrip) {
    FluidTypeId water = registerTestFluid("test_fs_water5");
    BlockTypeId stone = BlockTypeId::fromName("test_fs:stone5");

    ColumnPos colPos{10, 20};
    ChunkColumn column(colPos);

    // Place some blocks so the subchunk exists
    column.setBlock(5, 64, 5, stone);
    column.setBlock(5, 65, 5, stone);

    // Place fluid
    SubChunk* sc = column.getSubChunk(4);  // y=64 → subchunk 4
    ASSERT_NE(sc, nullptr);
    FluidLayer& fl = sc->getOrCreateFluidLayer();
    fl.setFluid(5, 0, 5, water, FLUID_SOURCE_LEVEL);  // local y=0 in subchunk 4 → world y=64
    fl.setFluid(5, 1, 5, water, 12);                    // world y=65

    auto bytes = ColumnSerializer::toCBOR(column, 10, 20);
    EXPECT_FALSE(bytes.empty());

    int32_t outX = -1, outZ = -1;
    auto restored = ColumnSerializer::fromCBOR(bytes, &outX, &outZ);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(outX, 10);
    EXPECT_EQ(outZ, 20);

    // Check blocks
    EXPECT_EQ(restored->getBlock(5, 64, 5), stone);
    EXPECT_EQ(restored->getBlock(5, 65, 5), stone);

    // Check fluid
    SubChunk* rsc = restored->getSubChunk(4);
    ASSERT_NE(rsc, nullptr);
    ASSERT_TRUE(rsc->hasFluidLayer());

    const FluidLayer* rfl = rsc->fluidLayer();
    EXPECT_EQ(rfl->nonEmptyCount(), 2);
    EXPECT_EQ(rfl->getFluidType(5, 0, 5), water);
    EXPECT_EQ(rfl->getLevel(5, 0, 5), FLUID_SOURCE_LEVEL);
    EXPECT_EQ(rfl->getFluidType(5, 1, 5), water);
    EXPECT_EQ(rfl->getLevel(5, 1, 5), 12);
}

TEST(FluidSerialization, IntermediateSerializeDeserialize) {
    FluidTypeId water = registerTestFluid("test_fs_water6");

    SubChunk chunk;
    FluidLayer& fl = chunk.getOrCreateFluidLayer();

    // Place several fluid cells at different levels
    fl.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);
    fl.setFluid(1, 0, 0, water, 14);
    fl.setFluid(2, 0, 0, water, 10);
    fl.setFluid(3, 0, 0, water, 5);
    fl.setFluid(4, 0, 0, water, 1);

    auto serialized = SubChunkSerializer::serialize(chunk, 0);
    auto restored = SubChunkSerializer::deserialize(serialized);

    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(restored->hasFluidLayer());

    const FluidLayer* rfl = restored->fluidLayer();
    EXPECT_EQ(rfl->nonEmptyCount(), 5);
    EXPECT_EQ(rfl->getLevel(0, 0, 0), FLUID_SOURCE_LEVEL);
    EXPECT_EQ(rfl->getLevel(1, 0, 0), 14);
    EXPECT_EQ(rfl->getLevel(2, 0, 0), 10);
    EXPECT_EQ(rfl->getLevel(3, 0, 0), 5);
    EXPECT_EQ(rfl->getLevel(4, 0, 0), 1);
}

TEST(FluidSerialization, MaxPaletteRoundTrip) {
    // Register 15 distinct fluid types (max palette capacity)
    std::vector<FluidTypeId> fluids;
    for (int i = 0; i < 15; ++i) {
        fluids.push_back(registerTestFluid("test_fs_maxpal_" + std::to_string(i)));
    }

    SubChunk chunk;
    FluidLayer& fl = chunk.getOrCreateFluidLayer();

    // Place one cell of each type
    for (int i = 0; i < 15; ++i) {
        fl.setFluid(i, 0, 0, fluids[i], FLUID_SOURCE_LEVEL);
    }
    EXPECT_EQ(fl.nonEmptyCount(), 15);
    EXPECT_EQ(fl.palette().size(), 15);

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    ASSERT_TRUE(restored->hasFluidLayer());

    const FluidLayer* rfl = restored->fluidLayer();
    EXPECT_EQ(rfl->nonEmptyCount(), 15);

    for (int i = 0; i < 15; ++i) {
        EXPECT_EQ(rfl->getFluidType(i, 0, 0), fluids[i])
            << "Mismatch at index " << i;
        EXPECT_EQ(rfl->getLevel(i, 0, 0), FLUID_SOURCE_LEVEL);
    }
}
