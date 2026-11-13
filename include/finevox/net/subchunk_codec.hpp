#pragma once

/**
 * @file subchunk_codec.hpp
 * @brief The Subchunk message of PROTOCOL.md §5.2
 */

#include "finevox/core/position.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace finevox::net {

/// One subchunk, with blocks and fluids as wire ids
struct SubchunkData {
    static constexpr size_t CELLS = 4096;

    ChunkPos pos;
    std::vector<uint32_t> palette;   ///< At least one entry
    std::vector<uint16_t> indices;   ///< CELLS of them, or empty if the palette has one entry
    std::array<uint8_t, CELLS> light{};

    struct Fluid {
        uint16_t index = 0;
        uint32_t id = 0;
        uint8_t level = 0;
    };
    std::vector<Fluid> fluids;

    /// Interned biome names by column, x + z*16: 256 of them, or none
    std::vector<uint32_t> biomes;
};

/// The message body. Compression is for networks: a local connection has no use for it.
[[nodiscard]] std::vector<uint8_t> encodeSubchunk(const SubchunkData& data, bool compress);

/// False if it's malformed, in which case `data` means nothing
[[nodiscard]] bool decodeSubchunk(std::span<const uint8_t> body, SubchunkData& data);

/// An LZ4 block. Its raw size travels beside it, in the message.
[[nodiscard]] std::vector<uint8_t> lz4Compress(std::span<const uint8_t> raw);
/// False if it isn't `rawSize` bytes once undone, or `rawSize` is over `limit`
[[nodiscard]] bool lz4Decompress(std::span<const uint8_t> block, uint64_t rawSize, size_t limit,
                                 std::vector<uint8_t>& raw);

}  // namespace finevox::net
