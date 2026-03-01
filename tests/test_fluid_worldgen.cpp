#include <gtest/gtest.h>
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/worldgen/biome_map.hpp"
#include "finevox/worldgen/biome.hpp"

using namespace finevox;
using namespace finevox::worldgen;

class FluidWorldgenTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Register water fluid type
        FluidType water;
        water.name = "water";
        water.id = FluidTypeId::fromName("water");
        water.density = 1000.0f;
        FluidRegistry::global().registerType("water", water);
        waterId_ = FluidTypeId::fromName("water");
    }

    FluidTypeId waterId_;
};

TEST_F(FluidWorldgenTest, ColumnBelowSeaLevelGetsWater) {
    const int32_t SEA_LEVEL = 62;
    const int32_t SURFACE_Y = 50;
    World world;
    ChunkColumn& col = world.getOrCreateColumn({0, 0});

    // Manually place stone up to SURFACE_Y (don't use TerrainPass which overwrites heightmap)
    BlockTypeId stoneId = BlockTypeId::fromName("stone");
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 0; y <= SURFACE_Y; ++y) {
                col.setBlock(lx, y, lz, stoneId);
            }
        }
    }

    // Build context with heightmap at SURFACE_Y (below sea level)
    BiomeMap biomeMap(42, BiomeRegistry::global());
    GenerationContext ctx{col, {0, 0}, world, biomeMap, 42};
    for (auto& h : ctx.heightmap) h = SURFACE_Y;

    FluidPass fluid(SEA_LEVEL);
    fluid.generate(ctx);

    // y=51 through y=62 should have water
    for (int32_t y = SURFACE_Y + 1; y <= SEA_LEVEL; ++y) {
        int32_t chunkY = ChunkColumn::worldYToChunkY(y);
        int32_t localY = ChunkColumn::worldYToLocalY(y);
        const SubChunk* sc = col.getSubChunk(chunkY);
        ASSERT_NE(sc, nullptr) << "SubChunk missing at chunkY=" << chunkY;
        EXPECT_EQ(sc->getFluid(0, localY, 0), waterId_) << "y=" << y;
        EXPECT_EQ(sc->getFluidLevel(0, localY, 0), 15) << "y=" << y;
    }
}

TEST_F(FluidWorldgenTest, ColumnAboveSeaLevelNoWater) {
    const int32_t SEA_LEVEL = 62;
    const int32_t SURFACE_Y = 70;
    World world;
    ChunkColumn& col = world.getOrCreateColumn({0, 0});

    // Place stone up to SURFACE_Y
    BlockTypeId stoneId = BlockTypeId::fromName("stone");
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 0; y <= SURFACE_Y; ++y) {
                col.setBlock(lx, y, lz, stoneId);
            }
        }
    }

    BiomeMap biomeMap(42, BiomeRegistry::global());
    GenerationContext ctx{col, {0, 0}, world, biomeMap, 42};
    for (auto& h : ctx.heightmap) h = SURFACE_Y;

    FluidPass fluid(SEA_LEVEL);
    fluid.generate(ctx);

    // No water should be placed anywhere
    for (int32_t chunkY = 0; chunkY <= 5; ++chunkY) {
        const SubChunk* sc = col.getSubChunk(chunkY);
        if (!sc) continue;
        if (!sc->hasFluidLayer()) continue;
        EXPECT_TRUE(sc->fluidLayer()->isEmpty()) << "chunkY=" << chunkY;
    }
}

TEST_F(FluidWorldgenTest, FluidOnlyInAirBlocks) {
    const int32_t SEA_LEVEL = 62;
    const int32_t SURFACE_Y = 50;
    World world;
    ChunkColumn& col = world.getOrCreateColumn({0, 0});

    // Place terrain
    BlockTypeId stoneId = BlockTypeId::fromName("stone");
    for (int32_t lx = 0; lx < 16; ++lx) {
        for (int32_t lz = 0; lz < 16; ++lz) {
            for (int32_t y = 0; y <= SURFACE_Y; ++y) {
                col.setBlock(lx, y, lz, stoneId);
            }
        }
    }

    // Place a solid block at y=55 (above surface but below sea level)
    col.setBlock(0, 55, 0, stoneId);

    BiomeMap biomeMap(42, BiomeRegistry::global());
    GenerationContext ctx{col, {0, 0}, world, biomeMap, 42};
    for (auto& h : ctx.heightmap) h = SURFACE_Y;

    FluidPass fluid(SEA_LEVEL);
    fluid.generate(ctx);

    // y=55 should NOT have fluid (it's a solid block)
    int32_t chunkY = ChunkColumn::worldYToChunkY(55);
    int32_t localY = ChunkColumn::worldYToLocalY(55);
    const SubChunk* sc = col.getSubChunk(chunkY);
    ASSERT_NE(sc, nullptr);
    EXPECT_TRUE(sc->getFluid(0, localY, 0).isEmpty());
}

TEST_F(FluidWorldgenTest, FluidPassRunsAfterDecoration) {
    FluidPass fluid(62);
    DecorationPass deco;
    EXPECT_GT(fluid.priority(), deco.priority());
}

TEST_F(FluidWorldgenTest, FluidPassPriority7000) {
    FluidPass fluid(62);
    EXPECT_EQ(fluid.priority(), 7000);
}
