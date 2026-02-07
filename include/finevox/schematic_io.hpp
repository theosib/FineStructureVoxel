/**
 * @file schematic_io.hpp
 * @brief Schematic serialization and file I/O
 *
 * Design: [21-clipboard-schematic.md] Section 21.6
 *
 * File format: 4-byte magic "VXSC", 4-byte compressed size,
 * LZ4-compressed CBOR data.
 */

#pragma once

#include "finevox/schematic.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace finevox {

/// Serialize schematic to CBOR bytes
[[nodiscard]] std::vector<uint8_t> serializeSchematic(const Schematic& schematic);

/// Deserialize schematic from CBOR bytes
[[nodiscard]] Schematic deserializeSchematic(std::span<const uint8_t> data);

/// Save schematic to file (with LZ4 compression)
void saveSchematic(const Schematic& schematic, const std::filesystem::path& path);

/// Load schematic from file
[[nodiscard]] Schematic loadSchematic(const std::filesystem::path& path);

}  // namespace finevox
