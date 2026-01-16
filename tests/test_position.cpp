#include <gtest/gtest.h>
#include "finevox/position.hpp"
#include <unordered_set>

using namespace finevox;

// ============================================================================
// Face tests
// ============================================================================

TEST(FaceTest, OppositeFace) {
    EXPECT_EQ(oppositeFace(Face::NegX), Face::PosX);
    EXPECT_EQ(oppositeFace(Face::PosX), Face::NegX);
    EXPECT_EQ(oppositeFace(Face::NegY), Face::PosY);
    EXPECT_EQ(oppositeFace(Face::PosY), Face::NegY);
    EXPECT_EQ(oppositeFace(Face::NegZ), Face::PosZ);
    EXPECT_EQ(oppositeFace(Face::PosZ), Face::NegZ);
}

TEST(FaceTest, FaceNormals) {
    EXPECT_EQ(faceNormal(Face::NegX), (std::array<int32_t, 3>{-1, 0, 0}));
    EXPECT_EQ(faceNormal(Face::PosX), (std::array<int32_t, 3>{ 1, 0, 0}));
    EXPECT_EQ(faceNormal(Face::NegY), (std::array<int32_t, 3>{ 0,-1, 0}));
    EXPECT_EQ(faceNormal(Face::PosY), (std::array<int32_t, 3>{ 0, 1, 0}));
    EXPECT_EQ(faceNormal(Face::NegZ), (std::array<int32_t, 3>{ 0, 0,-1}));
    EXPECT_EQ(faceNormal(Face::PosZ), (std::array<int32_t, 3>{ 0, 0, 1}));
}

// ============================================================================
// BlockPos tests
// ============================================================================

TEST(BlockPosTest, DefaultConstruction) {
    BlockPos pos;
    EXPECT_EQ(pos.x, 0);
    EXPECT_EQ(pos.y, 0);
    EXPECT_EQ(pos.z, 0);
}

TEST(BlockPosTest, Construction) {
    BlockPos pos(10, 64, -30);
    EXPECT_EQ(pos.x, 10);
    EXPECT_EQ(pos.y, 64);
    EXPECT_EQ(pos.z, -30);
}

TEST(BlockPosTest, PackUnpackRoundTrip) {
    // Test various positions including edge cases
    // Layout: [x:26][y:12][z:26]
    // X, Z range: +/- 33,554,432
    // Y range: +/- 2,048
    std::vector<BlockPos> positions = {
        {0, 0, 0},
        {1, 2, 3},
        {-1, -2, -3},
        {100, 64, 200},
        {-100, -64, -200},
        {1000000, 0, -1000000},      // Large X/Z
        {30000000, 2000, -30000000}, // Near X/Z limits, large Y
        {0, -2047, 0},               // Near Y min
        {0, 2047, 0},                // Near Y max
    };

    for (const auto& original : positions) {
        uint64_t packed = original.pack();
        BlockPos unpacked = BlockPos::unpack(packed);
        EXPECT_EQ(unpacked, original) << "Failed for pos: "
            << original.x << ", " << original.y << ", " << original.z;
    }
}

TEST(BlockPosTest, Neighbor) {
    BlockPos pos(10, 20, 30);

    EXPECT_EQ(pos.neighbor(Face::NegX), BlockPos(9, 20, 30));
    EXPECT_EQ(pos.neighbor(Face::PosX), BlockPos(11, 20, 30));
    EXPECT_EQ(pos.neighbor(Face::NegY), BlockPos(10, 19, 30));
    EXPECT_EQ(pos.neighbor(Face::PosY), BlockPos(10, 21, 30));
    EXPECT_EQ(pos.neighbor(Face::NegZ), BlockPos(10, 20, 29));
    EXPECT_EQ(pos.neighbor(Face::PosZ), BlockPos(10, 20, 31));
}

TEST(BlockPosTest, LocalCoordinates) {
    // Positive coordinates
    BlockPos pos(35, 67, 49);
    EXPECT_EQ(pos.localX(), 3);   // 35 % 16 = 3
    EXPECT_EQ(pos.localY(), 3);   // 67 % 16 = 3
    EXPECT_EQ(pos.localZ(), 1);   // 49 % 16 = 1

    // Negative coordinates - should still give 0-15
    BlockPos negPos(-1, -1, -1);
    EXPECT_EQ(negPos.localX(), 15);  // -1 & 0xF = 15
    EXPECT_EQ(negPos.localY(), 15);
    EXPECT_EQ(negPos.localZ(), 15);
}

