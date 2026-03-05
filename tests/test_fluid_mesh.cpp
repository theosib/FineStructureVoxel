#include <gtest/gtest.h>
#include "finevox/core/fluid_mesh.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/subchunk.hpp"

using namespace finevox;

// ============================================================================
// Helper: register a test fluid type
// ============================================================================

static FluidTypeId ensureWaterRegistered() {
    FluidTypeId water = FluidTypeId::fromName("test_mesh_water");
    if (!FluidRegistry::global().getType(water)) {
        FluidType ft;
        ft.maxLevel = 14;
        ft.tintColor = glm::vec4(0.2f, 0.4f, 0.9f, 0.6f);
        FluidRegistry::global().registerType("test_mesh_water", ft);
    }
    return water;
}

// ============================================================================
// Surface Height Tests
// ============================================================================

TEST(FluidMeshBuilderTest, SourceHeightIs14Over16) {
    float h = FluidMeshBuilder::surfaceHeight(FLUID_SOURCE_LEVEL, 14);
    EXPECT_FLOAT_EQ(h, 14.0f / 16.0f);
}

TEST(FluidMeshBuilderTest, FlowingLevelHalfHeight) {
    // Level 7 out of maxLevel 14, scaled by source height (14/16)
    float h = FluidMeshBuilder::surfaceHeight(7, 14);
    EXPECT_FLOAT_EQ(h, 7.0f / 14.0f * (14.0f / 16.0f));
}

TEST(FluidMeshBuilderTest, FlowingLevelMinHeight) {
    // Level 1 out of maxLevel 14, scaled by source height (14/16)
    float h = FluidMeshBuilder::surfaceHeight(1, 14);
    EXPECT_FLOAT_EQ(h, 1.0f / 14.0f * (14.0f / 16.0f));
}

TEST(FluidMeshBuilderTest, FlowingLevelMaxHeight) {
    // Level 14 out of maxLevel 14 = source height (14/16), not full block
    float h = FluidMeshBuilder::surfaceHeight(14, 14);
    EXPECT_FLOAT_EQ(h, 14.0f / 16.0f);
}

// ============================================================================
// Empty Fluid Layer Tests
// ============================================================================

TEST(FluidMeshBuilderTest, EmptyFluidLayerProducesEmptyMesh) {
    SubChunk sc;
    ChunkPos pos{0, 0, 0};

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, pos, noFluid, noSolid);
    EXPECT_TRUE(mesh.isEmpty());
}

TEST(FluidMeshBuilderTest, NoFluidLayerProducesEmptyMesh) {
    SubChunk sc;
    EXPECT_FALSE(sc.hasFluidLayer());

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, noSolid);
    EXPECT_TRUE(mesh.isEmpty());
}

// ============================================================================
// Single Source Block Tests
// ============================================================================

TEST(FluidMeshBuilderTest, SingleSourceBlockProduces6Faces) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(5, 5, 5, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, noSolid);

    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24u);
    // 6 faces * 6 indices (2 triangles) = 36 indices
    EXPECT_EQ(mesh.indexCount(), 36u);
}

TEST(FluidMeshBuilderTest, SourceBlockTintColorStored) {
    FluidTypeId water = ensureWaterRegistered();
    const FluidType* ft = FluidRegistry::global().getType(water);

    SubChunk sc;
    sc.setFluid(0, 0, 0, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, noSolid);
    ASSERT_FALSE(mesh.isEmpty());

    // Check first vertex: tileBounds should be tintColor, ao should be alpha
    const auto& v = mesh.vertices[0];
    EXPECT_FLOAT_EQ(v.tileBounds.x, ft->tintColor.x);
    EXPECT_FLOAT_EQ(v.tileBounds.y, ft->tintColor.y);
    EXPECT_FLOAT_EQ(v.tileBounds.z, ft->tintColor.z);
    EXPECT_FLOAT_EQ(v.tileBounds.w, ft->tintColor.w);
    EXPECT_FLOAT_EQ(v.ao, ft->tintColor.a);
}

TEST(FluidMeshBuilderTest, SourceBlockTopFaceHeight) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(3, 3, 3, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, noSolid);
    ASSERT_FALSE(mesh.isEmpty());

    // Find a vertex with Y > block base (should be at 3 + 14/16)
    float expectedTopY = 3.0f + 14.0f / 16.0f;
    bool foundTopVertex = false;
    for (const auto& v : mesh.vertices) {
        if (v.position.y > 3.5f) {
            EXPECT_FLOAT_EQ(v.position.y, expectedTopY);
            foundTopVertex = true;
            break;
        }
    }
    EXPECT_TRUE(foundTopVertex);
}

// ============================================================================
// Face Culling Tests
// ============================================================================

