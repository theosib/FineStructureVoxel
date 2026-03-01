#include "finevox/core/serialization.hpp"
#include "finevox/core/cbor.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_type_id.hpp"

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

    // Serialize rotation data (only if there are non-identity rotations)
    if (chunk.hasNonIdentityRotations()) {
        const auto& rotations = chunk.rotationData();
        result.rotations.assign(rotations.begin(), rotations.end());
    }

    // Serialize light data (only if not all dark)
    if (!chunk.isLightDark()) {
        const auto& light = chunk.lightData();
        result.lightData.assign(light.begin(), light.end());
    }

    // Serialize per-block extra data (sparse map)
    for (const auto& [index, dataPtr] : chunk.allBlockData()) {
        if (dataPtr && !dataPtr->empty()) {
            result.blockData[static_cast<uint16_t>(index)] = dataPtr->clone();
        }
    }

    // Serialize subchunk-level extra data
    if (chunk.hasData() && !chunk.data()->empty()) {
        result.subchunkData = chunk.data()->clone();
    }

    // Serialize fluid layer (only if non-empty)
    const FluidLayer* fluidLayer = chunk.fluidLayer();
    if (fluidLayer && !fluidLayer->isEmpty()) {
        const FluidPalette& fp = fluidLayer->palette();
        // Build palette strings: index 0 = empty (unused), 1..15 = fluid type names
        result.fluidPalette.resize(FluidPalette::MAX_ENTRIES + 1);
        result.fluidPalette[0] = "";  // index 0 = empty
        for (uint8_t i = 1; i <= FluidPalette::MAX_ENTRIES; ++i) {
            FluidTypeId fid = fp.getType(i);
            result.fluidPalette[i] = fid.isValid() ? std::string(fid.name()) : "";
        }
        // Trim trailing empty entries
        while (result.fluidPalette.size() > 1 && result.fluidPalette.back().empty()) {
            result.fluidPalette.pop_back();
        }
        // Copy raw data
        const auto& raw = fluidLayer->rawData();
        result.fluidData.assign(raw.begin(), raw.end());
    }

    return result;
}

