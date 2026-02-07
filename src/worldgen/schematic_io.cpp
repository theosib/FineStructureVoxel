/**
 * @file schematic_io.cpp
 * @brief Schematic CBOR serialization and LZ4-compressed file I/O
 *
 * Design: [21-clipboard-schematic.md] Section 21.6
 *
 * File format: magic "VXSC" (4 bytes) + compressed size (4 bytes LE)
 * + LZ4-compressed CBOR payload.
 */

#include "finevox/worldgen/schematic_io.hpp"
#include "finevox/core/cbor.hpp"

#include <lz4.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace finevox::worldgen {

namespace {

constexpr uint32_t SCHEMATIC_MAGIC = 0x56585343;  // "VXSC"
constexpr int32_t FORMAT_VERSION = 1;

}  // namespace

// ============================================================================
// Serialization
// ============================================================================

std::vector<uint8_t> serializeSchematic(const Schematic& schematic) {
    std::vector<uint8_t> out;
    out.reserve(1024);

    // Build palette from block types
    std::vector<std::string> palette;
    std::unordered_map<std::string, uint32_t> paletteMap;

    // Air is always palette index 0
    palette.push_back("air");
    paletteMap["air"] = 0;
    paletteMap[""] = 0;  // Empty string = air

    for (int32_t x = 0; x < schematic.sizeX(); ++x) {
        for (int32_t z = 0; z < schematic.sizeZ(); ++z) {
            for (int32_t y = 0; y < schematic.sizeY(); ++y) {
                const auto& snap = schematic.at(x, y, z);
                if (!snap.isAir() && paletteMap.find(snap.typeName) == paletteMap.end()) {
                    paletteMap[snap.typeName] = static_cast<uint32_t>(palette.size());
                    palette.push_back(snap.typeName);
                }
            }
        }
    }

    bool use16Bit = palette.size() > 256;

    // Build block data and sparse metadata
    int64_t vol = schematic.volume();
    std::vector<uint8_t> blockBytes;
    if (use16Bit) {
        blockBytes.resize(static_cast<size_t>(vol) * 2);
    } else {
        blockBytes.resize(static_cast<size_t>(vol));
    }

    // Count metadata entries for map header
    size_t metadataCount = 0;
    size_t blockIdx = 0;

    for (int32_t x = 0; x < schematic.sizeX(); ++x) {
        for (int32_t z = 0; z < schematic.sizeZ(); ++z) {
            for (int32_t y = 0; y < schematic.sizeY(); ++y) {
                const auto& snap = schematic.at(x, y, z);
                uint32_t paletteIdx = 0;
                if (!snap.isAir()) {
                    auto it = paletteMap.find(snap.typeName);
                    if (it != paletteMap.end()) paletteIdx = it->second;
                }

                if (use16Bit) {
                    blockBytes[blockIdx * 2] = static_cast<uint8_t>(paletteIdx >> 8);
                    blockBytes[blockIdx * 2 + 1] = static_cast<uint8_t>(paletteIdx);
                } else {
                    blockBytes[blockIdx] = static_cast<uint8_t>(paletteIdx);
                }

                if (snap.hasMetadata()) {
                    ++metadataCount;
                }
                ++blockIdx;
            }
        }
    }

    // Count CBOR map fields
    size_t fieldCount = 4;  // version, size, palette, blocks
    if (!schematic.name().empty()) ++fieldCount;
    if (!schematic.author().empty()) ++fieldCount;
    if (use16Bit) ++fieldCount;  // use16bit flag
    if (metadataCount > 0) ++fieldCount;

    cbor::encodeMapHeader(out, fieldCount);

    // version
    cbor::encodeString(out, "version");
    cbor::encodeInt(out, FORMAT_VERSION);

    // name (optional)
    if (!schematic.name().empty()) {
        cbor::encodeString(out, "name");
        cbor::encodeString(out, schematic.name());
    }

    // author (optional)
    if (!schematic.author().empty()) {
        cbor::encodeString(out, "author");
        cbor::encodeString(out, schematic.author());
    }

    // size
    cbor::encodeString(out, "size");
    cbor::encodeArrayHeader(out, 3);
    cbor::encodeInt(out, schematic.sizeX());
    cbor::encodeInt(out, schematic.sizeY());
    cbor::encodeInt(out, schematic.sizeZ());

    // palette
    cbor::encodeString(out, "palette");
    cbor::encodeArrayHeader(out, palette.size());
    for (const auto& name : palette) {
        cbor::encodeString(out, name);
    }

    // use16bit flag
    if (use16Bit) {
        cbor::encodeString(out, "use16bit");
        cbor::encodeBool(out, true);
    }

    // blocks
    cbor::encodeString(out, "blocks");
    cbor::encodeBytes(out, blockBytes);

    // metadata (sparse)
    if (metadataCount > 0) {
        cbor::encodeString(out, "metadata");
        cbor::encodeMapHeader(out, metadataCount);

        blockIdx = 0;
        for (int32_t x = 0; x < schematic.sizeX(); ++x) {
            for (int32_t z = 0; z < schematic.sizeZ(); ++z) {
                for (int32_t y = 0; y < schematic.sizeY(); ++y) {
                    const auto& snap = schematic.at(x, y, z);
                    if (snap.hasMetadata()) {
                        cbor::encodeInt(out, static_cast<int64_t>(blockIdx));

                        // Count sub-fields
                        size_t subFields = 0;
                        if (snap.rotation != Rotation::IDENTITY) ++subFields;
                        if (snap.displacement != glm::vec3(0.0f)) ++subFields;
                        if (snap.extraData.has_value()) ++subFields;

                        cbor::encodeMapHeader(out, subFields);

                        if (snap.rotation != Rotation::IDENTITY) {
                            cbor::encodeString(out, "rotation");
                            cbor::encodeInt(out, snap.rotation.index());
                        }
                        if (snap.displacement != glm::vec3(0.0f)) {
                            cbor::encodeString(out, "displacement");
                            cbor::encodeArrayHeader(out, 3);
                            cbor::encodeDouble(out, snap.displacement.x);
                            cbor::encodeDouble(out, snap.displacement.y);
                            cbor::encodeDouble(out, snap.displacement.z);
                        }
                        if (snap.extraData.has_value()) {
                            cbor::encodeString(out, "data");
                            auto dataBytes = snap.extraData->toCBOR();
                            out.insert(out.end(), dataBytes.begin(), dataBytes.end());
                        }
                    }
                    ++blockIdx;
                }
            }
        }
    }

    return out;
}

