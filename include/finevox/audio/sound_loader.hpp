#pragma once

/**
 * @file sound_loader.hpp
 * @brief Loads .sound config files into SoundSetDefinitions
 *
 * Uses the existing ConfigParser to read .sound files,
 * which use repeated keys for variants (same pattern as face: in .geom).
 */

#include "finevox/core/sound_registry.hpp"
#include "finevox/core/config_parser.hpp"

#include <string>
#include <optional>

namespace finevox::audio {

class SoundLoader {
public:
    /// Load a .sound file and return the definition
    [[nodiscard]] static std::optional<SoundSetDefinition> loadFromFile(
        const std::string& name, const std::string& configPath);

    /// Load from a ConfigDocument (already parsed)
    [[nodiscard]] static std::optional<SoundSetDefinition> loadFromConfig(
        const std::string& name, const ConfigDocument& doc);

    /// Load and register all .sound files in a directory
    /// Returns number of sound sets loaded
    static size_t loadDirectory(const std::string& dirPath);
};

}  // namespace finevox::audio