TEST(BlockPosTest, LocalIndex) {
    // Index layout: y*256 + z*16 + x
    BlockPos pos(3, 5, 7);
    EXPECT_EQ(pos.toLocalIndex(), 5*256 + 7*16 + 3);

    // Corner cases
    BlockPos origin(0, 0, 0);
    EXPECT_EQ(origin.toLocalIndex(), 0);

    BlockPos max(15, 15, 15);
    EXPECT_EQ(max.toLocalIndex(), 15*256 + 15*16 + 15);
}

TEST(BlockPosTest, FromLocalIndex) {
    // Chunk at (2, 4, 6), local index corresponds to (3, 5, 7)
    int32_t index = 5*256 + 7*16 + 3;
    BlockPos pos = BlockPos::fromLocalIndex(2, 4, 6, index);

    EXPECT_EQ(pos.x, 2*16 + 3);  // 35
    EXPECT_EQ(pos.y, 4*16 + 5);  // 69
    EXPECT_EQ(pos.z, 6*16 + 7);  // 103
}

TEST(BlockPosTest, HashableInUnorderedSet) {
    std::unordered_set<BlockPos> positions;
    positions.insert({0, 0, 0});
    positions.insert({1, 2, 3});
    positions.insert({-1, -2, -3});

    EXPECT_EQ(positions.size(), 3);
    EXPECT_TRUE(positions.contains({0, 0, 0}));
    EXPECT_TRUE(positions.contains({1, 2, 3}));
    EXPECT_FALSE(positions.contains({4, 5, 6}));
}

TEST(BlockPosTest, Comparison) {
    BlockPos a(1, 2, 3);
    BlockPos b(1, 2, 3);
    BlockPos c(1, 2, 4);

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a < c);
}

// ============================================================================
// ChunkPos tests
// ============================================================================

TEST(ChunkPosTest, FromBlock) {
    // Positive blocks
    EXPECT_EQ(ChunkPos::fromBlock({0, 0, 0}), ChunkPos(0, 0, 0));
    EXPECT_EQ(ChunkPos::fromBlock({15, 15, 15}), ChunkPos(0, 0, 0));
    EXPECT_EQ(ChunkPos::fromBlock({16, 32, 48}), ChunkPos(1, 2, 3));

    // Negative blocks - important edge case
    EXPECT_EQ(ChunkPos::fromBlock({-1, -1, -1}), ChunkPos(-1, -1, -1));
    EXPECT_EQ(ChunkPos::fromBlock({-16, -16, -16}), ChunkPos(-1, -1, -1));
    EXPECT_EQ(ChunkPos::fromBlock({-17, -17, -17}), ChunkPos(-2, -2, -2));
}

TEST(ChunkPosTest, ToBlockPos) {
    ChunkPos chunk(2, 3, 4);
    BlockPos block = chunk.toBlockPos();

    EXPECT_EQ(block.x, 32);
    EXPECT_EQ(block.y, 48);
    EXPECT_EQ(block.z, 64);
}

TEST(ChunkPosTest, PackUnpackRoundTrip) {
    std::vector<ChunkPos> positions = {
        {0, 0, 0},
        {1, 2, 3},
        {-1, -2, -3},
        {1000, 64, -1000},
    };

    for (const auto& original : positions) {
        uint64_t packed = original.pack();
        ChunkPos unpacked = ChunkPos::unpack(packed);
        EXPECT_EQ(unpacked, original);
    }
}

TEST(ChunkPosTest, Neighbor) {
    ChunkPos pos(5, 10, 15);
    EXPECT_EQ(pos.neighbor(Face::PosX), ChunkPos(6, 10, 15));
    EXPECT_EQ(pos.neighbor(Face::NegY), ChunkPos(5, 9, 15));
}

// ============================================================================
// ColumnPos tests
// ============================================================================

TEST(ColumnPosTest, FromBlock) {
    EXPECT_EQ(ColumnPos::fromBlock({35, 100, 67}), ColumnPos(2, 4));
    EXPECT_EQ(ColumnPos::fromBlock({-1, 0, -1}), ColumnPos(-1, -1));
}

TEST(ColumnPosTest, FromChunk) {
    EXPECT_EQ(ColumnPos::fromChunk({5, 10, 15}), ColumnPos(5, 15));
}

TEST(ColumnPosTest, PackUnpackRoundTrip) {
    std::vector<ColumnPos> positions = {
        {0, 0},
        {1000, -1000},
        {-500000, 500000},
        {INT32_MAX, INT32_MIN},  // Extreme values
    };

    for (const auto& original : positions) {
        uint64_t packed = original.pack();
        ColumnPos unpacked = ColumnPos::unpack(packed);
        EXPECT_EQ(unpacked, original);
    }
}
