#include <gtest/gtest.h>
#include "finevox/mesh.hpp"
#include "finevox/subchunk.hpp"
#include "finevox/world.hpp"
#include <cmath>

using namespace finevox;

// ============================================================================
// Test fixtures and helpers
// ============================================================================

class MeshTest : public ::testing::Test {
protected:
    MeshBuilder builder;

    void SetUp() override {
        // Disable greedy meshing by default for tests that expect specific vertex counts
        builder.setGreedyMeshing(false);
    }

    // Simple texture provider that returns unit UVs
    BlockTextureProvider simpleTextureProvider = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };

    // Opaque provider that always returns false (nothing is opaque = all faces visible)
    BlockOpaqueProvider nothingOpaque = [](const BlockPos&) {
        return false;
    };

    // Opaque provider that always returns true (everything opaque = no faces visible)
    BlockOpaqueProvider everythingOpaque = [](const BlockPos&) {
        return true;
    };
};

// ============================================================================
// ChunkVertex tests
// ============================================================================

TEST(ChunkVertexTest, DefaultConstruction) {
    ChunkVertex v;
    // Default constructor exists and can be called
    // Values are uninitialized (using = default), so we don't check specific values
    (void)v;  // Suppress unused variable warning
    SUCCEED();  // Just verify it compiles and doesn't crash
}

TEST(ChunkVertexTest, ParameterizedConstruction) {
    ChunkVertex v(
        glm::vec3(1.0f, 2.0f, 3.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        glm::vec2(0.5f, 0.5f),
        0.75f
    );

    EXPECT_EQ(v.position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(v.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(v.texCoord, glm::vec2(0.5f, 0.5f));
    EXPECT_FLOAT_EQ(v.ao, 0.75f);
}

TEST(ChunkVertexTest, Equality) {
    ChunkVertex v1(glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), glm::vec2(0, 0), 1.0f);
    ChunkVertex v2(glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), glm::vec2(0, 0), 1.0f);
    ChunkVertex v3(glm::vec3(1, 2, 3), glm::vec3(0, 1, 0), glm::vec2(0, 0), 0.5f);

    EXPECT_EQ(v1, v2);
    EXPECT_NE(v1, v3);
}

// ============================================================================
// MeshData tests
// ============================================================================

TEST(MeshDataTest, EmptyByDefault) {
    MeshData mesh;
    EXPECT_TRUE(mesh.isEmpty());
    EXPECT_EQ(mesh.vertexCount(), 0);
    EXPECT_EQ(mesh.indexCount(), 0);
    EXPECT_EQ(mesh.triangleCount(), 0);
    EXPECT_EQ(mesh.memoryUsage(), 0);
}

TEST(MeshDataTest, ReserveSpace) {
    MeshData mesh;
    mesh.reserve(100, 150);

    // Capacity should be at least what we requested
    EXPECT_GE(mesh.vertices.capacity(), 100);
    EXPECT_GE(mesh.indices.capacity(), 150);

    // But size should still be 0
    EXPECT_TRUE(mesh.isEmpty());
}

TEST(MeshDataTest, Clear) {
    MeshData mesh;
    mesh.vertices.push_back(ChunkVertex());
    mesh.indices.push_back(0);

    EXPECT_FALSE(mesh.isEmpty());

    mesh.clear();
    EXPECT_TRUE(mesh.isEmpty());
}

TEST(MeshDataTest, MemoryUsage) {
    MeshData mesh;

    // Add some data
    for (int i = 0; i < 4; ++i) {
        mesh.vertices.push_back(ChunkVertex());
    }
    for (int i = 0; i < 6; ++i) {
        mesh.indices.push_back(i);
    }

    size_t expected = 4 * sizeof(ChunkVertex) + 6 * sizeof(uint32_t);
    EXPECT_EQ(mesh.memoryUsage(), expected);
}

// ============================================================================
// MeshBuilder tests - Empty subchunk
// ============================================================================

TEST_F(MeshTest, EmptySubchunkGeneratesEmptyMesh) {
    SubChunk subChunk;  // Default is all air
    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    EXPECT_TRUE(mesh.isEmpty());
}

// ============================================================================
// MeshBuilder tests - Single block
// ============================================================================

TEST_F(MeshTest, SingleBlockGenerates6Faces) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);  // Block in center

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24);
    // 6 faces * 6 indices (2 triangles each) = 36 indices
    EXPECT_EQ(mesh.indexCount(), 36);
    EXPECT_EQ(mesh.triangleCount(), 12);
}