std::vector<uint8_t> SubChunkSerializer::toCBOR(const SubChunk& chunk, int32_t yLevel) {
    SerializedSubChunk data = serialize(chunk, yLevel);
    std::vector<uint8_t> out;

    // Count fields: y, palette, blocks, and optionally rotations, light, blockData, subchunkData, fluid
    int fieldCount = 3;  // y, palette, blocks
    bool hasRotations = !data.rotations.empty();
    bool hasLightData = !data.lightData.empty();
    bool hasBlockData = !data.blockData.empty();
    bool hasSubchunkData = data.subchunkData && !data.subchunkData->empty();
    bool hasFluidData = !data.fluidData.empty();
    if (hasRotations) fieldCount++;
    if (hasLightData) fieldCount++;
    if (hasBlockData) fieldCount++;
    if (hasSubchunkData) fieldCount++;
    if (hasFluidData) fieldCount += 2;  // fluid_palette + fluid_data

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

    // "light": byte string (optional, 4096 bytes of packed light data)
    if (hasLightData) {
        cbor::encodeString(out, "light");
        cbor::encodeBytes(out, data.lightData);
    }

    // "blockData": map of block index -> DataContainer (optional, sparse per-block extra data)
    if (hasBlockData) {
        cbor::encodeString(out, "blockData");
        cbor::encodeMapHeader(out, data.blockData.size());
        for (const auto& [index, container] : data.blockData) {
            cbor::encodeInt(out, index);
            auto containerBytes = container->toCBOR();
            // Embed the container CBOR directly (it's already a valid CBOR value)
            out.insert(out.end(), containerBytes.begin(), containerBytes.end());
        }
    }

    // "data": DataContainer (optional, subchunk-level extra data)
    if (hasSubchunkData) {
        cbor::encodeString(out, "data");
        auto containerBytes = data.subchunkData->toCBOR();
        out.insert(out.end(), containerBytes.begin(), containerBytes.end());
    }

    // "fluid_palette" + "fluid_data": fluid layer (optional)
    if (hasFluidData) {
        cbor::encodeString(out, "fluid_palette");
        cbor::encodeArrayHeader(out, data.fluidPalette.size());
        for (const auto& name : data.fluidPalette) {
            cbor::encodeString(out, name);
        }

        cbor::encodeString(out, "fluid_data");
        cbor::encodeBytes(out, data.fluidData);
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

    // Apply rotation data if present
    if (data.rotations.size() == SubChunk::VOLUME) {
        std::array<uint8_t, SubChunk::VOLUME> rotationArray;
        std::copy(data.rotations.begin(), data.rotations.end(), rotationArray.begin());
        chunk->setRotationData(rotationArray);
    }

    // Apply light data if present
    if (data.lightData.size() == SubChunk::VOLUME) {
        std::array<uint8_t, SubChunk::VOLUME> lightArray;
        std::copy(data.lightData.begin(), data.lightData.end(), lightArray.begin());
        chunk->setLightData(lightArray);
    }

    // Apply per-block extra data
    for (const auto& [index, container] : data.blockData) {
        if (container && !container->empty()) {
            DataContainer& blockData = chunk->getOrCreateBlockData(index);
            // Copy the data from the serialized container
            container->forEach([&blockData](DataKey key, const DataValue& value) {
                // Clone the value and set it
                blockData.set(key, DataContainer::cloneValue(value));
            });
        }
    }

    // Apply subchunk-level extra data
    if (data.subchunkData && !data.subchunkData->empty()) {
        DataContainer& subchunkData = chunk->getOrCreateData();
        data.subchunkData->forEach([&subchunkData](DataKey key, const DataValue& value) {
            subchunkData.set(key, DataContainer::cloneValue(value));
        });
    }

    // Restore fluid layer (only if fluid data was serialized)
    if (!data.fluidData.empty() && data.fluidData.size() == FluidLayer::VOLUME) {
        FluidLayer& fl = chunk->getOrCreateFluidLayer();
        fl.clear();

        // Rebuild palette mapping: serialized index → FluidTypeId
        // index 0 is empty, 1..N are fluid type names
        std::array<FluidTypeId, 16> paletteMap{};
        for (size_t i = 1; i < data.fluidPalette.size() && i <= FluidPalette::MAX_ENTRIES; ++i) {
            if (!data.fluidPalette[i].empty()) {
                paletteMap[i] = FluidTypeId::fromName(data.fluidPalette[i]);
            }
        }

        // Re-set each non-empty cell using the public API so palette + refcounts are correct
        std::array<uint8_t, FluidLayer::VOLUME> rawData{};
        std::copy(data.fluidData.begin(), data.fluidData.end(), rawData.begin());
        for (int32_t idx = 0; idx < FluidLayer::VOLUME; ++idx) {
            uint8_t packed = rawData[idx];
            uint8_t palIdx = (packed >> 4) & 0x0F;
            uint8_t level  = packed & 0x0F;
            if (palIdx != 0 && level != 0) {
                FluidTypeId fid = paletteMap[palIdx];
                if (fid.isValid()) {
                    fl.setFluid(idx, fid, level);
                }
            }
        }
    }

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
        } else if (key == "light") {
            auto [bytesType, bytesLen] = decoder.readHeader();
            if (bytesType == cbor::BYTE_STRING) {
                serialized.lightData = decoder.readBytes(bytesLen);
            }
        } else if (key == "blockData") {
            // Per-block extra data: map of block index -> DataContainer
            auto [mapType, mapLen] = decoder.readHeader();
            if (mapType == cbor::MAP) {
                for (uint64_t j = 0; j < mapLen; ++j) {
                    int64_t index = decoder.readInt();
                    // The value is an embedded CBOR DataContainer
                    // Save position, skip to measure, then extract and parse
                    size_t startPos = decoder.position();
                    decoder.skipValue();
                    size_t endPos = decoder.position();

                    // Extract the bytes for this DataContainer
                    std::span<const uint8_t> containerData{data.data() + startPos, endPos - startPos};
                    auto container = DataContainer::fromCBOR(containerData);
                    if (container) {
                        serialized.blockData[static_cast<uint16_t>(index)] = std::move(container);
                    }
                }
            }
        } else if (key == "data") {
            // Subchunk-level extra data: DataContainer
            size_t startPos = decoder.position();
            decoder.skipValue();
            size_t endPos = decoder.position();

            std::span<const uint8_t> containerData{data.data() + startPos, endPos - startPos};
            serialized.subchunkData = DataContainer::fromCBOR(containerData);
        } else if (key == "fluid_palette") {
            auto [arrType, arrLen] = decoder.readHeader();
            if (arrType == cbor::ARRAY) {
                serialized.fluidPalette.reserve(arrLen);
                for (uint64_t j = 0; j < arrLen; ++j) {
                    auto [strType, strLen] = decoder.readHeader();
                    if (strType == cbor::TEXT_STRING) {
                        serialized.fluidPalette.push_back(decoder.readString(strLen));
                    } else {
                        serialized.fluidPalette.push_back("");
                    }
                }
            }
        } else if (key == "fluid_data") {
            auto [bytesType, bytesLen] = decoder.readHeader();
            if (bytesType == cbor::BYTE_STRING) {
                serialized.fluidData = decoder.readBytes(bytesLen);
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
    bool hasColumnData = column.hasData() && !column.data()->empty();
    if (hasColumnData) fieldCount++;

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

    // "data": DataContainer (optional, column-level extra data)
    if (hasColumnData) {
        cbor::encodeString(out, "data");
        auto containerBytes = column.data()->toCBOR();
        out.insert(out.end(), containerBytes.begin(), containerBytes.end());
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
    std::unique_ptr<DataContainer> columnData;

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
        } else if (key == "data") {
            // Column-level extra data
            size_t startPos = decoder.position();
            decoder.skipValue();
            size_t endPos = decoder.position();

            std::span<const uint8_t> containerData{data.data() + startPos, endPos - startPos};
            columnData = DataContainer::fromCBOR(containerData);
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

                    SerializedSubChunk serialized;

                    for (uint64_t k = 0; k < scFieldCount; ++k) {
                        auto [fieldKeyType, fieldKeyLen] = decoder.readHeader();
                        if (fieldKeyType != cbor::TEXT_STRING) {
                            decoder.skipValue();
                            continue;
                        }
                        std::string fieldKey = decoder.readString(fieldKeyLen);

                        if (fieldKey == "y") {
                            serialized.yLevel = static_cast<int32_t>(decoder.readInt());
                        } else if (fieldKey == "palette") {
                            auto [paletteType, paletteLen] = decoder.readHeader();
                            if (paletteType == cbor::ARRAY) {
                                serialized.palette.reserve(paletteLen);
                                for (uint64_t p = 0; p < paletteLen; ++p) {
                                    auto [strType, strLen] = decoder.readHeader();
                                    if (strType == cbor::TEXT_STRING) {
                                        serialized.palette.push_back(decoder.readString(strLen));
                                    } else {
                                        serialized.palette.push_back("");
                                    }
                                }
                            }
                        } else if (fieldKey == "blocks") {
                            auto [bytesType, bytesLen] = decoder.readHeader();
                            if (bytesType == cbor::BYTE_STRING) {
                                serialized.blocks = decoder.readBytes(bytesLen);
                                serialized.use16Bit = (serialized.blocks.size() == SubChunk::VOLUME * 2);
                            }
                        } else if (fieldKey == "rotations") {
                            auto [bytesType, bytesLen] = decoder.readHeader();
                            if (bytesType == cbor::BYTE_STRING) {
                                serialized.rotations = decoder.readBytes(bytesLen);
                            }
                        } else if (fieldKey == "light") {
                            auto [bytesType, bytesLen] = decoder.readHeader();
                            if (bytesType == cbor::BYTE_STRING) {
                                serialized.lightData = decoder.readBytes(bytesLen);
                            }
                        } else if (fieldKey == "blockData") {
                            // Per-block extra data: map of block index -> DataContainer
                            auto [mapType, mapLen] = decoder.readHeader();
                            if (mapType == cbor::MAP) {
                                for (uint64_t m = 0; m < mapLen; ++m) {
                                    int64_t index = decoder.readInt();
                                    size_t startPos = decoder.position();
                                    decoder.skipValue();
                                    size_t endPos = decoder.position();

                                    std::span<const uint8_t> containerData{data.data() + startPos, endPos - startPos};
                                    auto container = DataContainer::fromCBOR(containerData);
                                    if (container) {
                                        serialized.blockData[static_cast<uint16_t>(index)] = std::move(container);
                                    }
                                }
                            }
                        } else if (fieldKey == "data") {
                            // Subchunk-level extra data
                            size_t startPos = decoder.position();
                            decoder.skipValue();
                            size_t endPos = decoder.position();

                            std::span<const uint8_t> containerData{data.data() + startPos, endPos - startPos};
                            serialized.subchunkData = DataContainer::fromCBOR(containerData);
                        } else if (fieldKey == "fluid_palette") {
                            auto [paletteType, paletteLen] = decoder.readHeader();
                            if (paletteType == cbor::ARRAY) {
                                serialized.fluidPalette.reserve(paletteLen);
                                for (uint64_t p = 0; p < paletteLen; ++p) {
                                    auto [strType, strLen] = decoder.readHeader();
                                    if (strType == cbor::TEXT_STRING) {
                                        serialized.fluidPalette.push_back(decoder.readString(strLen));
                                    } else {
                                        serialized.fluidPalette.push_back("");
                                    }
                                }
                            }
                        } else if (fieldKey == "fluid_data") {
                            auto [bytesType, bytesLen] = decoder.readHeader();
                            if (bytesType == cbor::BYTE_STRING) {
                                serialized.fluidData = decoder.readBytes(bytesLen);
                            }
                        } else {
                            decoder.skipValue();
                        }
                    }

                    auto sc = SubChunkSerializer::deserialize(serialized);
                    if (sc) {
                        subchunks.emplace_back(serialized.yLevel, std::move(sc));
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
                // Convert linear index to local coordinates
                LocalBlockCoord local = LocalBlockCoord::fromIndex(static_cast<uint16_t>(idx));
                // Convert to world Y coordinate
                int worldY = y * 16 + local.y;
                column->setBlock(local.x, worldY, local.z, type);
            }
        }

        SubChunk* targetSc = column->getSubChunk(y);
        if (targetSc) {
            // Copy rotation data
            if (sc->hasNonIdentityRotations()) {
                targetSc->setRotationData(sc->rotationData());
            }

            // Copy light data
            if (!sc->isLightDark()) {
                targetSc->setLightData(sc->lightData());
            }

            // Copy block extra data
            for (const auto& [blockIdx, dataPtr] : sc->allBlockData()) {
                if (dataPtr && !dataPtr->empty()) {
                    DataContainer& targetData = targetSc->getOrCreateBlockData(blockIdx);
                    dataPtr->forEach([&targetData](DataKey key, const DataValue& value) {
                        targetData.set(key, DataContainer::cloneValue(value));
                    });
                }
            }

            // Copy subchunk-level extra data
            if (sc->hasData() && !sc->data()->empty()) {
                DataContainer& targetData = targetSc->getOrCreateData();
                sc->data()->forEach([&targetData](DataKey key, const DataValue& value) {
                    targetData.set(key, DataContainer::cloneValue(value));
                });
            }

            // Copy fluid layer
            const FluidLayer* srcFluid = sc->fluidLayer();
            if (srcFluid && !srcFluid->isEmpty()) {
                FluidLayer& targetFluid = targetSc->getOrCreateFluidLayer();
                // Copy cell by cell using public API to maintain palette/refcounts
                for (int32_t idx = 0; idx < FluidLayer::VOLUME; ++idx) {
                    FluidCell cell = srcFluid->getCell(idx);
                    if (!cell.isEmpty()) {
                        FluidTypeId fid = srcFluid->palette().getType(cell.paletteIndex);
                        if (fid.isValid()) {
                            targetFluid.setFluid(idx, fid, cell.level);
                        }
                    }
                }
            }
        }
    }

    // Apply column-level extra data
    if (columnData && !columnData->empty()) {
        DataContainer& colData = column->getOrCreateData();
        columnData->forEach([&colData](DataKey key, const DataValue& value) {
            colData.set(key, DataContainer::cloneValue(value));
        });
    }

    return column;
}

}  // namespace finevox
