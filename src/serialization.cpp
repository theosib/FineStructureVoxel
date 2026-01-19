#include "finevox/serialization.hpp"
#include "finevox/cbor.hpp"
#include "finevox/string_interner.hpp"

namespace finevox {

// ============================================================================
// SubChunk Serialization
// ============================================================================

SerializedSubChunk SubChunkSerializer::serialize(const SubChunk& chunk, int32_t yLevel) {
    SerializedSubChunk result;
    result.yLevel = yLevel;

    // Build palette array from SubChunkPalette
    const auto& palette = chunk.palette();
    const auto& entries = palette.entries();

    // We need to compact the palette first to get contiguous indices
    // But we don't want to modify the original chunk, so we'll just use what we have
    // and handle gaps by writing empty strings for unused slots

    // Find the actual max used index
    size_t maxUsedIndex = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].isValid() || i == 0) {  // Air at 0 is always valid
            maxUsedIndex = i;
        }
    }

    // Build palette strings
    result.palette.resize(maxUsedIndex + 1);
    for (size_t i = 0; i <= maxUsedIndex; ++i) {
        if (i < entries.size()) {
            result.palette[i] = std::string(entries[i].name());
        } else {
            result.palette[i] = "";  // Empty slot
        }
    }

    // Ensure air is at index 0
    if (result.palette.empty() || result.palette[0] != "") {
        // This shouldn't happen, but handle it
        if (result.palette.empty()) {
            result.palette.push_back("");
        } else {
            result.palette[0] = "";  // Air has empty name
        }
    }

    // Determine if we need 16-bit indices
    result.use16Bit = result.palette.size() > 256;

    // Serialize block indices
    const auto& blocks = chunk.blocks();
    if (result.use16Bit) {
        result.blocks.resize(SubChunk::VOLUME * 2);
        for (int i = 0; i < SubChunk::VOLUME; ++i) {
            uint16_t idx = blocks[i];
            result.blocks[i * 2] = static_cast<uint8_t>(idx & 0xFF);
            result.blocks[i * 2 + 1] = static_cast<uint8_t>(idx >> 8);
        }
    } else {
        result.blocks.resize(SubChunk::VOLUME);
        for (int i = 0; i < SubChunk::VOLUME; ++i) {
            result.blocks[i] = static_cast<uint8_t>(blocks[i]);
        }
    }

    // TODO: Rotations - not yet implemented in SubChunk
    // For now, rotations is left empty (all default)

    // TODO: Per-block data - not yet implemented in SubChunk
    // For now, blockData is left empty

    return result;
}

std::vector<uint8_t> SubChunkSerializer::toCBOR(const SubChunk& chunk, int32_t yLevel) {
    SerializedSubChunk data = serialize(chunk, yLevel);
    std::vector<uint8_t> out;

    // Count fields: y, palette, blocks, and optionally rotations, data
    int fieldCount = 3;  // y, palette, blocks
    bool hasRotations = !data.rotations.empty();
    bool hasBlockData = !data.blockData.empty();
    if (hasRotations) fieldCount++;
    if (hasBlockData) fieldCount++;

    cbor::encodeMapHeader(out, fieldCount);

    // "y": yLevel
    cbor::encodeString(out, "y");
    cbor::encodeInt(out, data.yLevel);

    // "palette": [strings...]
    cbor::encodeString(out, "palette");
    cbor::encodeArrayHeader(out, data.palette.size());
    for (const auto& name : data.palette) {
        cbor::encodeString(out, name);
    }

    // "blocks": byte string
    cbor::encodeString(out, "blocks");
    cbor::encodeBytes(out, data.blocks);

    // "rotations": byte string (optional)
    if (hasRotations) {
        cbor::encodeString(out, "rotations");
        cbor::encodeBytes(out, data.rotations);
    }

    // "data": map (optional)
    if (hasBlockData) {
        cbor::encodeString(out, "data");
        cbor::encodeMapHeader(out, data.blockData.size());
        for (const auto& [index, container] : data.blockData) {
            cbor::encodeInt(out, index);
            auto containerBytes = container->toCBOR();
            // Embed the container CBOR directly (it's already a valid CBOR value)
            out.insert(out.end(), containerBytes.begin(), containerBytes.end());
        }
    }

    return out;
}

