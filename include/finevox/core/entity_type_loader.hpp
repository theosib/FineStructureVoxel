#pragma once

/**
 * @file entity_type_loader.hpp
 * @brief Loads EntityTypeDef from .entity config files
 */

#include "finevox/core/entity_type_def.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <cstddef>

namespace finevox {

class EntityTypeLoader {
public:
    /// Load an entity type definition from a .entity file
    [[nodiscard]] static std::optional<EntityTypeDef> loadFromFile(const std::string& path);

    /// Load an entity type definition from a string
    [[nodiscard]] static std::optional<EntityTypeDef> loadFromString(std::string_view content);

    /// Load all .entity files from a directory and register them
    /// Returns the number of types successfully loaded and registered
    static size_t loadDirectory(const std::string& dirPath);
};

}  // namespace finevox
