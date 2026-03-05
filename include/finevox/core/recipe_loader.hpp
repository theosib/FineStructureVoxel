#pragma once

/**
 * @file recipe_loader.hpp
 * @brief Loads recipes from .recipe config files
 *
 * Design: Phase 22 Crafting + Recipe System (Sub-Phase 22-1)
 *
 * File format (uses ConfigParser):
 *
 * Shaped:
 *   name: finevox:workbench
 *   type: shaped
 *   station: none
 *   category: building
 *   pattern:
 *       width: 2
 *       height: 2
 *       row: #common:planks #common:planks
 *       row: #common:planks #common:planks
 *   mirror: true
 *   output: finevox:workbench
 *   count: 1
 *
 * Shapeless:
 *   name: finevox:oak_planks
 *   type: shapeless
 *   station: none
 *   category: materials
 *   ingredient: finevox:oak_log
 *   output: finevox:oak_planks
 *   count: 4
 *
 * Smelting:
 *   name: finevox:iron_ingot
 *   type: smelting
 *   station: finevox:furnace
 *   category: materials
 *   input: finevox:raw_iron
 *   smelt_time: 200
 *   experience: 0.7
 *   output: finevox:iron_ingot
 *   count: 1
 *
 * Ingredient naming:
 *   #tag:name  → ItemMatch::tagged() (leading '#' denotes a tag)
 *   item:name  → ItemMatch::exact()
 *   _ or empty → ItemMatch::empty()
 */

#include "finevox/core/recipe.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace finevox {

class RecipeLoader {
public:
    /// Load a recipe from a .recipe file
    [[nodiscard]] static std::optional<Recipe> loadFromFile(const std::string& path);

    /// Load a recipe from a string (for testing)
    [[nodiscard]] static std::optional<Recipe> loadFromString(std::string_view content);

    /// Load all .recipe files from a directory, registering each in RecipeRegistry.
    /// Returns number of recipes successfully loaded.
    static size_t loadDirectory(const std::string& dirPath);

    /// Parse an ingredient string into an ItemMatch.
    /// '#tag:name' → tagged, '_' or 'empty' → empty, otherwise → exact.
    [[nodiscard]] static ItemMatch parseIngredient(std::string_view str);
};

}  // namespace finevox
