#pragma once

/**
 * @file recipe_registry.hpp
 * @brief RecipeRegistry — global singleton for named crafting recipes
 *
 * Design: Phase 22 Crafting + Recipe System (Sub-Phase 22-1)
 *
 * Thread-safe registry mapping RecipeId to Recipe data.
 * Secondary indices provide fast lookup by station type and output item.
 */

#include "finevox/core/recipe.hpp"
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class RecipeRegistry {
public:
    static RecipeRegistry& global();

    /// Register a recipe by name. Returns false if name already registered.
    bool registerRecipe(std::string_view name, Recipe recipe);

    /// Look up by RecipeId
    [[nodiscard]] const Recipe* getRecipe(RecipeId id) const;

    /// Look up by name
    [[nodiscard]] const Recipe* getRecipe(std::string_view name) const;

    /// Check existence
    [[nodiscard]] bool hasRecipe(RecipeId id) const;
    [[nodiscard]] bool hasRecipe(std::string_view name) const;

    /// Find all recipes requiring a specific station type
    [[nodiscard]] std::vector<const Recipe*> getRecipesForStation(StationTypeId station) const;

    /// Find all recipes that produce a given output item
    [[nodiscard]] std::vector<const Recipe*> getRecipesForOutput(ItemTypeId output) const;

    /// Find smelting recipe for a given input item at a station
    /// Returns nullptr if no matching recipe
    [[nodiscard]] const Recipe* findSmeltingRecipe(ItemTypeId input,
                                                    StationTypeId station) const;

    /// Get all registered recipes
    [[nodiscard]] std::vector<const Recipe*> allRecipes() const;

    /// Number of registered recipes
    [[nodiscard]] size_t size() const;

    /// Clear all (for testing)
    void clear();

    RecipeRegistry(const RecipeRegistry&) = delete;
    RecipeRegistry& operator=(const RecipeRegistry&) = delete;

private:
    RecipeRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<RecipeId, Recipe> recipes_;

    // Secondary indices for fast lookup
    std::unordered_map<StationTypeId, std::vector<RecipeId>> byStation_;
    std::unordered_map<ItemTypeId, std::vector<RecipeId>> byOutput_;
};

}  // namespace finevox