// ============================================================================
// Deserialization
// ============================================================================

Schematic deserializeSchematic(std::span<const uint8_t> data) {
    cbor::Decoder decoder(data);

    auto [mapType, mapSize] = decoder.readHeader();
    if (mapType != cbor::MAP) {
        throw std::runtime_error("Invalid schematic CBOR: expected map");
    }

    int32_t sizeX = 0, sizeY = 0, sizeZ = 0;
    std::vector<std::string> palette;
    std::vector<uint8_t> blockBytes;
    bool use16Bit = false;
    std::string name;
    std::string author;

    // Metadata storage: linear index -> {rotation, displacement, extraData}
    struct MetaEntry {
        uint8_t rotIndex = 0;
        glm::vec3 displacement{0.0f};
        std::optional<DataContainer> extraData;
    };
    std::unordered_map<size_t, MetaEntry> metadata;

    for (uint64_t i = 0; i < mapSize; ++i) {
        auto [keyType, keyLen] = decoder.readHeader();
        if (keyType != cbor::TEXT_STRING) {
            decoder.skipValue();
            continue;
        }
        std::string key = decoder.readString(keyLen);

        if (key == "version") {
            decoder.readInt();  // Read but we only support v1
        } else if (key == "name") {
            auto [t, l] = decoder.readHeader();
            if (t == cbor::TEXT_STRING) name = decoder.readString(l);
        } else if (key == "author") {
            auto [t, l] = decoder.readHeader();
            if (t == cbor::TEXT_STRING) author = decoder.readString(l);
        } else if (key == "size") {
            auto [arrType, arrLen] = decoder.readHeader();
            if (arrType == cbor::ARRAY && arrLen >= 3) {
                sizeX = static_cast<int32_t>(decoder.readInt());
                sizeY = static_cast<int32_t>(decoder.readInt());
                sizeZ = static_cast<int32_t>(decoder.readInt());
            }
        } else if (key == "palette") {
            auto [arrType, arrLen] = decoder.readHeader();
            if (arrType == cbor::ARRAY) {
                palette.reserve(arrLen);
                for (uint64_t j = 0; j < arrLen; ++j) {
                    auto [strType, strLen] = decoder.readHeader();
                    if (strType == cbor::TEXT_STRING) {
                        palette.push_back(decoder.readString(strLen));
                    }
                }
            }
        } else if (key == "use16bit") {
            auto [t, v] = decoder.readHeader();
            use16Bit = (t == cbor::SIMPLE && v == cbor::TRUE_VALUE);
        } else if (key == "blocks") {
            auto [bytesType, bytesLen] = decoder.readHeader();
            if (bytesType == cbor::BYTE_STRING) {
                blockBytes = decoder.readBytes(bytesLen);
            }
        } else if (key == "metadata") {
            auto [metaType, metaLen] = decoder.readHeader();
            if (metaType == cbor::MAP) {
                for (uint64_t j = 0; j < metaLen; ++j) {
                    size_t idx = static_cast<size_t>(decoder.readInt());
                    MetaEntry entry;

                    auto [subType, subLen] = decoder.readHeader();
                    if (subType == cbor::MAP) {
                        for (uint64_t k = 0; k < subLen; ++k) {
                            auto [skType, skLen] = decoder.readHeader();
                            if (skType != cbor::TEXT_STRING) {
                                decoder.skipValue();
                                continue;
                            }
                            std::string subKey = decoder.readString(skLen);

                            if (subKey == "rotation") {
                                entry.rotIndex = static_cast<uint8_t>(decoder.readInt());
                            } else if (subKey == "displacement") {
                                auto [at, al] = decoder.readHeader();
                                if (at == cbor::ARRAY && al >= 3) {
                                    // Read float64 values
                                    // Note: readHeader() for float64 returns (SIMPLE, raw_bits)
                                    auto readDouble = [&]() -> float {
                                        auto [dt, dv] = decoder.readHeader();
                                        if (dt == cbor::SIMPLE) {
                                            // dv contains the raw 64-bit float bits
                                            double result;
                                            std::memcpy(&result, &dv, sizeof(result));
                                            return static_cast<float>(result);
                                        }
                                        // Fallback: int
                                        if (dt == cbor::UNSIGNED_INT) return static_cast<float>(dv);
                                        if (dt == cbor::NEGATIVE_INT) return static_cast<float>(-1 - static_cast<int64_t>(dv));
                                        return 0.0f;
                                    };
                                    entry.displacement.x = readDouble();
                                    entry.displacement.y = readDouble();
                                    entry.displacement.z = readDouble();
                                }
                            } else if (subKey == "data") {
                                // Read raw CBOR bytes for DataContainer
                                size_t startPos = decoder.position();
                                decoder.skipValue();
                                size_t endPos = decoder.position();
                                std::span<const uint8_t> dcSpan(data.data() + startPos, endPos - startPos);
                                auto dc = DataContainer::fromCBOR(dcSpan);
                                if (dc) {
                                    entry.extraData = std::move(*dc);
                                }
                            } else {
                                decoder.skipValue();
                            }
                        }
                    }
                    metadata[idx] = std::move(entry);
                }
            }
        } else {
            decoder.skipValue();
        }
    }

    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) {
        throw std::runtime_error("Invalid schematic: bad dimensions");
    }

    Schematic result(sizeX, sizeY, sizeZ);
    result.setName(name);
    result.setAuthor(author);

    // Fill blocks from palette indices
    size_t blockIdx = 0;
    for (int32_t x = 0; x < sizeX; ++x) {
        for (int32_t z = 0; z < sizeZ; ++z) {
            for (int32_t y = 0; y < sizeY; ++y) {
                uint32_t paletteIdx = 0;
                if (use16Bit && blockIdx * 2 + 1 < blockBytes.size()) {
                    paletteIdx = (static_cast<uint32_t>(blockBytes[blockIdx * 2]) << 8) |
                                  blockBytes[blockIdx * 2 + 1];
                } else if (!use16Bit && blockIdx < blockBytes.size()) {
                    paletteIdx = blockBytes[blockIdx];
                }

                if (paletteIdx < palette.size() && palette[paletteIdx] != "air") {
                    auto& snap = result.at(x, y, z);
                    snap.typeName = palette[paletteIdx];

                    auto metaIt = metadata.find(blockIdx);
                    if (metaIt != metadata.end()) {
                        snap.rotation = Rotation::byIndex(metaIt->second.rotIndex);
                        snap.displacement = metaIt->second.displacement;
                        snap.extraData = std::move(metaIt->second.extraData);
                    }
                }
                ++blockIdx;
            }
        }
    }

    return result;
}

