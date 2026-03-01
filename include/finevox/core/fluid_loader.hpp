#pragma once

/**
 * @file fluid_loader.hpp
 * @brief Loads FluidType definitions from .fluid config files
 *
 * Follows the same pattern as EntityTypeLoader.
 */

#include "finevox/core/fluid_type.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <cstddef>

namespace finevox {

class FluidLoader {
public:
    /// Load a fluid type definition from a .fluid file
    [[nodiscard]] static std::optional<FluidType> loadFromFile(const std::string& path);

    /// Load a fluid type definition from a string
    [[nodiscard]] static std::optional<FluidType> loadFromString(std::string_view content);

    /// Load all .fluid files from a directory, register types, and register interactions
    /// Returns the number of types successfully loaded and registered
    static size_t loadDirectory(const std::string& dirPath);

    /// Parse and register interactions from a .fluid file's content.
    /// Call after all fluid types are registered.
    /// Format: "interaction <other_fluid>: <result_block>"
    /// Returns number of interactions registered.
    static size_t loadInteractions(std::string_view content, std::string_view ownerFluid);
};

}  // namespace finevox
