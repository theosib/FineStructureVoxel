/**
 * @file feature_loader.cpp
 * @brief ConfigParser-based .feature and .ore file loader
 *
 * Design: [27-world-generation.md] Section 27.5.5
 */

#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/feature_tree.hpp"
#include "finevox/worldgen/feature_ore.hpp"
#include "finevox/worldgen/feature_registry.hpp"
#include "finevox/core/config_parser.hpp"

#include <filesystem>

namespace finevox::worldgen {

std::shared_ptr<Feature> FeatureLoader::loadFeatureFile(
    std::string_view featureName, const std::string& configPath) {

    ConfigParser parser;
    auto doc = parser.parseFile(configPath);
    if (!doc) return nullptr;

    auto typeStr = doc->getString("type");
    if (typeStr == "tree") {
        auto config = parseTreeConfig(*doc);
        if (!config) return nullptr;
        return std::make_shared<TreeFeature>(std::string(featureName), *config);
    }

    // Unknown feature type
    return nullptr;
}

std::shared_ptr<Feature> FeatureLoader::loadOreFile(
    std::string_view featureName, const std::string& configPath) {

    ConfigParser parser;
    auto doc = parser.parseFile(configPath);
    if (!doc) return nullptr;

    auto config = parseOreConfig(*doc);
    if (!config) return nullptr;
    return std::make_shared<OreFeature>(std::string(featureName), *config);
}

std::optional<TreeConfig> FeatureLoader::parseTreeConfig(const ConfigDocument& doc) {
    TreeConfig config;

    auto trunkStr = doc.getString("trunk");
    if (trunkStr.empty()) return std::nullopt;
    config.trunkBlock = BlockTypeId::fromName(trunkStr);

    auto leavesStr = doc.getString("leaves");
    if (leavesStr.empty()) return std::nullopt;
    config.leavesBlock = BlockTypeId::fromName(leavesStr);

    config.minTrunkHeight = doc.getInt("min_trunk_height", config.minTrunkHeight);
    config.maxTrunkHeight = doc.getInt("max_trunk_height", config.maxTrunkHeight);
    config.leafRadius = doc.getInt("leaf_radius", config.leafRadius);
    config.requiresSoil = doc.getBool("requires_soil", config.requiresSoil);

    return config;
}

std::optional<OreConfig> FeatureLoader::parseOreConfig(const ConfigDocument& doc) {
    OreConfig config;

    auto blockStr = doc.getString("block");
    if (blockStr.empty()) return std::nullopt;
    config.oreBlock = BlockTypeId::fromName(blockStr);

    auto replaceStr = doc.getString("replace");
    if (replaceStr.empty()) return std::nullopt;
    config.replaceBlock = BlockTypeId::fromName(replaceStr);

    config.veinSize = doc.getInt("vein_size", config.veinSize);
    config.minHeight = doc.getInt("min_height", config.minHeight);
    config.maxHeight = doc.getInt("max_height", config.maxHeight);
    config.veinsPerChunk = doc.getInt("veins_per_chunk", config.veinsPerChunk);

    return config;
}

size_t FeatureLoader::loadDirectory(const std::string& dirPath,
                                     const std::string& namePrefix) {
    size_t count = 0;
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;

        std::string stem = entry.path().stem().string();
        std::string ext = entry.path().extension().string();
        std::string name = namePrefix.empty() ? stem : namePrefix + ":" + stem;

        std::shared_ptr<Feature> feature;
        if (ext == ".feature") {
            feature = loadFeatureFile(name, entry.path().string());
        } else if (ext == ".ore") {
            feature = loadOreFile(name, entry.path().string());
        }

        if (feature) {
            FeatureRegistry::global().registerFeature(std::move(feature));
            ++count;
        }
    }

    return count;
}

}  // namespace finevox::worldgen
