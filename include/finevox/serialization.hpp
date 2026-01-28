#pragma once

/**
 * @file serialization.hpp
 * @brief CBOR-based serialization for SubChunks and ChunkColumns
 *
 * Design: [11-persistence.md] §11.3 Serialization
 */

#include "finevox/subchunk.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/data_container.hpp"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>
#include <unordered_map>

namespace finevox {

// ============================================================================
// SubChunk Serialization
// ============================================================================

// Serialized SubChunk data (intermediate format for flexibility)
struct SerializedSubChunk {
    int32_t yLevel = 0;                          // Y-level in chunk coordinates
    std::vector<std::string> palette;            // Block type names (index = local ID)
    std::vector<uint8_t> blocks;                 // 8-bit or 16-bit indices
    bool use16Bit = false;                       // True if blocks uses 16-bit indices
    std::vector<uint8_t> rotations;              // 1 byte per block (empty if all zero)
    std::vector<uint8_t> lightData;              // 4096 bytes: packed sky+block light (empty if all dark)
    std::unordered_map<uint16_t, std::unique_ptr<DataContainer>> blockData;  // Sparse per-block extra data
    std::unique_ptr<DataContainer> subchunkData; // SubChunk-level extra data
};

class SubChunkSerializer {
public:
    // Serialize a SubChunk to CBOR bytes
    // yLevel is the Y-coordinate of this subchunk (in chunk coords, e.g., -4, 0, 1, ...)
    [[nodiscard]] static std::vector<uint8_t> toCBOR(const SubChunk& chunk, int32_t yLevel);

    // Deserialize a SubChunk from CBOR bytes
    // Returns nullptr on failure
    [[nodiscard]] static std::unique_ptr<SubChunk> fromCBOR(std::span<const uint8_t> data, int32_t* outYLevel = nullptr);

    // Intermediate serialization (for inspection/testing)
    [[nodiscard]] static SerializedSubChunk serialize(const SubChunk& chunk, int32_t yLevel);
    [[nodiscard]] static std::unique_ptr<SubChunk> deserialize(const SerializedSubChunk& data);
};

// ============================================================================
// ChunkColumn Serialization
// ============================================================================

// Serialized ChunkColumn data
struct SerializedColumn {
    int32_t x = 0;                               // Column X coordinate
    int32_t z = 0;                               // Column Z coordinate
    std::vector<SerializedSubChunk> subchunks;   // Non-empty subchunks only
    std::vector<int16_t> heightmap;              // 256 values (16x16), optional
    std::vector<uint8_t> biomes;                 // Biome data, format TBD
    std::unique_ptr<DataContainer> data;         // Column-level extra data
};

class ColumnSerializer {
public:
    // Serialize a ChunkColumn to CBOR bytes
    [[nodiscard]] static std::vector<uint8_t> toCBOR(const ChunkColumn& column, int32_t x, int32_t z);

    // Deserialize a ChunkColumn from CBOR bytes
    // Returns nullptr on failure
    [[nodiscard]] static std::unique_ptr<ChunkColumn> fromCBOR(std::span<const uint8_t> data,
                                                                int32_t* outX = nullptr,
                                                                int32_t* outZ = nullptr);
};

// ============================================================================
// Compression (LZ4)
// ============================================================================

// Note: LZ4 compression will be added when we implement region files.
// For now, SubChunk and ChunkColumn serialization produce uncompressed CBOR.

}  // namespace finevox
