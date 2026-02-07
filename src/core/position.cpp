#include "finevox/core/position.hpp"

namespace finevox {

// BlockPos packing: 64 bits total
// We use offset binary encoding to handle negative values
//
// Layout: [x:26][y:12][z:26]
// - X, Z: 26 bits each = +/- 33,554,432 blocks (~33M, plenty for any world)
// - Y: 12 bits = +/- 2,048 blocks (4096 total range, far more than needed)
//
// For reference, Minecraft uses Y range of -64 to 320 (384 blocks)

static constexpr int32_t XZ_BITS = 26;
static constexpr int32_t Y_BITS = 12;

static constexpr int32_t XZ_OFFSET = 1 << (XZ_BITS - 1);  // 33,554,432
static constexpr int32_t Y_OFFSET = 1 << (Y_BITS - 1);    // 2,048

static constexpr uint64_t XZ_MASK = (1ULL << XZ_BITS) - 1;
static constexpr uint64_t Y_MASK = (1ULL << Y_BITS) - 1;

uint64_t BlockPos::pack() const {
    // Add offset to make values positive, then pack
    uint64_t px = static_cast<uint64_t>(x + XZ_OFFSET) & XZ_MASK;
    uint64_t py = static_cast<uint64_t>(y + Y_OFFSET) & Y_MASK;
    uint64_t pz = static_cast<uint64_t>(z + XZ_OFFSET) & XZ_MASK;
    // Layout: [x:26][y:12][z:26] = 64 bits
    return (px << 38) | (py << 26) | pz;
}

BlockPos BlockPos::unpack(uint64_t packed) {
    int32_t px = static_cast<int32_t>((packed >> 38) & XZ_MASK) - XZ_OFFSET;
    int32_t py = static_cast<int32_t>((packed >> 26) & Y_MASK) - Y_OFFSET;
    int32_t pz = static_cast<int32_t>(packed & XZ_MASK) - XZ_OFFSET;
    return {px, py, pz};
}

// ChunkPos packing: similar scheme
// Chunks are 16 blocks, so effective range is 16x larger per unit
// Layout: [x:26][y:12][z:26]
// - X, Z: +/- 2M chunks
// - Y: +/- 128 chunks (2048 blocks vertical)

static constexpr int32_t CHUNK_XZ_BITS = 26;
static constexpr int32_t CHUNK_Y_BITS = 12;

static constexpr int32_t CHUNK_XZ_OFFSET = 1 << (CHUNK_XZ_BITS - 1);
static constexpr int32_t CHUNK_Y_OFFSET = 1 << (CHUNK_Y_BITS - 1);

static constexpr uint64_t CHUNK_XZ_MASK = (1ULL << CHUNK_XZ_BITS) - 1;
static constexpr uint64_t CHUNK_Y_MASK = (1ULL << CHUNK_Y_BITS) - 1;

uint64_t ChunkPos::pack() const {
    uint64_t px = static_cast<uint64_t>(x + CHUNK_XZ_OFFSET) & CHUNK_XZ_MASK;
    uint64_t py = static_cast<uint64_t>(y + CHUNK_Y_OFFSET) & CHUNK_Y_MASK;
    uint64_t pz = static_cast<uint64_t>(z + CHUNK_XZ_OFFSET) & CHUNK_XZ_MASK;
    return (px << 38) | (py << 26) | pz;
}

ChunkPos ChunkPos::unpack(uint64_t packed) {
    int32_t px = static_cast<int32_t>((packed >> 38) & CHUNK_XZ_MASK) - CHUNK_XZ_OFFSET;
    int32_t py = static_cast<int32_t>((packed >> 26) & CHUNK_Y_MASK) - CHUNK_Y_OFFSET;
    int32_t pz = static_cast<int32_t>(packed & CHUNK_XZ_MASK) - CHUNK_XZ_OFFSET;
    return {px, py, pz};
}

// ColumnPos packing: only x and z, so we can use 32 bits each
uint64_t ColumnPos::pack() const {
    uint64_t px = static_cast<uint64_t>(static_cast<uint32_t>(x));
    uint64_t pz = static_cast<uint64_t>(static_cast<uint32_t>(z));
    return (px << 32) | pz;
}

ColumnPos ColumnPos::unpack(uint64_t packed) {
    int32_t px = static_cast<int32_t>(static_cast<uint32_t>(packed >> 32));
    int32_t pz = static_cast<int32_t>(static_cast<uint32_t>(packed & 0xFFFFFFFF));
    return {px, pz};
}

}  // namespace finevox
