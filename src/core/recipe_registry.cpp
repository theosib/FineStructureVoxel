#include "finevox/core/recipe_registry.hpp"

namespace finevox {

RecipeRegistry& RecipeRegistry::global() {
    static RecipeRegistry instance;
    return instance;
}

bool RecipeRegistry::registerRecipe(std::string_view name, Recipe recipe) {
    auto id = RecipeId::fromName(name);
    recipe.id = id;

    std::unique_lock lock(mutex_);
    auto [it, inserted] = recipes_.emplace(id, std::move(recipe));
    if (!inserted) {
        return false;
    }

    // Build secondary indices
    const Recipe& stored = it->second;
    byStation_[stored.station].push_back(id);
    if (!stored.outputItem.isEmpty()) {
        byOutput_[stored.outputItem].push_back(id);
    }

    return true;
}

const Recipe* RecipeRegistry::getRecipe(RecipeId id) const {
    std::shared_lock lock(mutex_);
    auto it = recipes_.find(id);
    return it != recipes_.end() ? &it->second : nullptr;
}

const Recipe* RecipeRegistry::getRecipe(std::string_view name) const {
    return getRecipe(RecipeId::fromName(name));
}

bool RecipeRegistry::hasRecipe(RecipeId id) const {
    std::shared_lock lock(mutex_);
    return recipes_.count(id) > 0;
}

bool RecipeRegistry::hasRecipe(std::string_view name) const {
    return hasRecipe(RecipeId::fromName(name));
}

std::vector<const Recipe*> RecipeRegistry::getRecipesForStation(StationTypeId station) const {
    std::shared_lock lock(mutex_);
    std::vector<const Recipe*> result;

    auto it = byStation_.find(station);
    if (it != byStation_.end()) {
        result.reserve(it->second.size());
        for (const auto& recipeId : it->second) {
            auto rit = recipes_.find(recipeId);
            if (rit != recipes_.end()) {
                result.push_back(&rit->second);
            }
        }
    }

    return result;
}

std::vector<const Recipe*> RecipeRegistry::getRecipesForOutput(ItemTypeId output) const {
    std::shared_lock lock(mutex_);
    std::vector<const Recipe*> result;

    auto it = byOutput_.find(output);
    if (it != byOutput_.end()) {
        result.reserve(it->second.size());
        for (const auto& recipeId : it->second) {
            auto rit = recipes_.find(recipeId);
            if (rit != recipes_.end()) {
                result.push_back(&rit->second);
            }
        }
    }

    return result;
}

const Recipe* RecipeRegistry::findSmeltingRecipe(ItemTypeId input,
                                                  StationTypeId station) const {
    std::shared_lock lock(mutex_);

    auto it = byStation_.find(station);
    if (it == byStation_.end()) {
        return nullptr;
    }

    for (const auto& recipeId : it->second) {
        auto rit = recipes_.find(recipeId);
        if (rit == recipes_.end()) continue;

        const Recipe& recipe = rit->second;
        if (recipe.type != RecipeType::Smelting) continue;
        if (recipe.smeltInput.matches(input)) {
            return &recipe;
        }
    }

    return nullptr;
}

std::vector<const Recipe*> RecipeRegistry::allRecipes() const {
    std::shared_lock lock(mutex_);
    std::vector<const Recipe*> result;
    result.reserve(recipes_.size());
    for (const auto& [id, recipe] : recipes_) {
        result.push_back(&recipe);
    }
    return result;
}

size_t RecipeRegistry::size() const {
    std::shared_lock lock(mutex_);
    return recipes_.size();
}

void RecipeRegistry::clear() {
    std::unique_lock lock(mutex_);
    recipes_.clear();
    byStation_.clear();
    byOutput_.clear();
}

}  // namespace finevox
