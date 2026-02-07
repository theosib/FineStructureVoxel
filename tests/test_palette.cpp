#include <gtest/gtest.h>
#include "finevox/core/palette.hpp"

using namespace finevox;

// ============================================================================
// ceilLog2 utility tests
// ============================================================================

TEST(CeilLog2Test, EdgeCases) {
    EXPECT_EQ(ceilLog2(0), 0);
    EXPECT_EQ(ceilLog2(1), 0);
}

TEST(CeilLog2Test, PowersOfTwo) {
    EXPECT_EQ(ceilLog2(2), 1);
    EXPECT_EQ(ceilLog2(4), 2);
    EXPECT_EQ(ceilLog2(8), 3);
    EXPECT_EQ(ceilLog2(16), 4);
    EXPECT_EQ(ceilLog2(256), 8);
    EXPECT_EQ(ceilLog2(65536), 16);
}

TEST(CeilLog2Test, NonPowersOfTwo) {
    EXPECT_EQ(ceilLog2(3), 2);   // Need 2 bits for 0,1,2
    EXPECT_EQ(ceilLog2(5), 3);   // Need 3 bits for 0-4
    EXPECT_EQ(ceilLog2(7), 3);
    EXPECT_EQ(ceilLog2(9), 4);
    EXPECT_EQ(ceilLog2(17), 5);
    EXPECT_EQ(ceilLog2(100), 7);
    EXPECT_EQ(ceilLog2(257), 9);
    EXPECT_EQ(ceilLog2(1000), 10);
}

// ============================================================================
// SubChunkPalette tests
// ============================================================================

TEST(PaletteTest, DefaultHasAirAtIndex0) {
    SubChunkPalette palette;
    EXPECT_EQ(palette.activeCount(), 1);
    EXPECT_EQ(palette.getGlobalId(0), AIR_BLOCK_TYPE);
}

TEST(PaletteTest, AddTypeReturnsIndex) {
    SubChunkPalette palette;
    BlockTypeId stone = BlockTypeId::fromName("palette_test:stone");

    auto index = palette.addType(stone);
    EXPECT_EQ(index, 1);  // Air is 0, stone is 1
    EXPECT_EQ(palette.activeCount(), 2);
}

TEST(PaletteTest, AddSameTypeTwiceReturnsSameIndex) {
    SubChunkPalette palette;
    BlockTypeId stone = BlockTypeId::fromName("palette_test:stone2");

    auto index1 = palette.addType(stone);
    auto index2 = palette.addType(stone);
    EXPECT_EQ(index1, index2);
    EXPECT_EQ(palette.activeCount(), 2);  // Still just air + stone
}

TEST(PaletteTest, GetGlobalIdRoundTrip) {
    SubChunkPalette palette;
    BlockTypeId dirt = BlockTypeId::fromName("palette_test:dirt");

    auto index = palette.addType(dirt);
    EXPECT_EQ(palette.getGlobalId(index), dirt);
}

TEST(PaletteTest, GetLocalIndexRoundTrip) {
    SubChunkPalette palette;
    BlockTypeId grass = BlockTypeId::fromName("palette_test:grass");

    auto index = palette.addType(grass);
    EXPECT_EQ(palette.getLocalIndex(grass), index);
}

TEST(PaletteTest, ContainsAfterAdd) {
    SubChunkPalette palette;
    BlockTypeId cobble = BlockTypeId::fromName("palette_test:cobblestone");

    EXPECT_FALSE(palette.contains(cobble));
    (void)palette.addType(cobble);
    EXPECT_TRUE(palette.contains(cobble));
}

TEST(PaletteTest, InvalidLocalIndexReturnsAir) {
    SubChunkPalette palette;
    EXPECT_EQ(palette.getGlobalId(9999), AIR_BLOCK_TYPE);
}

TEST(PaletteTest, UnknownGlobalIdReturnsInvalid) {
    SubChunkPalette palette;
    BlockTypeId unknown = BlockTypeId::fromName("palette_test:unknown");
    EXPECT_EQ(palette.getLocalIndex(unknown), SubChunkPalette::INVALID_LOCAL_INDEX);
}

// ============================================================================
// Bits for serialization tests - exact bit widths
// ============================================================================

TEST(PaletteTest, BitsForSerialization_1Type) {
    SubChunkPalette palette;
    EXPECT_EQ(palette.bitsForSerialization(), 0);  // Just air, no storage needed
}

TEST(PaletteTest, BitsForSerialization_2Types) {
    SubChunkPalette palette;
    (void)palette.addType(BlockTypeId::fromName("bfs:type1"));
    EXPECT_EQ(palette.bitsForSerialization(), 1);  // 2 types = 1 bit
}

TEST(PaletteTest, BitsForSerialization_3Types) {
    SubChunkPalette palette;
    (void)palette.addType(BlockTypeId::fromName("bfs3:a"));
    (void)palette.addType(BlockTypeId::fromName("bfs3:b"));
    EXPECT_EQ(palette.activeCount(), 3);
    EXPECT_EQ(palette.bitsForSerialization(), 2);  // 3 types = 2 bits
}

