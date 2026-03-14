#pragma once

/**
 * @file crafting_helper.hpp
 * @brief CraftingHelper — stateless utility for recipe matching and execution
 *
 * Design: Phase 22 Crafting + Recipe System (Sub-Phase 22-2)
 *
 * Provides static methods for:
 *   - Shaped recipe matching (bounding box alignment, optional mirror)
 *   - Shapeless recipe matching (backtracking assignment)
 *   - Recipe lookup against a crafting grid
 *   - Crafting execution (consume ingredients from grid)
 *
 * Thread-safe: all methods are stateless, reading only from global registries.
 */

#include "finevox/core/recipe.hpp"
#include "finevox/core/inventory.hpp"

#include <cstdint>
#include <vector>

namespace finevox {

class CraftingHelper {
public:
    /// Result of a recipe match attempt
    struct MatchResult {
        const Recipe* recipe = nullptr;  ///< Matched recipe (nullptr if no match)
        bool mirrored = false;           ///< Whether match was on horizontally flipped pattern
        int32_t offsetX = 0;             ///< Column offset where bounding box starts in grid
        int32_t offsetY = 0;             ///< Row offset where bounding box starts in grid
    };

    /// Find the first matching recipe for items in a crafting grid.
    /// Tries shaped and shapeless recipes at the given station.
    /// Smelting recipes are skipped (use RecipeRegistry::findSmeltingRecipe instead).
    static MatchResult findRecipe(const ItemTypeId* gridSlots, int32_t gridWidth,
                                   int32_t gridHeight, StationTypeId station);

    /// Check if a recipe can be crafted with the given grid contents (no mutation).
    /// Returns false for smelting recipes.
    static bool canCraft(const Recipe& recipe, const ItemTypeId* gridSlots,
                          int32_t gridWidth, int32_t gridHeight);

    /// Execute crafting: decrement one of each consumed ingredient in the grid.
    /// @param result       The match result from findRecipe/matchShaped
    /// @param craftingGrid InventoryView over the crafting grid slots
    /// @param gridWidth    Number of columns in the grid
    /// @param gridHeight   Number of rows in the grid
    /// @return true if execution succeeded
    static bool executeCraft(const MatchResult& result, InventoryView& craftingGrid,
                              int32_t gridWidth, int32_t gridHeight);

    /// Test if a shaped recipe matches the grid (including mirror check).
    static MatchResult matchShaped(const Recipe& recipe, const ItemTypeId* gridSlots,
                                    int32_t gridWidth, int32_t gridHeight);

    /// Test if a shapeless recipe matches the items in the grid.
    static bool matchShapeless(const Recipe& recipe, const ItemTypeId* gridSlots,
                                int32_t gridWidth, int32_t gridHeight);

private:
    /// Compute the bounding box of non-empty items in the grid.
    /// Returns false if grid is entirely empty.
    static bool computeBoundingBox(const ItemTypeId* gridSlots, int32_t gridWidth,
                                    int32_t gridHeight,
                                    int32_t& outMinX, int32_t& outMinY,
                                    int32_t& outMaxX, int32_t& outMaxY);

    /// Test shaped pattern at a specific offset, optionally mirrored.
    static bool testPatternAt(const Recipe& recipe, const ItemTypeId* gridSlots,
                               int32_t gridWidth, int32_t gridHeight,
                               int32_t offsetX, int32_t offsetY, bool mirrored);

    /// Recursive backtracking for shapeless matching.
    static bool backtrackMatch(const std::vector<ItemMatch>& ingredients,
                                const std::vector<ItemTypeId>& items,
                                uint32_t used, int32_t idx);

    /// Backtracking with assignment output (for execution).
    static bool backtrackAssign(const std::vector<ItemMatch>& ingredients,
                                 const std::vector<ItemTypeId>& items,
                                 uint32_t used, int32_t idx,
                                 std::vector<int32_t>& assignment);
};

}  // namespace finevox
