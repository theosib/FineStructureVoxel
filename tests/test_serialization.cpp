#include <gtest/gtest.h>
#include "finevox/serialization.hpp"
#include "finevox/string_interner.hpp"

using namespace finevox;

// ============================================================================
// SubChunk Serialization Tests
// ============================================================================

TEST(SubChunkSerialization, EmptySubChunk) {
    SubChunk chunk;
    EXPECT_TRUE(chunk.isEmpty());

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    EXPECT_FALSE(bytes.empty());

    int32_t yLevel = -1;
    auto restored = SubChunkSerializer::fromCBOR(bytes, &yLevel);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(yLevel, 0);
    EXPECT_TRUE(restored->isEmpty());
}

TEST(SubChunkSerialization, SingleBlockType) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Fill with stone
    chunk.fill(stone);
    EXPECT_EQ(chunk.nonAirCount(), SubChunk::VOLUME);

    auto bytes = SubChunkSerializer::toCBOR(chunk, 5);

    int32_t yLevel = -1;
    auto restored = SubChunkSerializer::fromCBOR(bytes, &yLevel);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(yLevel, 5);
    EXPECT_EQ(restored->nonAirCount(), SubChunk::VOLUME);

    // Check all blocks are stone
    for (int i = 0; i < SubChunk::VOLUME; ++i) {
        EXPECT_EQ(restored->getBlock(i), stone);
    }
}

TEST(SubChunkSerialization, MultipleBlockTypes) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");
    BlockTypeId grass = BlockTypeId::fromName("test:grass");

    // Create a pattern
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                if (y < 5) {
                    chunk.setBlock(x, y, z, stone);
                } else if (y < 10) {
                    chunk.setBlock(x, y, z, dirt);
                } else if (y < 11) {
                    chunk.setBlock(x, y, z, grass);
                }
                // y >= 11 remains air
            }
        }
    }

    auto bytes = SubChunkSerializer::toCBOR(chunk, -2);

    int32_t yLevel = 0;
    auto restored = SubChunkSerializer::fromCBOR(bytes, &yLevel);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(yLevel, -2);

    // Verify pattern
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                BlockTypeId expected;
                if (y < 5) {
                    expected = stone;
                } else if (y < 10) {
                    expected = dirt;
                } else if (y < 11) {
                    expected = grass;
                } else {
                    expected = AIR_BLOCK_TYPE;
                }
                EXPECT_EQ(restored->getBlock(x, y, z), expected)
                    << "Mismatch at (" << x << ", " << y << ", " << z << ")";
            }
        }
    }
}

TEST(SubChunkSerialization, NegativeYLevel) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    chunk.setBlock(0, 0, 0, stone);

    auto bytes = SubChunkSerializer::toCBOR(chunk, -4);

    int32_t yLevel = 0;
    auto restored = SubChunkSerializer::fromCBOR(bytes, &yLevel);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(yLevel, -4);
    EXPECT_EQ(restored->getBlock(0, 0, 0), stone);
}

TEST(SubChunkSerialization, SerializedStructure) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(1, 1, 1, dirt);

    auto serialized = SubChunkSerializer::serialize(chunk, 3);

    EXPECT_EQ(serialized.yLevel, 3);
    EXPECT_GE(serialized.palette.size(), 3u);  // At least air, stone, dirt
    EXPECT_EQ(serialized.palette[0], "");  // Air at index 0
    EXPECT_EQ(serialized.blocks.size(), SubChunk::VOLUME);  // 8-bit
    EXPECT_FALSE(serialized.use16Bit);
}

TEST(SubChunkSerialization, RoundTripPreservesData) {
    SubChunk original;
    BlockTypeId types[5];
    types[0] = AIR_BLOCK_TYPE;
    types[1] = BlockTypeId::fromName("test:a");
    types[2] = BlockTypeId::fromName("test:b");
    types[3] = BlockTypeId::fromName("test:c");
    types[4] = BlockTypeId::fromName("test:d");

    // Set random-ish pattern
    for (int i = 0; i < SubChunk::VOLUME; ++i) {
        original.setBlock(i, types[(i * 7 + i / 13) % 5]);
    }

    auto bytes = SubChunkSerializer::toCBOR(original, 7);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);

    // Verify all blocks match
    for (int i = 0; i < SubChunk::VOLUME; ++i) {
        EXPECT_EQ(restored->getBlock(i), original.getBlock(i))
            << "Mismatch at index " << i;
    }
}