TEST(PaletteTest, BitsForSerialization_4Types) {
    SubChunkPalette palette;
    (void)palette.addType(BlockTypeId::fromName("bfs4:a"));
    (void)palette.addType(BlockTypeId::fromName("bfs4:b"));
    (void)palette.addType(BlockTypeId::fromName("bfs4:c"));
    EXPECT_EQ(palette.activeCount(), 4);
    EXPECT_EQ(palette.bitsForSerialization(), 2);  // 4 types = 2 bits
}

TEST(PaletteTest, BitsForSerialization_5Types) {
    SubChunkPalette palette;
    for (int i = 0; i < 4; ++i) {
        (void)palette.addType(BlockTypeId::fromName("bfs5:type" + std::to_string(i)));
    }
    EXPECT_EQ(palette.activeCount(), 5);
    EXPECT_EQ(palette.bitsForSerialization(), 3);  // 5 types = 3 bits
}

TEST(PaletteTest, BitsForSerialization_17Types) {
    SubChunkPalette palette;
    for (int i = 0; i < 16; ++i) {
        (void)palette.addType(BlockTypeId::fromName("bfs17:type" + std::to_string(i)));
    }
    EXPECT_EQ(palette.activeCount(), 17);
    EXPECT_EQ(palette.bitsForSerialization(), 5);  // 17 types = 5 bits
}

TEST(PaletteTest, BitsForSerialization_257Types) {
    SubChunkPalette palette;
    for (int i = 0; i < 256; ++i) {
        (void)palette.addType(BlockTypeId::fromName("bfs257:type" + std::to_string(i)));
    }
    EXPECT_EQ(palette.activeCount(), 257);
    EXPECT_EQ(palette.bitsForSerialization(), 9);  // 257 types = 9 bits
}

// ============================================================================
// RemoveType and free list tests
// ============================================================================

TEST(PaletteTest, RemoveTypeBasic) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("remove:stone");

    auto idx = palette.addType(stone);
    EXPECT_EQ(palette.activeCount(), 2);
    EXPECT_TRUE(palette.contains(stone));

    EXPECT_TRUE(palette.removeType(stone));
    EXPECT_EQ(palette.activeCount(), 1);  // Only air remains
    EXPECT_FALSE(palette.contains(stone));
}

TEST(PaletteTest, RemoveAirFails) {
    SubChunkPalette palette;
    EXPECT_FALSE(palette.removeType(AIR_BLOCK_TYPE));
    EXPECT_EQ(palette.activeCount(), 1);
}

TEST(PaletteTest, RemoveNonexistentFails) {
    SubChunkPalette palette;
    auto unknown = BlockTypeId::fromName("remove:unknown");
    EXPECT_FALSE(palette.removeType(unknown));
}

TEST(PaletteTest, FreeListReusesIndex) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("freelist:stone");
    auto dirt = BlockTypeId::fromName("freelist:dirt");

    auto stoneIdx = palette.addType(stone);  // Gets index 1
    EXPECT_EQ(stoneIdx, 1);

    palette.removeType(stone);  // Index 1 goes to free list

    auto dirtIdx = palette.addType(dirt);  // Should reuse index 1
    EXPECT_EQ(dirtIdx, 1);
}

TEST(PaletteTest, FreeListMultipleReuse) {
    SubChunkPalette palette;
    auto a = BlockTypeId::fromName("freelist2:a");
    auto b = BlockTypeId::fromName("freelist2:b");
    auto c = BlockTypeId::fromName("freelist2:c");
    auto d = BlockTypeId::fromName("freelist2:d");

    auto idxA = palette.addType(a);  // 1
    auto idxB = palette.addType(b);  // 2
    auto idxC = palette.addType(c);  // 3
    EXPECT_EQ(idxA, 1);
    EXPECT_EQ(idxB, 2);
    EXPECT_EQ(idxC, 3);

    palette.removeType(b);  // Free list: [2]
    palette.removeType(a);  // Free list: [2, 1]

    // Free list is LIFO, so next add gets 1
    auto idxD = palette.addType(d);
    EXPECT_EQ(idxD, 1);

    // Add a again, should get 2
    auto idxA2 = palette.addType(a);
    EXPECT_EQ(idxA2, 2);
}

TEST(PaletteTest, NeedsCompactionAfterRemove) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("needscompact:stone");

    EXPECT_FALSE(palette.needsCompaction());

    palette.addType(stone);
    EXPECT_FALSE(palette.needsCompaction());

    palette.removeType(stone);
    EXPECT_TRUE(palette.needsCompaction());
}

