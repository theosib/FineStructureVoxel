/**
 * @file biome_loader.hpp
 * @brief ConfigParser-based .biome file loader
 *
 * Design: [27-world-generation.md] Section 27.3.5
 *
 * Loads biome definitions from ConfigParser format files and
 * registers them with the BiomeRegistry.
 */

#pragma once

#include "finevox/biome.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finevox {

class ConfigDocument;

/// Load biome definitions from .biome config files
class BiomeLoader {
public:
    /// Parse a .biome file and return the BiomeProperties
    /// @param biomeName Logical name for the biome (e.g., "demo:plains")
    /// @param configPath Path to the .biome file
    /// @return Parsed properties, or nullopt on parse failure
    [[nodiscard]] static std::optional<BiomeProperties> loadFromFile(
        std::string_view biomeName, const std::string& configPath);

    /// Parse biome properties from an already-loaded ConfigDocument
    [[nodiscard]] static std::optional<BiomeProperties> loadFromConfig(
        std::string_view biomeName, const ConfigDocument& doc);

    /// Load and register all .biome files in a directory
    /// @return Number of biomes successfully loaded
    static size_t loadDirectory(const std::string& dirPath,
                                const std::string& namePrefix = "");
};

}  // namespace finevox