// ============================================================================
// ChunkColumn Serialization Tests
// ============================================================================

TEST(ChunkColumnSerialization, EmptyColumn) {
    ChunkColumn column(ColumnPos{10, 20});

    auto bytes = ColumnSerializer::toCBOR(column, 10, 20);
    EXPECT_FALSE(bytes.empty());

    int32_t x = 0, z = 0;
    auto restored = ColumnSerializer::fromCBOR(bytes, &x, &z);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(x, 10);
    EXPECT_EQ(z, 20);
    EXPECT_EQ(restored->nonAirCount(), 0);
}

TEST(ChunkColumnSerialization, SingleSubChunk) {
    ChunkColumn column(ColumnPos{0, 0});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Set some blocks in a single subchunk (y = 0-15)
    for (int y = 0; y < 16; ++y) {
        column.setBlock(0, y, 0, stone);
    }

    auto bytes = ColumnSerializer::toCBOR(column, 5, 10);

    int32_t x = 0, z = 0;
    auto restored = ColumnSerializer::fromCBOR(bytes, &x, &z);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(x, 5);
    EXPECT_EQ(z, 10);
    EXPECT_EQ(restored->nonAirCount(), 16);

    for (int y = 0; y < 16; ++y) {
        EXPECT_EQ(restored->getBlock(0, y, 0), stone);
    }
}

TEST(ChunkColumnSerialization, MultipleSubChunks) {
    ChunkColumn column(ColumnPos{0, 0});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    // Set blocks in multiple subchunks
    column.setBlock(0, 0, 0, stone);    // Subchunk y=0
    column.setBlock(0, 16, 0, dirt);    // Subchunk y=1
    column.setBlock(0, 32, 0, stone);   // Subchunk y=2
    column.setBlock(0, -16, 0, dirt);   // Subchunk y=-1

    auto bytes = ColumnSerializer::toCBOR(column, 0, 0);

    auto restored = ColumnSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->nonAirCount(), 4);

    EXPECT_EQ(restored->getBlock(0, 0, 0), stone);
    EXPECT_EQ(restored->getBlock(0, 16, 0), dirt);
    EXPECT_EQ(restored->getBlock(0, 32, 0), stone);
    EXPECT_EQ(restored->getBlock(0, -16, 0), dirt);
}

TEST(ChunkColumnSerialization, NegativeCoordinates) {
    ChunkColumn column(ColumnPos{0, 0});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    column.setBlock(0, 0, 0, stone);

    auto bytes = ColumnSerializer::toCBOR(column, -100, -200);

    int32_t x = 0, z = 0;
    auto restored = ColumnSerializer::fromCBOR(bytes, &x, &z);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(x, -100);
    EXPECT_EQ(z, -200);
}

TEST(ChunkColumnSerialization, RoundTripPreservesData) {
    ChunkColumn original(ColumnPos{42, 84});
    BlockTypeId types[4];
    types[0] = AIR_BLOCK_TYPE;
    types[1] = BlockTypeId::fromName("test:type1");
    types[2] = BlockTypeId::fromName("test:type2");
    types[3] = BlockTypeId::fromName("test:type3");

    // Create a pattern across multiple subchunks
    for (int y = -32; y < 64; ++y) {
        for (int x = 0; x < 16; x += 4) {
            for (int z = 0; z < 16; z += 4) {
                int typeIdx = ((x + z + y) % 4 + 4) % 4;
                if (typeIdx != 0) {  // Skip air
                    original.setBlock(x, y, z, types[typeIdx]);
                }
            }
        }
    }

    auto bytes = ColumnSerializer::toCBOR(original, 42, 84);

    int32_t x = 0, z = 0;
    auto restored = ColumnSerializer::fromCBOR(bytes, &x, &z);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(x, 42);
    EXPECT_EQ(z, 84);

    // Verify all blocks match
    for (int y = -32; y < 64; ++y) {
        for (int lx = 0; lx < 16; ++lx) {
            for (int lz = 0; lz < 16; ++lz) {
                EXPECT_EQ(restored->getBlock(lx, y, lz), original.getBlock(lx, y, lz))
                    << "Mismatch at (" << lx << ", " << y << ", " << lz << ")";
            }
        }
    }
}