// ============================================================================
// File I/O
// ============================================================================

void saveSchematic(const Schematic& schematic, const std::filesystem::path& path) {
    auto cborData = serializeSchematic(schematic);

    int maxCompressed = LZ4_compressBound(static_cast<int>(cborData.size()));
    std::vector<uint8_t> compressed(static_cast<size_t>(maxCompressed));

    int compressedSize = LZ4_compress_default(
        reinterpret_cast<const char*>(cborData.data()),
        reinterpret_cast<char*>(compressed.data()),
        static_cast<int>(cborData.size()),
        maxCompressed);

    if (compressedSize <= 0) {
        throw std::runtime_error("LZ4 compression failed");
    }

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file for writing: " + path.string());
    }

    // Write magic
    uint32_t magic = SCHEMATIC_MAGIC;
    file.write(reinterpret_cast<const char*>(&magic), 4);

    // Write uncompressed size (needed for decompression)
    uint32_t uncompressedSize = static_cast<uint32_t>(cborData.size());
    file.write(reinterpret_cast<const char*>(&uncompressedSize), 4);

    // Write compressed size
    uint32_t compSize = static_cast<uint32_t>(compressedSize);
    file.write(reinterpret_cast<const char*>(&compSize), 4);

    // Write compressed data
    file.write(reinterpret_cast<const char*>(compressed.data()), compressedSize);
}