std::unique_ptr<SubChunk> SubChunkSerializer::deserialize(const SerializedSubChunk& data) {
    auto chunk = std::make_unique<SubChunk>();

    // First, we need to populate the palette
    // The palette in SubChunk starts with air at index 0
    // We need to add all non-air types and track their indices

    std::vector<SubChunkPalette::LocalIndex> paletteMapping(data.palette.size());
    paletteMapping[0] = 0;  // Air stays at 0

    for (size_t i = 1; i < data.palette.size(); ++i) {
        if (!data.palette[i].empty()) {
            BlockTypeId type = BlockTypeId::fromName(data.palette[i]);
            paletteMapping[i] = chunk->palette().addType(type);
        } else {
            paletteMapping[i] = 0;  // Empty slot maps to air
        }
    }

    // Now set all blocks
    if (data.use16Bit) {
        for (int i = 0; i < SubChunk::VOLUME; ++i) {
            uint16_t serializedIdx = static_cast<uint16_t>(data.blocks[i * 2]) |
                                     (static_cast<uint16_t>(data.blocks[i * 2 + 1]) << 8);
            if (serializedIdx < paletteMapping.size()) {
                BlockTypeId type = chunk->palette().getGlobalId(paletteMapping[serializedIdx]);
                chunk->setBlock(i, type);
            }
        }
    } else {
        for (int i = 0; i < SubChunk::VOLUME; ++i) {
            uint8_t serializedIdx = data.blocks[i];
            if (serializedIdx < paletteMapping.size()) {
                BlockTypeId type = chunk->palette().getGlobalId(paletteMapping[serializedIdx]);
                chunk->setBlock(i, type);
            }
        }
    }

    // TODO: Apply rotations when SubChunk supports them

    // TODO: Apply per-block data when SubChunk supports them

    return chunk;
}

std::unique_ptr<SubChunk> SubChunkSerializer::fromCBOR(std::span<const uint8_t> data, int32_t* outYLevel) {
    if (data.empty()) {
        return nullptr;
    }

    cbor::Decoder decoder(data);
    auto [majorType, fieldCount] = decoder.readHeader();

    if (majorType != cbor::MAP) {
        return nullptr;
    }

    SerializedSubChunk serialized;

    for (uint64_t i = 0; i < fieldCount; ++i) {
        // Read key
        auto [keyType, keyLen] = decoder.readHeader();
        if (keyType != cbor::TEXT_STRING) {
            decoder.skipValue();
            continue;
        }
        std::string key = decoder.readString(keyLen);

        if (key == "y") {
            serialized.yLevel = static_cast<int32_t>(decoder.readInt());
        } else if (key == "palette") {
            auto [arrType, arrLen] = decoder.readHeader();
            if (arrType == cbor::ARRAY) {
                serialized.palette.reserve(arrLen);
                for (uint64_t j = 0; j < arrLen; ++j) {
                    auto [strType, strLen] = decoder.readHeader();
                    if (strType == cbor::TEXT_STRING) {
                        serialized.palette.push_back(decoder.readString(strLen));
                    } else {
                        serialized.palette.push_back("");
                    }
                }
            }
        } else if (key == "blocks") {
            auto [bytesType, bytesLen] = decoder.readHeader();
            if (bytesType == cbor::BYTE_STRING) {
                serialized.blocks = decoder.readBytes(bytesLen);
                // Determine if 16-bit based on size
                serialized.use16Bit = (serialized.blocks.size() == SubChunk::VOLUME * 2);
            }
        } else if (key == "rotations") {
            auto [bytesType, bytesLen] = decoder.readHeader();
            if (bytesType == cbor::BYTE_STRING) {
                serialized.rotations = decoder.readBytes(bytesLen);
            }
        } else if (key == "data") {
            auto [mapType, mapLen] = decoder.readHeader();
            if (mapType == cbor::MAP) {
                for (uint64_t j = 0; j < mapLen; ++j) {
                    int64_t index = decoder.readInt();
                    // The value is an embedded CBOR DataContainer
                    // We need to read it as raw bytes and parse
                    // For now, skip it (TODO: implement proper nested CBOR reading)
                    decoder.skipValue();
                    (void)index;
                }
            }
        } else {
            decoder.skipValue();
        }
    }

    if (outYLevel) {
        *outYLevel = serialized.yLevel;
    }

    return deserialize(serialized);
}

// ============================================================================
// ChunkColumn Serialization
// ============================================================================

std::vector<uint8_t> ColumnSerializer::toCBOR(const ChunkColumn& column, int32_t x, int32_t z) {
    std::vector<uint8_t> out;

    // Count non-empty subchunks
    std::vector<std::pair<int32_t, const SubChunk*>> nonEmptySubchunks;
    column.forEachSubChunk([&](int32_t y, const SubChunk& sc) {
        if (!sc.isEmpty()) {
            nonEmptySubchunks.emplace_back(y, &sc);
        }
    });

    // Field count: x, z, subchunks, and optionally heightmap, biomes, data
    int fieldCount = 3;  // x, z, subchunks

    cbor::encodeMapHeader(out, fieldCount);

    // "x": x coordinate
    cbor::encodeString(out, "x");
    cbor::encodeInt(out, x);

    // "z": z coordinate
    cbor::encodeString(out, "z");
    cbor::encodeInt(out, z);

    // "subchunks": array of subchunk data
    cbor::encodeString(out, "subchunks");
    cbor::encodeArrayHeader(out, nonEmptySubchunks.size());

    for (const auto& [y, sc] : nonEmptySubchunks) {
        auto scBytes = SubChunkSerializer::toCBOR(*sc, y);
        out.insert(out.end(), scBytes.begin(), scBytes.end());
    }

    return out;
}