TEST_F(MeshTest, SingleBlockAllNeighborsOpaque) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    // All neighbors are opaque, so no faces should be rendered
    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, everythingOpaque, simpleTextureProvider);

    EXPECT_TRUE(mesh.isEmpty());
}

// ============================================================================
// MeshBuilder tests - Face culling
// ============================================================================

TEST_F(MeshTest, TwoAdjacentBlocksCullSharedFace) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);
    subChunk.setBlock(9, 8, 8, stone);  // +X neighbor

    ChunkPos pos{0, 0, 0};

    // Opaque provider that checks actual blocks in subchunk
    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        // Convert world pos to local
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;  // Outside subchunk = not opaque
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // 2 blocks, but they share 2 faces (block1's +X and block2's -X)
    // So we should have: 2 * 6 - 2 = 10 faces
    // 10 faces * 4 vertices = 40 vertices
    EXPECT_EQ(mesh.vertexCount(), 40);
    EXPECT_EQ(mesh.indexCount(), 60);  // 10 faces * 6 indices
}

// ============================================================================
// MeshBuilder tests - Vertex positions
// ============================================================================

TEST_F(MeshTest, VertexPositionsCorrectForBlockAtOrigin) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(0, 0, 0, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // All vertices should be within [0, 1] cube
    for (const auto& vertex : mesh.vertices) {
        EXPECT_GE(vertex.position.x, 0.0f);
        EXPECT_LE(vertex.position.x, 1.0f);
        EXPECT_GE(vertex.position.y, 0.0f);
        EXPECT_LE(vertex.position.y, 1.0f);
        EXPECT_GE(vertex.position.z, 0.0f);
        EXPECT_LE(vertex.position.z, 1.0f);
    }
}

TEST_F(MeshTest, VertexPositionsCorrectForBlockAtOffset) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(5, 7, 9, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // All vertices should be within [5,7,9] to [6,8,10] cube
    for (const auto& vertex : mesh.vertices) {
        EXPECT_GE(vertex.position.x, 5.0f);
        EXPECT_LE(vertex.position.x, 6.0f);
        EXPECT_GE(vertex.position.y, 7.0f);
        EXPECT_LE(vertex.position.y, 8.0f);
        EXPECT_GE(vertex.position.z, 9.0f);
        EXPECT_LE(vertex.position.z, 10.0f);
    }
}

// ============================================================================
// MeshBuilder tests - Normals
// ============================================================================

TEST_F(MeshTest, NormalsAreUnitVectors) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    for (const auto& vertex : mesh.vertices) {
        float length = glm::length(vertex.normal);
        EXPECT_NEAR(length, 1.0f, 0.0001f);
    }
}

TEST_F(MeshTest, NormalsPointOutward) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // Count normals in each direction
    int posX = 0, negX = 0, posY = 0, negY = 0, posZ = 0, negZ = 0;

    for (const auto& vertex : mesh.vertices) {
        if (vertex.normal.x > 0.5f) posX++;
        else if (vertex.normal.x < -0.5f) negX++;
        else if (vertex.normal.y > 0.5f) posY++;
        else if (vertex.normal.y < -0.5f) negY++;
        else if (vertex.normal.z > 0.5f) posZ++;
        else if (vertex.normal.z < -0.5f) negZ++;
    }

    // Each face has 4 vertices with same normal
    EXPECT_EQ(posX, 4);
    EXPECT_EQ(negX, 4);
    EXPECT_EQ(posY, 4);
    EXPECT_EQ(negY, 4);
    EXPECT_EQ(posZ, 4);
    EXPECT_EQ(negZ, 4);
}

// ============================================================================
// MeshBuilder tests - Texture coordinates
// ============================================================================

TEST_F(MeshTest, TextureCoordsInBounds) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    for (const auto& vertex : mesh.vertices) {
        EXPECT_GE(vertex.texCoord.x, 0.0f);
        EXPECT_LE(vertex.texCoord.x, 1.0f);
        EXPECT_GE(vertex.texCoord.y, 0.0f);
        EXPECT_LE(vertex.texCoord.y, 1.0f);
    }
}

TEST_F(MeshTest, TextureProviderValuesUsed) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    // Custom texture provider that returns a specific UV range
    BlockTextureProvider customTexture = [](BlockTypeId, Face) {
        return glm::vec4(0.25f, 0.5f, 0.75f, 1.0f);  // minU, minV, maxU, maxV
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, customTexture);

    // All UV coords should be within the specified range
    for (const auto& vertex : mesh.vertices) {
        EXPECT_GE(vertex.texCoord.x, 0.25f - 0.001f);
        EXPECT_LE(vertex.texCoord.x, 0.75f + 0.001f);
        EXPECT_GE(vertex.texCoord.y, 0.5f - 0.001f);
        EXPECT_LE(vertex.texCoord.y, 1.0f + 0.001f);
    }
}

