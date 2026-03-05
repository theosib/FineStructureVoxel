#include "finevox/core/recipe_loader.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/config_parser.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace finevox {

// ConfigParser splits "key: ns:value" into key="key", suffix="ns", value="value".
// This helper reconstructs the full value as "ns:value" when a suffix is present.
static std::string getFullValue(const ConfigEntry& entry) {
    if (entry.hasSuffix()) {
        return entry.suffix + ":" + entry.value.asStringOwned();
    }
    return entry.value.asStringOwned();
}

// Parse space-separated ingredient tokens from a row string
static std::vector<ItemMatch> parseRow(std::string_view row) {
    std::vector<ItemMatch> result;
    std::string tmp{row};
    std::istringstream iss{tmp};
    std::string token;
    while (iss >> token) {
        result.push_back(RecipeLoader::parseIngredient(token));
    }
    return result;
}

ItemMatch RecipeLoader::parseIngredient(std::string_view str) {
    if (str.empty() || str == "_" || str == "empty") {
        return ItemMatch::empty();
    }
    if (str.starts_with("#")) {
        // Tag reference: #common:planks -> tagged(TagId::fromName("common:planks"))
        return ItemMatch::tagged(TagId::fromName(str.substr(1)));
    }
    // Exact item reference
    return ItemMatch::exact(ItemTypeId::fromName(str));
}

std::optional<Recipe> RecipeLoader::loadFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);

    if (doc.empty()) return std::nullopt;

    Recipe recipe;
    std::string name;

    // Collect pattern rows separately (they appear as multiple "row" entries
    // inside the pattern block, but ConfigParser flattens to sequential entries)
    std::vector<std::vector<ItemMatch>> rows;

    for (const auto& entry : doc.entries()) {
        const auto& key = entry.key;
        const auto& val = entry.value;

        if (key == "name") {
            name = getFullValue(entry);
        }
        else if (key == "type") {
            auto typeStr = val.asString();
            if (typeStr == "shaped") {
                recipe.type = RecipeType::Shaped;
            } else if (typeStr == "shapeless") {
                recipe.type = RecipeType::Shapeless;
            } else if (typeStr == "smelting") {
                recipe.type = RecipeType::Smelting;
            }
        }
        else if (key == "station") {
            auto fullVal = getFullValue(entry);
            if (fullVal == "none" || fullVal.empty()) {
                recipe.station = EMPTY_STATION;
            } else {
                recipe.station = StationTypeId::fromName(fullVal);
            }
        }
        else if (key == "category") {
            recipe.category = getFullValue(entry);
        }
        // --- Pattern (shaped) ---
        else if (key == "width") {
            recipe.width = val.asInt(0);
        }
        else if (key == "height") {
            recipe.height = val.asInt(0);
        }
        else if (key == "row") {
            rows.push_back(parseRow(getFullValue(entry)));
        }
        else if (key == "mirror") {
            recipe.allowMirror = val.asBool(false);
        }
        // --- Shapeless ---
        else if (key == "ingredient") {
            recipe.ingredients.push_back(parseIngredient(getFullValue(entry)));
        }
        // --- Smelting ---
        else if (key == "input") {
            recipe.smeltInput = parseIngredient(getFullValue(entry));
        }
        else if (key == "smelt_time") {
            recipe.smeltTicks = val.asInt(200);
        }
        else if (key == "experience") {
            recipe.experience = val.asFloat(0.0f);
        }
        // --- Output ---
        else if (key == "output") {
            recipe.outputItem = ItemTypeId::fromName(getFullValue(entry));
        }
        else if (key == "count") {
            recipe.outputCount = val.asInt(1);
        }
    }

    if (name.empty()) return std::nullopt;

    // Assemble shaped pattern from collected rows
    if (recipe.type == RecipeType::Shaped && !rows.empty()) {
        // Infer dimensions from rows if not explicitly set
        if (recipe.height == 0) {
            recipe.height = static_cast<int32_t>(rows.size());
        }
        if (recipe.width == 0 && !rows.empty()) {
            // Use the widest row
            for (const auto& row : rows) {
                recipe.width = std::max(recipe.width,
                    static_cast<int32_t>(row.size()));
            }
        }

        // Build flat pattern (row-major)
        recipe.pattern.clear();
        recipe.pattern.reserve(recipe.width * recipe.height);
        for (int32_t y = 0; y < recipe.height; ++y) {
            for (int32_t x = 0; x < recipe.width; ++x) {
                if (y < static_cast<int32_t>(rows.size()) &&
                    x < static_cast<int32_t>(rows[y].size())) {
                    recipe.pattern.push_back(rows[y][x]);
                } else {
                    recipe.pattern.push_back(ItemMatch::empty());
                }
            }
        }
    }

    // Set the recipe ID from the name
    recipe.id = RecipeId::fromName(name);

    return recipe;
}

std::optional<Recipe> RecipeLoader::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromString(content);
}

size_t RecipeLoader::loadDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".recipe") continue;

        auto recipe = loadFromFile(entry.path().string());
        if (recipe) {
            auto name = recipe->id.name();
            if (RecipeRegistry::global().registerRecipe(name, std::move(*recipe))) {
                ++count;
            }
        }
    }

    return count;
}

}  // namespace finevox
