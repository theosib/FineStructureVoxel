#pragma once

#include <cstdint>
#include <functional>
#include <array>
#include <optional>

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

// ============================================================================
// LocalBlockPos - Block position within a subchunk (0-15 each axis)
// ============================================================================
//
// Distinct type from BlockPos to prevent accidental mixing of world and local
// coordinates. All conversions between LocalBlockPos and BlockPos are explicit.
//
struct LocalBlockPos {
    uint8_t x = 0;
    uint8_t y = 0;
    uint8_t z = 0;

    constexpr LocalBlockPos() = default;
    constexpr LocalBlockPos(uint8_t x_, uint8_t y_, uint8_t z_) : x(x_), y(y_), z(z_) {}

    // Construct from int32_t (masks to 0-15)
    constexpr LocalBlockPos(int32_t x_, int32_t y_, int32_t z_)
        : x(static_cast<uint8_t>(x_ & 0xF))
        , y(static_cast<uint8_t>(y_ & 0xF))
        , z(static_cast<uint8_t>(z_ & 0xF)) {}

    // Pack to 12-bit index (Y-major layout: y*256 + z*16 + x)
    [[nodiscard]] constexpr uint16_t toIndex() const {
        return static_cast<uint16_t>((y << 8) | (z << 4) | x);
    }

    // Unpack from 12-bit index
    [[nodiscard]] static constexpr LocalBlockPos fromIndex(uint16_t index) {
        return {
            static_cast<uint8_t>(index & 0xF),
            static_cast<uint8_t>((index >> 8) & 0xF),
            static_cast<uint8_t>((index >> 4) & 0xF)
        };
    }

    // Get neighbor position if within bounds (0-15), nullopt if outside
    [[nodiscard]] constexpr std::optional<LocalBlockPos> neighbor(Face face) const {
        auto n = faceNormal(face);
        int32_t nx = static_cast<int32_t>(x) + n[0];
        int32_t ny = static_cast<int32_t>(y) + n[1];
        int32_t nz = static_cast<int32_t>(z) + n[2];

        if (nx < 0 || nx > 15 || ny < 0 || ny > 15 || nz < 0 || nz > 15) {
            return std::nullopt;
        }
        return LocalBlockPos{static_cast<uint8_t>(nx), static_cast<uint8_t>(ny), static_cast<uint8_t>(nz)};
    }

    // Check if neighbor is within bounds
    [[nodiscard]] constexpr bool hasNeighbor(Face face) const {
        auto n = faceNormal(face);
        int32_t nx = static_cast<int32_t>(x) + n[0];
        int32_t ny = static_cast<int32_t>(y) + n[1];
        int32_t nz = static_cast<int32_t>(z) + n[2];
        return nx >= 0 && nx <= 15 && ny >= 0 && ny <= 15 && nz >= 0 && nz <= 15;
    }

    constexpr bool operator==(const LocalBlockPos& other) const = default;
    constexpr auto operator<=>(const LocalBlockPos& other) const = default;
};

// ============================================================================
// BlockPos - Block position in world coordinates
// ============================================================================
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

    // Get local position within subchunk as LocalBlockPos
    [[nodiscard]] constexpr LocalBlockPos local() const {
        return LocalBlockPos{
            static_cast<uint8_t>(x & 0xF),
            static_cast<uint8_t>(y & 0xF),
            static_cast<uint8_t>(z & 0xF)
        };
    }

    // Get local index within subchunk (Y-major: y*256 + z*16 + x)
    [[nodiscard]] constexpr uint16_t localIndex() const {
        return local().toIndex();
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
    [[nodiscard]] constexpr BlockPos cornerBlockPos() const {
        return {x << 4, y << 4, z << 4};
    }

    // Convert local block position to world block position
    [[nodiscard]] constexpr BlockPos toWorld(LocalBlockPos local) const {
        return {
            (x << 4) + local.x,
            (y << 4) + local.y,
            (z << 4) + local.z
        };
    }

    // Convert local block index to world block position
    [[nodiscard]] constexpr BlockPos toWorld(uint16_t localIndex) const {
        return toWorld(LocalBlockPos::fromIndex(localIndex));
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
struct std::hash<finevox::LocalBlockPos> {
    size_t operator()(const finevox::LocalBlockPos& pos) const noexcept {
        return std::hash<uint16_t>{}(pos.toIndex());
    }
};

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
