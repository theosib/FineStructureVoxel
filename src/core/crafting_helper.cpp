#include "finevox/core/crafting_helper.hpp"
#include "finevox/core/recipe_registry.hpp"

namespace finevox {

// ============================================================================
// Private helpers
// ============================================================================

bool CraftingHelper::computeBoundingBox(const ItemTypeId* gridSlots, int32_t gridWidth,
                                         int32_t gridHeight,
                                         int32_t& outMinX, int32_t& outMinY,
                                         int32_t& outMaxX, int32_t& outMaxY) {
    outMinX = gridWidth;
    outMinY = gridHeight;
    outMaxX = -1;
    outMaxY = -1;

    for (int32_t y = 0; y < gridHeight; ++y) {
        for (int32_t x = 0; x < gridWidth; ++x) {
            if (!gridSlots[y * gridWidth + x].isEmpty()) {
                outMinX = std::min(outMinX, x);
                outMinY = std::min(outMinY, y);
                outMaxX = std::max(outMaxX, x);
                outMaxY = std::max(outMaxY, y);
            }
        }
    }

    return outMaxX >= 0;  // false if grid is entirely empty
}

bool CraftingHelper::testPatternAt(const Recipe& recipe, const ItemTypeId* gridSlots,
                                    int32_t gridWidth, [[maybe_unused]] int32_t gridHeight,
                                    int32_t offsetX, int32_t offsetY, bool mirrored) {
    for (int32_t py = 0; py < recipe.height; ++py) {
        for (int32_t px = 0; px < recipe.width; ++px) {
            int32_t patternX = mirrored ? (recipe.width - 1 - px) : px;
            int32_t gridIdx = (offsetY + py) * gridWidth + (offsetX + px);
            int32_t patternIdx = py * recipe.width + patternX;

            if (!recipe.pattern[patternIdx].matches(gridSlots[gridIdx])) {
                return false;
            }
        }
    }
    return true;
}

bool CraftingHelper::backtrackMatch(const std::vector<ItemMatch>& ingredients,
                                     const std::vector<ItemTypeId>& items,
                                     uint32_t used, int32_t idx) {
    if (idx == static_cast<int32_t>(ingredients.size())) {
        return true;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(items.size()); ++i) {
        if (used & (1u << i)) continue;
        if (ingredients[idx].matches(items[i])) {
            if (backtrackMatch(ingredients, items, used | (1u << i), idx + 1)) {
                return true;
            }
        }
    }
    return false;
}

bool CraftingHelper::backtrackAssign(const std::vector<ItemMatch>& ingredients,
                                      const std::vector<ItemTypeId>& items,
                                      uint32_t used, int32_t idx,
                                      std::vector<int32_t>& assignment) {
    if (idx == static_cast<int32_t>(ingredients.size())) {
        return true;
    }

    for (int32_t i = 0; i < static_cast<int32_t>(items.size()); ++i) {
        if (used & (1u << i)) continue;
        if (ingredients[idx].matches(items[i])) {
            assignment[idx] = i;
            if (backtrackAssign(ingredients, items, used | (1u << i), idx + 1, assignment)) {
                return true;
            }
        }
    }
    return false;
}

// ============================================================================
// Shaped matching
// ============================================================================

CraftingHelper::MatchResult CraftingHelper::matchShaped(const Recipe& recipe,
                                                          const ItemTypeId* gridSlots,
                                                          int32_t gridWidth,
                                                          int32_t gridHeight) {
    if (recipe.type != RecipeType::Shaped) return {};
    if (recipe.pattern.empty()) return {};

    int32_t minX, minY, maxX, maxY;
    if (!computeBoundingBox(gridSlots, gridWidth, gridHeight, minX, minY, maxX, maxY)) {
        return {};  // Empty grid — no match
    }

    int32_t bbW = maxX - minX + 1;
    int32_t bbH = maxY - minY + 1;

    if (bbW != recipe.width || bbH != recipe.height) {
        return {};  // Dimensions don't match
    }

    // Try normal orientation
    if (testPatternAt(recipe, gridSlots, gridWidth, gridHeight, minX, minY, false)) {
        return {&recipe, false, minX, minY};
    }

    // Try mirrored if allowed
    if (recipe.allowMirror) {
        if (testPatternAt(recipe, gridSlots, gridWidth, gridHeight, minX, minY, true)) {
            return {&recipe, true, minX, minY};
        }
    }

    return {};
}

// ============================================================================
// Shapeless matching
// ============================================================================

bool CraftingHelper::matchShapeless(const Recipe& recipe, const ItemTypeId* gridSlots,
                                     int32_t gridWidth, int32_t gridHeight) {
    if (recipe.type != RecipeType::Shapeless) return false;

    // Collect non-empty items from grid
    std::vector<ItemTypeId> items;
    for (int32_t i = 0; i < gridWidth * gridHeight; ++i) {
        if (!gridSlots[i].isEmpty()) {
            items.push_back(gridSlots[i]);
        }
    }

    if (items.size() != recipe.ingredients.size()) return false;

    return backtrackMatch(recipe.ingredients, items, 0, 0);
}

// ============================================================================
// findRecipe
// ============================================================================

CraftingHelper::MatchResult CraftingHelper::findRecipe(const ItemTypeId* gridSlots,
                                                         int32_t gridWidth,
                                                         int32_t gridHeight,
                                                         StationTypeId station) {
    auto recipes = RecipeRegistry::global().getRecipesForStation(station);
    // A station can also craft hand-crafting recipes
    if (station != EMPTY_STATION) {
        auto handRecipes = RecipeRegistry::global().getRecipesForStation(EMPTY_STATION);
        recipes.insert(recipes.end(), handRecipes.begin(), handRecipes.end());
    }

    for (const auto* recipe : recipes) {
        if (recipe->isShaped()) {
            auto result = matchShaped(*recipe, gridSlots, gridWidth, gridHeight);
            if (result.recipe) return result;
        } else if (recipe->isShapeless()) {
            if (matchShapeless(*recipe, gridSlots, gridWidth, gridHeight)) {
                return {recipe, false, 0, 0};
            }
        }
        // Skip smelting recipes
    }

    return {};
}

// ============================================================================
// canCraft
// ============================================================================

bool CraftingHelper::canCraft(const Recipe& recipe, const ItemTypeId* gridSlots,
                               int32_t gridWidth, int32_t gridHeight) {
    if (recipe.isSmelting()) return false;

    if (recipe.isShaped()) {
        return matchShaped(recipe, gridSlots, gridWidth, gridHeight).recipe != nullptr;
    }
    if (recipe.isShapeless()) {
        return matchShapeless(recipe, gridSlots, gridWidth, gridHeight);
    }
    return false;
}

// ============================================================================
// executeCraft
// ============================================================================

bool CraftingHelper::executeCraft(const MatchResult& result, InventoryView& craftingGrid,
                                   int32_t gridWidth, int32_t gridHeight) {
    if (!result.recipe) return false;

    const Recipe& recipe = *result.recipe;

    if (recipe.isShaped()) {
        // Decrement each non-empty pattern slot by 1
        for (int32_t py = 0; py < recipe.height; ++py) {
            for (int32_t px = 0; px < recipe.width; ++px) {
                int32_t patternX = result.mirrored ? (recipe.width - 1 - px) : px;
                int32_t patternIdx = py * recipe.width + patternX;

                if (recipe.pattern[patternIdx].isEmpty()) continue;

                int32_t gridSlotIdx = (result.offsetY + py) * gridWidth + (result.offsetX + px);
                auto stack = craftingGrid.getSlot(gridSlotIdx);
                stack.count -= 1;
                if (stack.count <= 0) {
                    stack.clear();
                }
                craftingGrid.setSlot(gridSlotIdx, stack);
            }
        }
        return true;
    }

    if (recipe.isShapeless()) {
        // Collect non-empty (slotIndex, ItemTypeId) pairs
        struct SlotItem {
            int32_t slotIndex;
            ItemTypeId type;
        };
        std::vector<SlotItem> slotItems;
        for (int32_t i = 0; i < gridWidth * gridHeight; ++i) {
            auto stack = craftingGrid.getSlot(i);
            if (!stack.isEmpty()) {
                slotItems.push_back({i, stack.type});
            }
        }

        if (slotItems.size() != recipe.ingredients.size()) return false;

        // Find assignment via backtracking
        std::vector<ItemTypeId> itemTypes;
        itemTypes.reserve(slotItems.size());
        for (const auto& si : slotItems) {
            itemTypes.push_back(si.type);
        }

        std::vector<int32_t> assignment(recipe.ingredients.size(), -1);
        if (!backtrackAssign(recipe.ingredients, itemTypes, 0, 0, assignment)) {
            return false;
        }

        // Decrement each assigned slot by 1
        for (size_t i = 0; i < assignment.size(); ++i) {
            int32_t slotIdx = slotItems[assignment[i]].slotIndex;
            auto stack = craftingGrid.getSlot(slotIdx);
            stack.count -= 1;
            if (stack.count <= 0) {
                stack.clear();
            }
            craftingGrid.setSlot(slotIdx, stack);
        }
        return true;
    }

    return false;  // Smelting not handled here
}

}  // namespace finevox
