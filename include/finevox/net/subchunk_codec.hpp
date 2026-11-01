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
};

/// Append the message body. Compression is for networks: a local connection has no use for it.
void encodeSubchunk(const SubchunkData& data, std::vector<uint8_t>& out, bool compress);

/// False if it's malformed, in which case `data` means nothing
[[nodiscard]] bool decodeSubchunk(std::span<const uint8_t> body, SubchunkData& data);

/// LZ4, as the Vision message uses it: `var rawSize` then the block
void encodeCompressed(std::span<const uint8_t> raw, std::vector<uint8_t>& out);
[[nodiscard]] bool decodeCompressed(std::span<const uint8_t> body, std::vector<uint8_t>& raw, size_t limit);

}  // namespace finevox::net