std::unique_ptr<ChunkColumn> ColumnSerializer::fromCBOR(std::span<const uint8_t> data,
                                                         int32_t* outX,
                                                         int32_t* outZ) {
    if (data.empty()) {
        return nullptr;
    }

    cbor::Decoder decoder(data);
    auto [majorType, fieldCount] = decoder.readHeader();

    if (majorType != cbor::MAP) {
        return nullptr;
    }

    int32_t x = 0, z = 0;
    std::vector<std::pair<int32_t, std::unique_ptr<SubChunk>>> subchunks;

    for (uint64_t i = 0; i < fieldCount; ++i) {
        // Read key
        auto [keyType, keyLen] = decoder.readHeader();
        if (keyType != cbor::TEXT_STRING) {
            decoder.skipValue();
            continue;
        }
        std::string key = decoder.readString(keyLen);

        if (key == "x") {
            x = static_cast<int32_t>(decoder.readInt());
        } else if (key == "z") {
            z = static_cast<int32_t>(decoder.readInt());
        } else if (key == "subchunks") {
            auto [arrType, arrLen] = decoder.readHeader();
            if (arrType == cbor::ARRAY) {
                for (uint64_t j = 0; j < arrLen; ++j) {
                    // Each subchunk is an embedded CBOR map
                    // Read the subchunk header to get its structure
                    auto [scType, scFieldCount] = decoder.readHeader();
                    if (scType != cbor::MAP) {
                        decoder.skipValue();
                        continue;
                    }

                    // We need to re-parse from the start
                    // This is inefficient, but correct. Better approach would be
                    // to track byte boundaries.
                    // For now, let's manually parse the subchunk inline

                    int32_t yLevel = 0;
                    std::vector<std::string> palette;
                    std::vector<uint8_t> blocks;
                    bool use16Bit = false;

                    for (uint64_t k = 0; k < scFieldCount; ++k) {
                        auto [fieldKeyType, fieldKeyLen] = decoder.readHeader();
                        if (fieldKeyType != cbor::TEXT_STRING) {
                            decoder.skipValue();
                            continue;
                        }
                        std::string fieldKey = decoder.readString(fieldKeyLen);

                        if (fieldKey == "y") {
                            yLevel = static_cast<int32_t>(decoder.readInt());
                        } else if (fieldKey == "palette") {
                            auto [paletteType, paletteLen] = decoder.readHeader();
                            if (paletteType == cbor::ARRAY) {
                                palette.reserve(paletteLen);
                                for (uint64_t p = 0; p < paletteLen; ++p) {
                                    auto [strType, strLen] = decoder.readHeader();
                                    if (strType == cbor::TEXT_STRING) {
                                        palette.push_back(decoder.readString(strLen));
                                    } else {
                                        palette.push_back("");
                                    }
                                }
                            }
                        } else if (fieldKey == "blocks") {
                            auto [bytesType, bytesLen] = decoder.readHeader();
                            if (bytesType == cbor::BYTE_STRING) {
                                blocks = decoder.readBytes(bytesLen);
                                use16Bit = (blocks.size() == SubChunk::VOLUME * 2);
                            }
                        } else {
                            decoder.skipValue();
                        }
                    }

                    // Create subchunk from parsed data
                    SerializedSubChunk serialized;
                    serialized.yLevel = yLevel;
                    serialized.palette = std::move(palette);
                    serialized.blocks = std::move(blocks);
                    serialized.use16Bit = use16Bit;

                    auto sc = SubChunkSerializer::deserialize(serialized);
                    if (sc) {
                        subchunks.emplace_back(yLevel, std::move(sc));
                    }
                }
            }
        } else {
            decoder.skipValue();
        }
    }

    if (outX) *outX = x;
    if (outZ) *outZ = z;

    // Create the column and populate it
    ColumnPos colPos{x, z};
    auto column = std::make_unique<ChunkColumn>(colPos);

    for (auto& [y, sc] : subchunks) {
        // We need to copy blocks from sc into the column
        // ChunkColumn creates subchunks on demand
        for (int idx = 0; idx < SubChunk::VOLUME; ++idx) {
            BlockTypeId type = sc->getBlock(idx);
            if (!type.isAir()) {
                // Convert linear index to coordinates (layout: y*256 + z*16 + x)
                int lx = idx & 15;
                int lz = (idx >> 4) & 15;
                int ly = idx >> 8;
                // Convert to column-local Y
                int worldY = y * 16 + ly;
                column->setBlock(lx, worldY, lz, type);
            }
        }
    }

    return column;
}

}  // namespace finevox
