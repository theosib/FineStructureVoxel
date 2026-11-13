/**
 * @file feature_loader.cpp
 * @brief ConfigParser-based .feature and .ore file loader
 *
 * Design: [27-world-generation.md] Section 27.5.5
 */

#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/feature_tree.hpp"
#include "finevox/worldgen/feature_ore.hpp"
#include "finevox/worldgen/feature_cluster.hpp"
#include "finevox/worldgen/feature_registry.hpp"
#include "finevox/core/config_parser.hpp"

#include <filesystem>
#include <sstream>

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
    if (typeStr == "cluster") {
        auto config = parseClusterConfig(*doc);
        if (!config) return nullptr;
        return std::make_shared<ClusterFeature>(std::string(featureName), *config);
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
    config.crownTop = doc.getInt("crown_top", config.crownTop);
    {
        std::istringstream crown{std::string(doc.getString("crown"))};
        std::string word;
        while (crown >> word) {
            TreeConfig::CrownLayer layer;
            if (word.back() == '~') layer.corners = TreeConfig::CrownLayer::Corners::Random;
            if (word.back() == '+') layer.corners = TreeConfig::CrownLayer::Corners::Cut;
            layer.radius = std::atoi(word.c_str());
            config.crown.push_back(layer);
        }
    }

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
    for (const ConfigEntry* host : doc.getAll("host")) {
        if (!host->hasSuffix()) continue;
        config.hosts.emplace_back(BlockTypeId::fromName(host->suffix),
                                  BlockTypeId::fromName(host->value.asStringOwned()));
    }

    return config;
}

std::optional<ClusterConfig> FeatureLoader::parseClusterConfig(const ConfigDocument& doc) {
    ClusterConfig config;

    auto blockStr = doc.getString("block");
    if (blockStr.empty()) return std::nullopt;
    config.block = BlockTypeId::fromName(blockStr);

    auto topStr = doc.getString("top");
    if (!topStr.empty()) config.topBlock = BlockTypeId::fromName(topStr);

    auto belowStr = doc.getString("on");
    if (!belowStr.empty()) config.requiredBelow = BlockTypeId::fromName(belowStr);

    config.attempts = doc.getInt("attempts", config.attempts);
    config.radius = doc.getInt("radius", config.radius);
    config.minHeight = doc.getInt("min_stack", config.minHeight);
    config.maxHeight = doc.getInt("max_stack", config.maxHeight);

    return config;
}

std::optional<FeaturePlacement> FeatureLoader::parsePlacement(const ConfigDocument& doc,
                                                              std::string_view featureName) {
    if (!doc.get("density")) return std::nullopt;

    FeaturePlacement placement;
    placement.featureName = std::string(featureName);
    placement.density = doc.getFloat("density", placement.density);
    placement.minHeight = doc.getInt("place_min_height", placement.minHeight);
    placement.maxHeight = doc.getInt("place_max_height", placement.maxHeight);
    placement.requiresSurface = doc.getBool("requires_surface", placement.requiresSurface);

    auto surfaceStr = doc.getString("surface");
    if (!surfaceStr.empty()) placement.requiredSurface = BlockTypeId::fromName(surfaceStr);
    placement.peaked = doc.getString("height_shape") == "peaked";

    // biomes: names, in the feature's namespace unless they give their own
    auto colon = featureName.find(':');
    std::string ns = colon == std::string_view::npos ? "" : std::string(featureName.substr(0, colon + 1));
    std::istringstream biomes{std::string(doc.getString("biomes"))};
    std::string biome;
    while (biomes >> biome) {
        placement.biomes.push_back(BiomeId::fromName(biome.find(':') == std::string::npos ? ns + biome : biome));
    }

    return placement;
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

            ConfigParser placementParser;
            if (auto doc = placementParser.parseFile(entry.path().string())) {
                if (auto placement = parsePlacement(*doc, name)) {
                    FeatureRegistry::global().addPlacement(std::move(*placement));
                }
            }
        }
    }

    return count;
}

}  // namespace finevox::worldgen