TEST(FluidMeshBuilderTest, TopFaceCulledWhenSameFluidAbove) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(5, 5, 5, water, FLUID_SOURCE_LEVEL);
    sc.setFluid(5, 6, 5, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    // Provider must report fluid within the subchunk (mirrors World::getFluid behavior)
    FluidNeighborProvider fluidProvider = [&sc](BlockCoord pos) -> std::pair<FluidTypeId, uint8_t> {
        if (pos.x >= 0 && pos.x < 16 && pos.y >= 0 && pos.y < 16 && pos.z >= 0 && pos.z < 16) {
            return {sc.getFluid(pos.x, pos.y, pos.z), sc.getFluidLevel(pos.x, pos.y, pos.z)};
        }
        return {EMPTY_FLUID_TYPE, 0};
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, fluidProvider, noSolid);

    // Two source blocks stacked: each has 6 faces but shared face should be culled
    // Top face of bottom block culled (neighbor above at >= level)
    // Bottom face of top block culled (neighbor below is same fluid)
    // = 2 * 6 - 2 = 10 faces
    EXPECT_EQ(mesh.vertexCount(), 10u * 4u);
    EXPECT_EQ(mesh.indexCount(), 10u * 6u);
}

TEST(FluidMeshBuilderTest, SideFaceCulledWhenSameFluidAtSameLevel) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(5, 5, 5, water, FLUID_SOURCE_LEVEL);
    sc.setFluid(6, 5, 5, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    FluidNeighborProvider fluidProvider = [&sc](BlockCoord pos) -> std::pair<FluidTypeId, uint8_t> {
        if (pos.x >= 0 && pos.x < 16 && pos.y >= 0 && pos.y < 16 && pos.z >= 0 && pos.z < 16) {
            return {sc.getFluid(pos.x, pos.y, pos.z), sc.getFluidLevel(pos.x, pos.y, pos.z)};
        }
        return {EMPTY_FLUID_TYPE, 0};
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, fluidProvider, noSolid);

    // Two adjacent source blocks: PosX of left and NegX of right are culled
    // = 2 * 6 - 2 = 10 faces
    EXPECT_EQ(mesh.vertexCount(), 10u * 4u);
    EXPECT_EQ(mesh.indexCount(), 10u * 6u);
}

TEST(FluidMeshBuilderTest, AllFacesCulledAgainstSolidBlock) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(5, 5, 5, water, FLUID_SOURCE_LEVEL);

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    // All neighbors are solid
    BlockSolidProvider allSolid = [](BlockCoord) { return true; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, allSolid);

    // All 6 faces culled = empty mesh
    EXPECT_TRUE(mesh.isEmpty());
}

// ============================================================================
// Flowing Block Tests
// ============================================================================

TEST(FluidMeshBuilderTest, FlowingBlockHasLowerHeight) {
    FluidTypeId water = ensureWaterRegistered();

    SubChunk sc;
    sc.setFluid(5, 5, 5, water, 7);  // level 7

    FluidMeshBuilder builder;
    FluidNeighborProvider noFluid = [](BlockCoord) {
        return std::make_pair(EMPTY_FLUID_TYPE, uint8_t(0));
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, noFluid, noSolid);
    ASSERT_FALSE(mesh.isEmpty());

    // Find a vertex with Y > block base (level 7/14 scaled by source height 14/16)
    float expectedTopY = 5.0f + 7.0f / 14.0f * (14.0f / 16.0f);
    bool foundTopVertex = false;
    for (const auto& v : mesh.vertices) {
        if (v.position.y > 5.25f) {
            EXPECT_FLOAT_EQ(v.position.y, expectedTopY);
            foundTopVertex = true;
            break;
        }
    }
    EXPECT_TRUE(foundTopVertex);
}

// ============================================================================
// Cross-SubChunk Neighbor Query
// ============================================================================

TEST(FluidMeshBuilderTest, CrossSubChunkNeighborQuery) {
    FluidTypeId water = ensureWaterRegistered();

    // Place fluid at edge of subchunk (x=0)
    SubChunk sc;
    sc.setFluid(0, 5, 5, water, FLUID_SOURCE_LEVEL);

    // Provide neighbor fluid at x=-1 (outside subchunk) via provider
    FluidNeighborProvider crossChunkFluid = [&water](BlockCoord pos) -> std::pair<FluidTypeId, uint8_t> {
        if (pos.x == -1 && pos.y == 5 && pos.z == 5) {
            return {water, FLUID_SOURCE_LEVEL};
        }
        return {EMPTY_FLUID_TYPE, 0};
    };
    BlockSolidProvider noSolid = [](BlockCoord) { return false; };

    FluidMeshBuilder builder;
    MeshData mesh = builder.buildFluidMesh(sc, {0, 0, 0}, crossChunkFluid, noSolid);

    // NegX face should be culled (same fluid neighbor at same level)
    // = 5 faces
    EXPECT_EQ(mesh.vertexCount(), 5u * 4u);
    EXPECT_EQ(mesh.indexCount(), 5u * 6u);
}
