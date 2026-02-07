/**
 * @file biome_loader.cpp
 * @brief ConfigParser-based .biome file loader
 *
 * Design: [27-world-generation.md] Section 27.3.5
 */

#include "finevox/worldgen/biome_loader.hpp"
#include "finevox/core/config_parser.hpp"

#include <filesystem>

namespace finevox::worldgen {

std::optional<BiomeProperties> BiomeLoader::loadFromFile(
    std::string_view biomeName, const std::string& configPath) {

    ConfigParser parser;
    auto doc = parser.parseFile(configPath);
    if (!doc) return std::nullopt;

    return loadFromConfig(biomeName, *doc);
}

std::optional<BiomeProperties> BiomeLoader::loadFromConfig(
    std::string_view biomeName, const ConfigDocument& doc) {

    BiomeProperties props;
    props.id = BiomeId::fromName(biomeName);

    // Display name
    if (auto* entry = doc.get("name")) {
        props.displayName = entry->value.asStringOwned();
    } else {
        props.displayName = std::string(biomeName);
    }

    // Climate
    if (auto* e = doc.get("temperature_min")) props.temperatureMin = e->value.asFloat();
    if (auto* e = doc.get("temperature_max")) props.temperatureMax = e->value.asFloat();
    if (auto* e = doc.get("humidity_min")) props.humidityMin = e->value.asFloat();
    if (auto* e = doc.get("humidity_max")) props.humidityMax = e->value.asFloat();

    // Terrain
    if (auto* e = doc.get("base_height")) props.baseHeight = e->value.asFloat();
    if (auto* e = doc.get("height_variation")) props.heightVariation = e->value.asFloat();
    if (auto* e = doc.get("height_scale")) props.heightScale = e->value.asFloat();

    // Surface blocks
    if (auto* e = doc.get("surface")) props.surfaceBlock = e->value.asStringOwned();
    if (auto* e = doc.get("filler")) props.fillerBlock = e->value.asStringOwned();
    if (auto* e = doc.get("filler_depth")) props.fillerDepth = e->value.asInt();
    if (auto* e = doc.get("stone")) props.stoneBlock = e->value.asStringOwned();
    if (auto* e = doc.get("underwater")) props.underwaterBlock = e->value.asStringOwned();

    // Feature densities
    if (auto* e = doc.get("tree_density")) props.treeDensity = e->value.asFloat();
    if (auto* e = doc.get("ore_density")) props.oreDensity = e->value.asFloat();
    if (auto* e = doc.get("decoration_density")) props.decorationDensity = e->value.asFloat();

    return props;
}

size_t BiomeLoader::loadDirectory(const std::string& dirPath,
                                   const std::string& namePrefix) {
    size_t count = 0;
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return 0;
    }

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".biome") {
            std::string stem = entry.path().stem().string();
            std::string biomeName = namePrefix.empty() ? stem : namePrefix + ":" + stem;

            auto props = loadFromFile(biomeName, entry.path().string());
            if (props) {
                BiomeRegistry::global().registerBiome(biomeName, std::move(*props));
                ++count;
            }
        }
    }

    return count;
}

}  // namespace finevox::worldgen