Schematic loadSchematic(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open schematic file: " + path.string());
    }

    auto fileSize = file.tellg();
    file.seekg(0);

    if (fileSize < 12) {
        throw std::runtime_error("Schematic file too small");
    }

    // Read magic
    uint32_t magic = 0;
    file.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != SCHEMATIC_MAGIC) {
        throw std::runtime_error("Invalid schematic file magic");
    }

    // Read uncompressed size
    uint32_t uncompressedSize = 0;
    file.read(reinterpret_cast<char*>(&uncompressedSize), 4);

    // Read compressed size
    uint32_t compressedSize = 0;
    file.read(reinterpret_cast<char*>(&compressedSize), 4);

    // Read compressed data
    std::vector<uint8_t> compressed(compressedSize);
    file.read(reinterpret_cast<char*>(compressed.data()), compressedSize);

    // Decompress
    std::vector<uint8_t> cborData(uncompressedSize);
    int result = LZ4_decompress_safe(
        reinterpret_cast<const char*>(compressed.data()),
        reinterpret_cast<char*>(cborData.data()),
        static_cast<int>(compressedSize),
        static_cast<int>(uncompressedSize));

    if (result < 0) {
        throw std::runtime_error("LZ4 decompression failed");
    }

    return deserializeSchematic(cborData);
}

}  // namespace finevox::worldgen
