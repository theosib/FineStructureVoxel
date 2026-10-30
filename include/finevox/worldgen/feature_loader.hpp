/**
 * @file feature_loader.hpp
 * @brief ConfigParser-based .feature and .ore file loader
 *
 * Design: [27-world-generation.md] Section 27.5.5
 */

#pragma once

#include "finevox/worldgen/feature.hpp"
#include "finevox/worldgen/feature_cluster.hpp"
#include "finevox/worldgen/feature_registry.hpp"

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

    /// Parse a ClusterConfig from a ConfigDocument
    [[nodiscard]] static std::optional<ClusterConfig> parseClusterConfig(
        const ConfigDocument& doc);

    /// Parse placement rules from a feature file's keys. Returns nullopt when the
    /// file has no `density` key, meaning the game places the feature itself.
    [[nodiscard]] static std::optional<FeaturePlacement> parsePlacement(
        const ConfigDocument& doc, std::string_view featureName);

    /// Load all .feature and .ore files from a directory and register them.
    /// Files declaring `density` also register their placement rule.
    /// @return Number of features successfully loaded
    static size_t loadDirectory(const std::string& dirPath,
                                const std::string& namePrefix = "");
};

}  // namespace finevox::worldgen