TEST(ChunkColumnSerialization, EmptySubChunksNotSerialized) {
    ChunkColumn column(ColumnPos{0, 0});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Only put blocks in y=0 subchunk
    column.setBlock(0, 0, 0, stone);

    auto bytes1 = ColumnSerializer::toCBOR(column, 0, 0);

    // Add a block in a different subchunk (y=6, which is block y=96-111)
    column.setBlock(0, 100, 0, stone);

    auto bytes2 = ColumnSerializer::toCBOR(column, 0, 0);

    // Now we have two subchunks, should be notably larger
    // Each subchunk serializes to ~4KB (4096 bytes for blocks + overhead)
    EXPECT_GT(bytes2.size(), bytes1.size());
    EXPECT_GT(bytes2.size(), bytes1.size() + 1000);  // Should be significantly larger

    // Empty subchunks (like y=1..5 between our two non-empty ones) are not serialized
    // Verify by checking we only have 2 subchunks worth of data, not 7
    EXPECT_LT(bytes2.size(), bytes1.size() * 4);  // Not 4x (which would be 4 subchunks)
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(SerializationEdgeCases, EmptyData) {
    std::span<const uint8_t> empty;

    auto sc = SubChunkSerializer::fromCBOR(empty);
    EXPECT_EQ(sc, nullptr);

    auto col = ColumnSerializer::fromCBOR(empty);
    EXPECT_EQ(col, nullptr);
}

TEST(SerializationEdgeCases, InvalidCBOR) {
    std::vector<uint8_t> garbage = {0xFF, 0xFE, 0xFD, 0xFC};

    auto sc = SubChunkSerializer::fromCBOR(garbage);
    // Should not crash, may return nullptr or partial data

    auto col = ColumnSerializer::fromCBOR(garbage);
    // Should not crash
}

TEST(SerializationEdgeCases, ManyBlockTypes) {
    SubChunk chunk;

    // Create many different block types (but < 256 to stay in 8-bit mode)
    for (int i = 0; i < 100; ++i) {
        std::string name = "test:block" + std::to_string(i);
        BlockTypeId type = BlockTypeId::fromName(name);
        chunk.setBlock(i % 16, (i / 16) % 16, i / 256, type);
    }

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->nonAirCount(), chunk.nonAirCount());
}

TEST(SerializationEdgeCases, AllCornersSet) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Set all 8 corners
    chunk.setBlock(0, 0, 0, stone);
    chunk.setBlock(15, 0, 0, stone);
    chunk.setBlock(0, 15, 0, stone);
    chunk.setBlock(15, 15, 0, stone);
    chunk.setBlock(0, 0, 15, stone);
    chunk.setBlock(15, 0, 15, stone);
    chunk.setBlock(0, 15, 15, stone);
    chunk.setBlock(15, 15, 15, stone);

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(restored->nonAirCount(), 8);

    EXPECT_EQ(restored->getBlock(0, 0, 0), stone);
    EXPECT_EQ(restored->getBlock(15, 0, 0), stone);
    EXPECT_EQ(restored->getBlock(0, 15, 0), stone);
    EXPECT_EQ(restored->getBlock(15, 15, 0), stone);
    EXPECT_EQ(restored->getBlock(0, 0, 15), stone);
    EXPECT_EQ(restored->getBlock(15, 0, 15), stone);
    EXPECT_EQ(restored->getBlock(0, 15, 15), stone);
    EXPECT_EQ(restored->getBlock(15, 15, 15), stone);
}

// ============================================================================
// Light Data Serialization Tests
// ============================================================================