// ============================================================================
// MeshBuilder tests - Ambient Occlusion
// ============================================================================

TEST_F(MeshTest, AOValuesWithNoOccluders) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // With no occluders, all AO values should be 1.0 (fully lit)
    for (const auto& vertex : mesh.vertices) {
        EXPECT_FLOAT_EQ(vertex.ao, 1.0f);
    }
}

TEST_F(MeshTest, AODisabled) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    builder.setCalculateAO(false);
    EXPECT_FALSE(builder.calculateAO());

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // With AO disabled, all values should be 1.0
    for (const auto& vertex : mesh.vertices) {
        EXPECT_FLOAT_EQ(vertex.ao, 1.0f);
    }

    builder.setCalculateAO(true);  // Reset
}

TEST_F(MeshTest, AOValuesWithOccluders) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Place a block at (8,8,8) with occluders at corners
    // AO checks blocks around each face, not just adjacent blocks
    // To get AO shadows, we need blocks at diagonal positions from a face
    subChunk.setBlock(8, 8, 8, stone);
    // Place blocks that will shadow the corners of the +Y face
    subChunk.setBlock(9, 9, 8, stone);  // Diagonally up and to the side
    subChunk.setBlock(8, 9, 9, stone);  // Diagonally up and forward
    subChunk.setBlock(9, 9, 9, stone);  // Diagonally up, side, and forward (corner)

    ChunkPos pos{0, 0, 0};

    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // We should have some AO values less than 1.0 now on the block at (8,8,8)
    // The blocks at (9,9,8), (8,9,9), (9,9,9) will shadow corners of the +Y face
    bool hasReducedAO = false;
    for (const auto& vertex : mesh.vertices) {
        // Only check vertices from the block at (8,8,8) - position range [8,9]
        if (vertex.position.x >= 8.0f && vertex.position.x <= 9.0f &&
            vertex.position.y >= 8.0f && vertex.position.y <= 9.0f &&
            vertex.position.z >= 8.0f && vertex.position.z <= 9.0f) {
            if (vertex.ao < 0.99f) {
                hasReducedAO = true;
                break;
            }
        }
    }

    EXPECT_TRUE(hasReducedAO);
}

// ============================================================================
// MeshBuilder tests - Index validity
// ============================================================================

TEST_F(MeshTest, IndicesAreValid) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Add several blocks
    subChunk.setBlock(8, 8, 8, stone);
    subChunk.setBlock(9, 8, 8, stone);
    subChunk.setBlock(8, 9, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    uint32_t maxVertex = static_cast<uint32_t>(mesh.vertices.size());

    // All indices should be within bounds
    for (uint32_t index : mesh.indices) {
        EXPECT_LT(index, maxVertex);
    }
}

TEST_F(MeshTest, IndicesFormValidTriangles) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(8, 8, 8, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // Index count should be divisible by 3 (triangles)
    EXPECT_EQ(mesh.indexCount() % 3, 0);
}

// ============================================================================
// MeshBuilder tests - World overload
// ============================================================================

