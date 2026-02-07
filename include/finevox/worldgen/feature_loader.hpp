/**
 * @file feature_loader.hpp
 * @brief ConfigParser-based .feature and .ore file loader
 *
 * Design: [27-world-generation.md] Section 27.5.5
 */

#pragma once

#include "finevox/worldgen/feature.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace finevox {
class ConfigDocument;
}

namespace finevox::worldgen {

struct TreeConfig;
struct OreConfig;

/// Load feature definitions from .feature and .ore config files
class FeatureLoader {
public:
    /// Parse a .feature file (tree, decoration, etc.)
    /// @return The created Feature, or nullptr on parse failure
    [[nodiscard]] static std::shared_ptr<Feature> loadFeatureFile(
        std::string_view featureName, const std::string& configPath);

    /// Parse a .ore file
    [[nodiscard]] static std::shared_ptr<Feature> loadOreFile(
        std::string_view featureName, const std::string& configPath);

    /// Parse a TreeConfig from a ConfigDocument
    [[nodiscard]] static std::optional<TreeConfig> parseTreeConfig(
        const ConfigDocument& doc);

    /// Parse an OreConfig from a ConfigDocument
    [[nodiscard]] static std::optional<OreConfig> parseOreConfig(
        const ConfigDocument& doc);

    /// Load all .feature and .ore files from a directory and register them
    /// @return Number of features successfully loaded
    static size_t loadDirectory(const std::string& dirPath,
                                const std::string& namePrefix = "");
};

}  // namespace finevox::worldgen