TEST(LightSerialization, DarkSubChunkNoLightData) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    chunk.setBlock(0, 0, 0, stone);

    // No light set - subchunk is dark
    EXPECT_TRUE(chunk.isLightDark());

    auto serialized = SubChunkSerializer::serialize(chunk, 0);
    EXPECT_TRUE(serialized.lightData.empty());

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->isLightDark());
}

TEST(LightSerialization, SubChunkWithLight) {
    SubChunk chunk;
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    chunk.setBlock(0, 0, 0, stone);

    // Set some light values
    chunk.setSkyLight(0, 0, 0, 15);
    chunk.setBlockLight(0, 0, 0, 10);
    chunk.setSkyLight(5, 5, 5, 8);
    chunk.setBlockLight(10, 10, 10, 14);

    EXPECT_FALSE(chunk.isLightDark());

    auto serialized = SubChunkSerializer::serialize(chunk, 0);
    EXPECT_EQ(serialized.lightData.size(), SubChunk::VOLUME);

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_FALSE(restored->isLightDark());

    // Check light values are preserved
    EXPECT_EQ(restored->getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(restored->getBlockLight(0, 0, 0), 10);
    EXPECT_EQ(restored->getSkyLight(5, 5, 5), 8);
    EXPECT_EQ(restored->getBlockLight(10, 10, 10), 14);
}

TEST(LightSerialization, FullSkyLightSubChunk) {
    SubChunk chunk;

    // Fill with full sky light
    chunk.fillSkyLight(15);
    EXPECT_TRUE(chunk.isFullSkyLight());
    EXPECT_FALSE(chunk.isLightDark());

    auto bytes = SubChunkSerializer::toCBOR(chunk, 0);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);
    EXPECT_TRUE(restored->isFullSkyLight());

    // Verify all positions have max sky light
    for (int i = 0; i < SubChunk::VOLUME; ++i) {
        EXPECT_EQ(restored->getSkyLight(i), 15);
    }
}

TEST(LightSerialization, LightRoundTrip) {
    SubChunk original;

    // Set a pattern of light values
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                uint8_t skyLight = (x + y) % 16;
                uint8_t blockLight = (z + y) % 16;
                original.setLight(x, y, z, skyLight, blockLight);
            }
        }
    }

    auto bytes = SubChunkSerializer::toCBOR(original, 3);
    auto restored = SubChunkSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);

    // Verify all light values match
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                EXPECT_EQ(restored->getSkyLight(x, y, z), original.getSkyLight(x, y, z))
                    << "Sky light mismatch at (" << x << ", " << y << ", " << z << ")";
                EXPECT_EQ(restored->getBlockLight(x, y, z), original.getBlockLight(x, y, z))
                    << "Block light mismatch at (" << x << ", " << y << ", " << z << ")";
            }
        }
    }
}

TEST(LightSerialization, ColumnWithLightData) {
    ChunkColumn column(ColumnPos{0, 0});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Set some blocks
    column.setBlock(0, 0, 0, stone);
    column.setBlock(0, 16, 0, stone);

    // Get the subchunks and set light
    SubChunk* sc0 = column.getSubChunk(0);
    SubChunk* sc1 = column.getSubChunk(1);

    ASSERT_NE(sc0, nullptr);
    ASSERT_NE(sc1, nullptr);

    sc0->setSkyLight(0, 0, 0, 15);
    sc0->setBlockLight(5, 5, 5, 10);
    sc1->fillSkyLight(12);

    auto bytes = ColumnSerializer::toCBOR(column, 0, 0);
    auto restored = ColumnSerializer::fromCBOR(bytes);

    ASSERT_NE(restored, nullptr);

    // Check light in restored subchunks
    SubChunk* rsc0 = restored->getSubChunk(0);
    SubChunk* rsc1 = restored->getSubChunk(1);

    ASSERT_NE(rsc0, nullptr);
    ASSERT_NE(rsc1, nullptr);

    EXPECT_EQ(rsc0->getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(rsc0->getBlockLight(5, 5, 5), 10);

    // sc1 was filled with sky light 12
    EXPECT_EQ(rsc1->getSkyLight(0, 0, 0), 12);
    EXPECT_EQ(rsc1->getSkyLight(15, 15, 15), 12);
}