TEST_F(MeshTest, WorldOverloadWorks) {
    // Create a simple world with one subchunk
    World world;

    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    world.setBlock(BlockPos(8, 8, 8), stone);

    // Get the subchunk
    ChunkPos chunkPos{0, 0, 0};
    const SubChunk* subChunk = world.getSubChunk(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    MeshData mesh = builder.buildSubChunkMesh(*subChunk, chunkPos, world, simpleTextureProvider);

    // Single block with 5 visible faces (bottom face is culled by nothing below)
    // Actually all 6 faces are visible since there's nothing around it
    EXPECT_EQ(mesh.vertexCount(), 24);  // 6 faces * 4 vertices
}

// ============================================================================
// Utility function tests
// ============================================================================

TEST(MeshUtilityTest, FaceNormalVec3) {
    EXPECT_EQ(faceNormalVec3(Face::PosX), glm::vec3(1, 0, 0));
    EXPECT_EQ(faceNormalVec3(Face::NegX), glm::vec3(-1, 0, 0));
    EXPECT_EQ(faceNormalVec3(Face::PosY), glm::vec3(0, 1, 0));
    EXPECT_EQ(faceNormalVec3(Face::NegY), glm::vec3(0, -1, 0));
    EXPECT_EQ(faceNormalVec3(Face::PosZ), glm::vec3(0, 0, 1));
    EXPECT_EQ(faceNormalVec3(Face::NegZ), glm::vec3(0, 0, -1));
}

TEST(MeshUtilityTest, FaceOffset) {
    EXPECT_EQ(faceOffset(Face::PosX), BlockPos(1, 0, 0));
    EXPECT_EQ(faceOffset(Face::NegX), BlockPos(-1, 0, 0));
    EXPECT_EQ(faceOffset(Face::PosY), BlockPos(0, 1, 0));
    EXPECT_EQ(faceOffset(Face::NegY), BlockPos(0, -1, 0));
    EXPECT_EQ(faceOffset(Face::PosZ), BlockPos(0, 0, 1));
    EXPECT_EQ(faceOffset(Face::NegZ), BlockPos(0, 0, -1));
}

// ============================================================================
// Performance/stress test
// ============================================================================

TEST_F(MeshTest, FullSubchunkMeshing) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Fill entire subchunk with blocks
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                subChunk.setBlock(x, y, z, stone);
            }
        }
    }

    EXPECT_EQ(subChunk.nonAirCount(), 16 * 16 * 16);

    ChunkPos pos{0, 0, 0};

    // Use opaque provider that checks actual block contents for internal face culling
    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;  // Outside subchunk = not opaque (outer faces visible)
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // With internal culling, only the outer faces are visible
    // Each of the 6 sides of the 16x16x16 cube has 16x16 = 256 faces
    // 6 * 256 = 1536 faces
    // 1536 * 4 vertices = 6144 vertices
    EXPECT_EQ(mesh.vertexCount(), 6144);
    EXPECT_EQ(mesh.indexCount(), 1536 * 6);  // 9216 indices
}

TEST_F(MeshTest, CheckerboardPattern) {
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Checkerboard pattern - maximum internal face exposure
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if ((x + y + z) % 2 == 0) {
                    subChunk.setBlock(x, y, z, stone);
                }
            }
        }
    }

    ChunkPos pos{0, 0, 0};

    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // With checkerboard, every block has all 6 faces visible
    // 2048 blocks * 6 faces * 4 vertices = 49152 vertices
    EXPECT_EQ(mesh.vertexCount(), 2048 * 6 * 4);
}

// ============================================================================
// Greedy Meshing tests
// ============================================================================

class GreedyMeshTest : public ::testing::Test {
protected:
    MeshBuilder builder;

    void SetUp() override {
        // Enable greedy meshing for these tests
        builder.setGreedyMeshing(true);
    }

    // Simple texture provider that returns unit UVs
    BlockTextureProvider simpleTextureProvider = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };

    // Opaque provider that always returns false (nothing is opaque = all faces visible)
    BlockOpaqueProvider nothingOpaque = [](const BlockPos&) {
        return false;
    };
};

TEST_F(GreedyMeshTest, SingleBlockSameAsSimple) {
    // A single block should produce the same result with or without greedy meshing
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    subChunk.setBlock(5, 5, 5, stone);

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // Single block = 6 faces, 4 vertices each = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24);
    EXPECT_EQ(mesh.indexCount(), 36);  // 6 faces * 6 indices
}

TEST_F(GreedyMeshTest, FullSubchunkReducesToSixFaces) {
    // A completely solid 16x16x16 subchunk should reduce to just 6 large quads
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                subChunk.setBlock(x, y, z, stone);
            }
        }
    }

    ChunkPos pos{0, 0, 0};

    // Opaque provider that considers blocks inside the subchunk as opaque
    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;  // Outside subchunk = not opaque (outer faces visible)
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // With greedy meshing, only 6 faces should be generated (one per side)
    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertexCount(), 24);
    EXPECT_EQ(mesh.indexCount(), 36);
}

TEST_F(GreedyMeshTest, TwoBlockTypesDontMerge) {
    // Adjacent blocks of different types should not be merged
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");
    BlockTypeId dirt = BlockTypeId::fromName("minecraft:dirt");

    // Fill bottom half with stone, top half with dirt
    for (int y = 0; y < 8; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                subChunk.setBlock(x, y, z, stone);
            }
        }
    }
    for (int y = 8; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                subChunk.setBlock(x, y, z, dirt);
            }
        }
    }

    ChunkPos pos{0, 0, 0};

    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // Expected faces:
    // - Bottom: 1 large quad (stone)
    // - Top: 1 large quad (dirt)
    // - 4 sides: each split into 2 quads (stone bottom, dirt top)
    // Total: 2 + 4*2 = 10 quads = 40 vertices
    EXPECT_EQ(mesh.vertexCount(), 40);
    EXPECT_EQ(mesh.indexCount(), 60);
}

