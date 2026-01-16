#pragma once

#include <cstdint>
#include <functional>
#include <array>

namespace finevox {

// Face enumeration for block faces and directions
enum class Face : uint8_t {
    NegX = 0,  // West  (-X)
    PosX = 1,  // East  (+X)
    NegY = 2,  // Down  (-Y)
    PosY = 3,  // Up    (+Y)
    NegZ = 4,  // North (-Z)
    PosZ = 5,  // South (+Z)
};

constexpr size_t FACE_COUNT = 6;

// Get the opposite face
constexpr Face oppositeFace(Face f) {
    return static_cast<Face>(static_cast<uint8_t>(f) ^ 1);
}

// Get face normal as integer offsets
constexpr std::array<int32_t, 3> faceNormal(Face f) {
    constexpr std::array<std::array<int32_t, 3>, 6> normals = {{
        {-1, 0, 0},  // NegX
        { 1, 0, 0},  // PosX
        { 0,-1, 0},  // NegY
        { 0, 1, 0},  // PosY
        { 0, 0,-1},  // NegZ
        { 0, 0, 1},  // PosZ
    }};
    return normals[static_cast<size_t>(f)];
}

// Block position in world coordinates
struct BlockPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    constexpr BlockPos() = default;
    constexpr BlockPos(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}

    // Pack into 64-bit value for use as map key
    // Layout: [sign bits][x:21][y:21][z:21] - supports +/- 1M blocks in each axis
    [[nodiscard]] uint64_t pack() const;
    [[nodiscard]] static BlockPos unpack(uint64_t packed);

    // Get position of neighbor in given direction
    [[nodiscard]] constexpr BlockPos neighbor(Face face) const {
        auto n = faceNormal(face);
        return {x + n[0], y + n[1], z + n[2]};
    }

    // Get local position within subchunk (0-15 for each axis)
    [[nodiscard]] constexpr int32_t localX() const { return x & 0xF; }
    [[nodiscard]] constexpr int32_t localY() const { return y & 0xF; }
    [[nodiscard]] constexpr int32_t localZ() const { return z & 0xF; }

    // Convert local coordinates to array index (Y-major: y*256 + z*16 + x)
    [[nodiscard]] constexpr int32_t toLocalIndex() const {
        return (localY() << 8) | (localZ() << 4) | localX();
    }

    // Create from local index
    [[nodiscard]] static constexpr BlockPos fromLocalIndex(int32_t chunkX, int32_t chunkY, int32_t chunkZ, int32_t index) {
        return {
            (chunkX << 4) | (index & 0xF),
            (chunkY << 4) | ((index >> 8) & 0xF),
            (chunkZ << 4) | ((index >> 4) & 0xF)
        };
    }

    constexpr bool operator==(const BlockPos& other) const = default;
    constexpr auto operator<=>(const BlockPos& other) const = default;
};

// Subchunk position (16x16x16 region)
struct ChunkPos {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;

    constexpr ChunkPos() = default;
    constexpr ChunkPos(int32_t x_, int32_t y_, int32_t z_) : x(x_), y(y_), z(z_) {}

    // Create from block position
    [[nodiscard]] static constexpr ChunkPos fromBlock(const BlockPos& block) {
        // Arithmetic right shift to handle negative coordinates correctly
        return {block.x >> 4, block.y >> 4, block.z >> 4};
    }

    // Get the block position of the corner (minimum x,y,z)
    [[nodiscard]] constexpr BlockPos toBlockPos() const {
        return {x << 4, y << 4, z << 4};
    }

    // Pack into 64-bit value
    [[nodiscard]] uint64_t pack() const;
    [[nodiscard]] static ChunkPos unpack(uint64_t packed);

    // Get neighbor chunk
    [[nodiscard]] constexpr ChunkPos neighbor(Face face) const {
        auto n = faceNormal(face);
        return {x + n[0], y + n[1], z + n[2]};
    }

    constexpr bool operator==(const ChunkPos& other) const = default;
    constexpr auto operator<=>(const ChunkPos& other) const = default;
};

// Column position (full-height column, x and z only)
struct ColumnPos {
    int32_t x = 0;
    int32_t z = 0;

    constexpr ColumnPos() = default;
    constexpr ColumnPos(int32_t x_, int32_t z_) : x(x_), z(z_) {}

    // Create from block position
    [[nodiscard]] static constexpr ColumnPos fromBlock(const BlockPos& block) {
        return {block.x >> 4, block.z >> 4};
    }

    // Create from chunk position
    [[nodiscard]] static constexpr ColumnPos fromChunk(const ChunkPos& chunk) {
        return {chunk.x, chunk.z};
    }

    // Pack into 64-bit value
    [[nodiscard]] uint64_t pack() const;
    [[nodiscard]] static ColumnPos unpack(uint64_t packed);

    constexpr bool operator==(const ColumnPos& other) const = default;
    constexpr auto operator<=>(const ColumnPos& other) const = default;
};

}  // namespace finevox

// Hash specializations for use in unordered containers
template<>
struct std::hash<finevox::BlockPos> {
    size_t operator()(const finevox::BlockPos& pos) const noexcept {
        return std::hash<uint64_t>{}(pos.pack());
    }
};

template<>
struct std::hash<finevox::ChunkPos> {
    size_t operator()(const finevox::ChunkPos& pos) const noexcept {
        return std::hash<uint64_t>{}(pos.pack());
    }
};

template<>
struct std::hash<finevox::ColumnPos> {
    size_t operator()(const finevox::ColumnPos& pos) const noexcept {
        return std::hash<uint64_t>{}(pos.pack());
    }
};