TEST(PaletteTest, MaxIndexTracking) {
    SubChunkPalette palette;
    auto a = BlockTypeId::fromName("maxidx:a");
    auto b = BlockTypeId::fromName("maxidx:b");

    EXPECT_EQ(palette.maxIndex(), 0);  // Just air

    palette.addType(a);  // Index 1
    EXPECT_EQ(palette.maxIndex(), 1);

    palette.addType(b);  // Index 2
    EXPECT_EQ(palette.maxIndex(), 2);

    // Removing doesn't decrease maxIndex (would require scan)
    palette.removeType(b);
    EXPECT_EQ(palette.maxIndex(), 2);
}

TEST(PaletteTest, RemovedSlotReturnsAir) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("removedslot:stone");

    auto idx = palette.addType(stone);
    palette.removeType(stone);

    // The slot still exists but returns air (the empty marker)
    EXPECT_EQ(palette.getGlobalId(idx), AIR_BLOCK_TYPE);
}

// ============================================================================
// Clear and compact tests
// ============================================================================

TEST(PaletteTest, ClearResetsToAirOnly) {
    SubChunkPalette palette;
    (void)palette.addType(BlockTypeId::fromName("clear:a"));
    (void)palette.addType(BlockTypeId::fromName("clear:b"));
    EXPECT_EQ(palette.activeCount(), 3);

    palette.clear();
    EXPECT_EQ(palette.activeCount(), 1);
    EXPECT_EQ(palette.getGlobalId(0), AIR_BLOCK_TYPE);
}

TEST(PaletteTest, CompactRemovesUnused) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("compact:stone");
    auto dirt = BlockTypeId::fromName("compact:dirt");
    auto grass = BlockTypeId::fromName("compact:grass");

    auto stoneIdx = palette.addType(stone);  // 1
    auto dirtIdx = palette.addType(dirt);    // 2
    auto grassIdx = palette.addType(grass);  // 3

    EXPECT_EQ(palette.activeCount(), 4);

    // Only air and stone are used
    std::vector<uint32_t> usage = {100, 50, 0, 0};  // air, stone, dirt(unused), grass(unused)
    auto mapping = palette.compact(usage);

    EXPECT_EQ(palette.activeCount(), 2);  // Air and stone
    EXPECT_EQ(mapping[0], 0);      // Air stays at 0
    EXPECT_EQ(mapping[stoneIdx], 1);  // Stone moves to 1
    EXPECT_EQ(mapping[dirtIdx], SubChunkPalette::INVALID_LOCAL_INDEX);
    EXPECT_EQ(mapping[grassIdx], SubChunkPalette::INVALID_LOCAL_INDEX);
}

TEST(PaletteTest, CompactReducesBits) {
    SubChunkPalette palette;
    // Add 5 types (including air) -> 3 bits needed
    for (int i = 0; i < 4; ++i) {
        (void)palette.addType(BlockTypeId::fromName("compactbits:type" + std::to_string(i)));
    }
    EXPECT_EQ(palette.activeCount(), 5);
    EXPECT_EQ(palette.bitsForSerialization(), 3);

    // Only use air and one other type
    std::vector<uint32_t> usage = {100, 50, 0, 0, 0};
    palette.compact(usage);

    EXPECT_EQ(palette.activeCount(), 2);  // Air + one type
    EXPECT_EQ(palette.bitsForSerialization(), 1);  // Now only 1 bit needed
}

TEST(PaletteTest, Entries) {
    SubChunkPalette palette;
    auto stone = BlockTypeId::fromName("entries:stone");
    (void)palette.addType(stone);

    auto& entries = palette.entries();
    EXPECT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[0], AIR_BLOCK_TYPE);
    EXPECT_EQ(entries[1], stone);
}

TEST(PaletteTest, CompactClearsFreeList) {
    SubChunkPalette palette;
    auto a = BlockTypeId::fromName("compactfree:a");
    auto b = BlockTypeId::fromName("compactfree:b");

    palette.addType(a);  // 1
    palette.addType(b);  // 2
    palette.removeType(a);  // Free list now has index 1

    EXPECT_TRUE(palette.needsCompaction());

    // Compact with only b used
    std::vector<uint32_t> usage = {100, 0, 50};  // air, a(removed), b(used)
    palette.compact(usage);

    EXPECT_FALSE(palette.needsCompaction());  // Free list should be cleared
    EXPECT_EQ(palette.activeCount(), 2);  // Air + b

    // Adding a new type should get a fresh index, not from free list
    auto c = BlockTypeId::fromName("compactfree:c");
    auto idxC = palette.addType(c);
    EXPECT_EQ(idxC, 2);  // Should be next contiguous index
}

TEST(PaletteTest, CompactUpdatesMaxIndex) {
    SubChunkPalette palette;
    for (int i = 0; i < 10; ++i) {
        (void)palette.addType(BlockTypeId::fromName("compactmax:type" + std::to_string(i)));
    }
    EXPECT_EQ(palette.maxIndex(), 10);

    // Only use air and first type
    std::vector<uint32_t> usage = {100, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    palette.compact(usage);

    EXPECT_EQ(palette.maxIndex(), 1);  // Now only 0 and 1
    EXPECT_EQ(palette.bitsForSerialization(), 1);
}