TEST_F(GreedyMeshTest, ReducesVertexCount) {
    // Verify that greedy meshing produces fewer vertices than simple meshing
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Create a 4x4x4 solid cube
    for (int y = 0; y < 4; ++y) {
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                subChunk.setBlock(x, y, z, stone);
            }
        }
    }

    ChunkPos pos{0, 0, 0};

    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    // Greedy mesh
    MeshData greedyMesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // Simple mesh
    builder.setGreedyMeshing(false);
    MeshData simpleMesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // Simple mesh: 6 sides * 4*4 faces = 96 faces * 4 verts = 384 vertices
    EXPECT_EQ(simpleMesh.vertexCount(), 384);

    // Greedy mesh: 6 faces (one large quad per side) = 24 vertices
    EXPECT_EQ(greedyMesh.vertexCount(), 24);

    // Greedy should use significantly fewer vertices
    EXPECT_LT(greedyMesh.vertexCount(), simpleMesh.vertexCount());
}

TEST_F(GreedyMeshTest, ValidTriangles) {
    // Verify that greedy meshing produces valid triangles
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Create a 4x4x4 solid cube
    for (int y = 0; y < 4; ++y) {
        for (int z = 0; z < 4; ++z) {
            for (int x = 0; x < 4; ++x) {
                subChunk.setBlock(x, y, z, stone);
            }
        }
    }

    ChunkPos pos{0, 0, 0};

    BlockOpaqueProvider checkBlocks = [&subChunk, &pos](const BlockPos& bpos) {
        int lx = bpos.x - pos.x * 16;
        int ly = bpos.y - pos.y * 16;
        int lz = bpos.z - pos.z * 16;

        if (lx < 0 || lx >= 16 || ly < 0 || ly >= 16 || lz < 0 || lz >= 16) {
            return false;
        }

        return subChunk.getBlock(lx, ly, lz) != AIR_BLOCK_TYPE;
    };

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, checkBlocks, simpleTextureProvider);

    // All indices should be valid
    for (size_t i = 0; i < mesh.indices.size(); ++i) {
        EXPECT_LT(mesh.indices[i], mesh.vertices.size())
            << "Invalid index at position " << i;
    }

    // Number of indices should be divisible by 3 (triangles)
    EXPECT_EQ(mesh.indices.size() % 3, 0);

    // Each triangle should have non-zero area (vertices not all the same)
    for (size_t i = 0; i < mesh.indices.size(); i += 3) {
        const auto& v0 = mesh.vertices[mesh.indices[i]].position;
        const auto& v1 = mesh.vertices[mesh.indices[i + 1]].position;
        const auto& v2 = mesh.vertices[mesh.indices[i + 2]].position;

        // At least one pair of vertices should differ
        bool hasArea = (v0 != v1) || (v1 != v2) || (v0 != v2);
        EXPECT_TRUE(hasArea) << "Degenerate triangle at index " << i;
    }
}

TEST_F(GreedyMeshTest, TextureTiling) {
    // Verify that texture coordinates tile correctly for merged faces
    SubChunk subChunk;
    BlockTypeId stone = BlockTypeId::fromName("minecraft:stone");

    // Create a 4x1x1 horizontal row of blocks (exposed from all sides)
    for (int x = 0; x < 4; ++x) {
        subChunk.setBlock(x, 8, 8, stone);
    }

    ChunkPos pos{0, 0, 0};

    MeshData mesh = builder.buildSubChunkMesh(subChunk, pos, nothingOpaque, simpleTextureProvider);

    // Find the top face (+Y) - it should have tiled UVs across 4 blocks
    bool foundTiledFace = false;
    for (size_t i = 0; i + 3 < mesh.vertices.size(); i += 4) {
        // Check if this is a top face (all vertices have same Y, normal pointing up)
        const auto& v0 = mesh.vertices[i];
        if (v0.normal == glm::vec3(0.0f, 1.0f, 0.0f)) {
            // This is a top face - check UV span
            float maxU = 0.0f;
            for (size_t j = 0; j < 4; ++j) {
                maxU = std::max(maxU, mesh.vertices[i + j].texCoord.x);
            }
            // If this face spans 4 blocks, max U should be 4.0 (tiled)
            if (maxU >= 3.9f) {
                foundTiledFace = true;
                break;
            }
        }
    }

    EXPECT_TRUE(foundTiledFace) << "Expected to find a face with tiled UVs spanning 4 blocks";
}
