#include <gtest/gtest.h>
#include "finevox/core/crafting_helper.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/recipe_loader.hpp"
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/name_registry.hpp"
#include "finevox/core/data_container.hpp"

using namespace finevox;

// ============================================================================
// Test fixture with helper methods
// ============================================================================

class CraftingHelperTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }

    void TearDown() override {
        RecipeRegistry::global().clear();
    }

    // Create a 3x3 grid initialized to empty
    std::array<ItemTypeId, 9> makeGrid() {
        std::array<ItemTypeId, 9> grid;
        grid.fill(EMPTY_ITEM_TYPE);
        return grid;
    }

    // Create a 2x2 grid initialized to empty
    std::array<ItemTypeId, 4> makeGrid2x2() {
        std::array<ItemTypeId, 4> grid;
        grid.fill(EMPTY_ITEM_TYPE);
        return grid;
    }

    // Item type helpers
    ItemTypeId iron() { return ItemTypeId::fromName("ctest_iron"); }
    ItemTypeId stick() { return ItemTypeId::fromName("ctest_stick"); }
    ItemTypeId planks() { return ItemTypeId::fromName("ctest_planks"); }
    ItemTypeId stone() { return ItemTypeId::fromName("ctest_stone"); }
    ItemTypeId log() { return ItemTypeId::fromName("ctest_log"); }
    ItemTypeId copper() { return ItemTypeId::fromName("ctest_copper"); }
    ItemTypeId gold() { return ItemTypeId::fromName("ctest_gold"); }

    // Register a shaped recipe (convenience)
    void registerShaped(const char* name, int32_t w, int32_t h,
                        const std::vector<ItemMatch>& pattern,
                        ItemTypeId output, int32_t count = 1,
                        StationTypeId station = EMPTY_STATION,
                        bool mirror = false) {
        Recipe r;
        r.type = RecipeType::Shaped;
        r.width = w;
        r.height = h;
        r.pattern = pattern;
        r.outputItem = output;
        r.outputCount = count;
        r.station = station;
        r.allowMirror = mirror;
        RecipeRegistry::global().registerRecipe(name, r);
    }

    // Register a shapeless recipe (convenience)
    void registerShapeless(const char* name,
                           const std::vector<ItemMatch>& ingredients,
                           ItemTypeId output, int32_t count = 1,
                           StationTypeId station = EMPTY_STATION) {
        Recipe r;
        r.type = RecipeType::Shapeless;
        r.ingredients = ingredients;
        r.outputItem = output;
        r.outputCount = count;
        r.station = station;
        RecipeRegistry::global().registerRecipe(name, r);
    }

    // Register a smelting recipe (convenience)
    void registerSmelting(const char* name, ItemMatch input, ItemTypeId output,
                          StationTypeId station = StationTypeId::fromName("ctest_furnace")) {
        Recipe r;
        r.type = RecipeType::Smelting;
        r.smeltInput = input;
        r.outputItem = output;
        r.station = station;
        RecipeRegistry::global().registerRecipe(name, r);
    }
};

// ============================================================================
// Shaped Matching Tests
// ============================================================================

TEST_F(CraftingHelperTest, ShapedExactMatch2x2) {
    // 2x2 recipe in a 2x2 grid
    registerShaped("ctest:bench", 2, 2, {
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_bench_out"));

    auto grid = makeGrid2x2();
    grid[0] = planks(); grid[1] = planks();
    grid[2] = planks(); grid[3] = planks();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:bench"),
        grid.data(), 2, 2);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_FALSE(result.mirrored);
}

TEST_F(CraftingHelperTest, ShapedNoMatchWrongItem) {
    registerShaped("ctest:wrong", 2, 2, {
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_wrong_out"));

    auto grid = makeGrid2x2();
    grid[0] = planks(); grid[1] = stone();  // wrong!
    grid[2] = planks(); grid[3] = planks();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:wrong"),
        grid.data(), 2, 2);
    EXPECT_EQ(result.recipe, nullptr);
}

TEST_F(CraftingHelperTest, ShapedNoMatchWrongDimensions) {
    // 2x2 recipe, but items form a 3x1 strip
    registerShaped("ctest:dim", 2, 2, {
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_dim_out"));

    auto grid = makeGrid();  // 3x3
    grid[0] = planks(); grid[1] = planks(); grid[2] = planks();
    // bounding box is 3x1, recipe is 2x2 → no match

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:dim"),
        grid.data(), 3, 3);
    EXPECT_EQ(result.recipe, nullptr);
}

TEST_F(CraftingHelperTest, ShapedMatchWithOffset) {
    // 2x2 recipe, items placed at bottom-right of 3x3 grid
    registerShaped("ctest:offset", 2, 2, {
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
        ItemMatch::exact(planks()), ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_offset_out"));

    auto grid = makeGrid();
    // Place at (1,1)-(2,2)
    grid[1*3+1] = planks(); grid[1*3+2] = planks();
    grid[2*3+1] = planks(); grid[2*3+2] = planks();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:offset"),
        grid.data(), 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 1);
    EXPECT_EQ(result.offsetY, 1);
}

TEST_F(CraftingHelperTest, ShapedMatchTopLeft) {
    // 2x2 recipe in top-left of 3x3 grid
    registerShaped("ctest:topleft", 2, 2, {
        ItemMatch::exact(iron()), ItemMatch::exact(iron()),
        ItemMatch::exact(stick()), ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_topleft_out"));

    auto grid = makeGrid();
    grid[0] = iron();  grid[1] = iron();
    grid[3] = stick(); grid[4] = stick();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:topleft"),
        grid.data(), 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 0);
    EXPECT_EQ(result.offsetY, 0);
}

TEST_F(CraftingHelperTest, ShapedMirrorMatch) {
    // Asymmetric 2x2 recipe with mirror allowed
    // Pattern: [iron, stick]
    //          [_,    stick]
    registerShaped("ctest:mirror", 2, 2, {
        ItemMatch::exact(iron()), ItemMatch::exact(stick()),
        ItemMatch::empty(),       ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_mirror_out"), 1, EMPTY_STATION, true);

    // Place mirrored version: [stick, iron]
    //                         [stick, _   ]
    auto grid = makeGrid2x2();
    grid[0] = stick(); grid[1] = iron();
    grid[2] = stick(); grid[3] = EMPTY_ITEM_TYPE;

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:mirror"),
        grid.data(), 2, 2);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_TRUE(result.mirrored);
}

TEST_F(CraftingHelperTest, ShapedMirrorNoMatchWhenDisabled) {
    // Same asymmetric recipe but mirror=false
    registerShaped("ctest:nomirror", 2, 2, {
        ItemMatch::exact(iron()), ItemMatch::exact(stick()),
        ItemMatch::empty(),       ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_nomirror_out"), 1, EMPTY_STATION, false);

    // Place mirrored version
    auto grid = makeGrid2x2();
    grid[0] = stick(); grid[1] = iron();
    grid[2] = stick(); grid[3] = EMPTY_ITEM_TYPE;

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:nomirror"),
        grid.data(), 2, 2);
    EXPECT_EQ(result.recipe, nullptr);
}

TEST_F(CraftingHelperTest, ShapedWithEmptyCells) {
    // 3x1 sword: [iron, iron, stick] as 1x3
    registerShaped("ctest:sword", 1, 3, {
        ItemMatch::exact(iron()),
        ItemMatch::exact(iron()),
        ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_sword_out"));

    auto grid = makeGrid();
    grid[0*3+1] = iron();
    grid[1*3+1] = iron();
    grid[2*3+1] = stick();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:sword"),
        grid.data(), 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 1);
    EXPECT_EQ(result.offsetY, 0);
}

TEST_F(CraftingHelperTest, ShapedSingleItem) {
    registerShaped("ctest:single", 1, 1, {
        ItemMatch::exact(log()),
    }, planks(), 4);

    auto grid = makeGrid();
    grid[4] = log();  // center of 3x3

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:single"),
        grid.data(), 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 1);
    EXPECT_EQ(result.offsetY, 1);
}

TEST_F(CraftingHelperTest, ShapedFull3x3) {
    std::vector<ItemMatch> pattern(9, ItemMatch::exact(planks()));
    registerShaped("ctest:full3x3", 3, 3, pattern,
                   ItemTypeId::fromName("ctest_full_out"));

    auto grid = makeGrid();
    grid.fill(planks());

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:full3x3"),
        grid.data(), 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 0);
    EXPECT_EQ(result.offsetY, 0);
}

TEST_F(CraftingHelperTest, ShapedEmptyGrid) {
    registerShaped("ctest:emptygrid", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_empty_out"));

    auto grid = makeGrid();  // all empty

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:emptygrid"),
        grid.data(), 3, 3);
    EXPECT_EQ(result.recipe, nullptr);
}

// ============================================================================
// Shapeless Matching Tests
// ============================================================================

TEST_F(CraftingHelperTest, ShapelessExactMatch) {
    registerShapeless("ctest:shapeless1", {
        ItemMatch::exact(iron()),
        ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_shapeless_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[5] = stick();

    EXPECT_TRUE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:shapeless1"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, ShapelessWrongCount) {
    registerShapeless("ctest:sl_count", {
        ItemMatch::exact(iron()),
        ItemMatch::exact(stick()),
        ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_sl_count_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[1] = stick();
    // Only 2 items, recipe needs 3

    EXPECT_FALSE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:sl_count"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, ShapelessExtraItems) {
    registerShapeless("ctest:sl_extra", {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_sl_extra_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[1] = stick();  // extra item
    // 2 items but recipe needs 1

    EXPECT_FALSE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:sl_extra"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, ShapelessTaggedMatch) {
    TagRegistry::global().addMember(TagId::fromName("ctest:metals"),
                                     ItemTypeId::fromName("ctest_iron"));
    TagRegistry::global().addMember(TagId::fromName("ctest:metals"),
                                     ItemTypeId::fromName("ctest_copper"));
    TagRegistry::global().rebuild();

    registerShapeless("ctest:sl_tagged", {
        ItemMatch::tagged(TagId::fromName("ctest:metals")),
        ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_sl_tagged_out"));

    auto grid = makeGrid();
    grid[0] = copper();
    grid[4] = stick();

    EXPECT_TRUE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:sl_tagged"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, ShapelessDuplicateItems) {
    registerShapeless("ctest:sl_dup", {
        ItemMatch::exact(iron()),
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_sl_dup_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[1] = iron();

    EXPECT_TRUE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:sl_dup"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, ShapelessBacktrackingRequired) {
    // Tag matches both iron and copper.
    // Recipe: [tagged(metals), exact(iron)]
    // Items:  [iron, copper]
    // Greedy tag-first: tag→iron, exact(iron)→copper → FAIL
    // Backtracking: tag→copper, exact(iron)→iron → SUCCESS
    TagRegistry::global().addMember(TagId::fromName("ctest:bt_metals"),
                                     ItemTypeId::fromName("ctest_iron"));
    TagRegistry::global().addMember(TagId::fromName("ctest:bt_metals"),
                                     ItemTypeId::fromName("ctest_copper"));
    TagRegistry::global().rebuild();

    registerShapeless("ctest:sl_bt", {
        ItemMatch::tagged(TagId::fromName("ctest:bt_metals")),
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_sl_bt_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[1] = copper();

    EXPECT_TRUE(CraftingHelper::matchShapeless(
        *RecipeRegistry::global().getRecipe("ctest:sl_bt"),
        grid.data(), 3, 3));
}

// ============================================================================
// findRecipe Tests
// ============================================================================

TEST_F(CraftingHelperTest, FindsShapedRecipe) {
    registerShaped("ctest:find_shaped", 1, 2, {
        ItemMatch::exact(iron()),
        ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_find_shaped_out"));

    auto grid = makeGrid();
    grid[0] = iron();
    grid[3] = stick();

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_TRUE(result.recipe->isShaped());
}

TEST_F(CraftingHelperTest, FindsShapelessRecipe) {
    registerShapeless("ctest:find_sl", {
        ItemMatch::exact(planks()),
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_find_sl_out"));

    auto grid = makeGrid();
    grid[4] = iron();
    grid[7] = planks();

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_TRUE(result.recipe->isShapeless());
}

TEST_F(CraftingHelperTest, ReturnsNullForNoMatch) {
    registerShaped("ctest:nomatch", 2, 2, {
        ItemMatch::exact(iron()), ItemMatch::exact(iron()),
        ItemMatch::exact(iron()), ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_nomatch_out"));

    auto grid = makeGrid();
    grid[0] = stick();  // only one stick

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    EXPECT_EQ(result.recipe, nullptr);
}

TEST_F(CraftingHelperTest, FiltersByStation) {
    auto station = StationTypeId::fromName("ctest_anvil");
    registerShaped("ctest:station_filter", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_station_out"), 1, station);

    auto grid = makeGrid();
    grid[0] = iron();

    // Wrong station
    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    EXPECT_EQ(result.recipe, nullptr);

    // Correct station
    auto result2 = CraftingHelper::findRecipe(grid.data(), 3, 3, station);
    ASSERT_NE(result2.recipe, nullptr);
}

TEST_F(CraftingHelperTest, SkipsSmeltingRecipes) {
    auto furnace = StationTypeId::fromName("ctest_furnace");
    registerSmelting("ctest:smelt_skip", ItemMatch::exact(iron()),
                     ItemTypeId::fromName("ctest_smelt_out"), furnace);

    auto grid = makeGrid();
    grid[0] = iron();

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, furnace);
    EXPECT_EQ(result.recipe, nullptr);  // smelting skipped
}

// ============================================================================
// canCraft Tests
// ============================================================================

TEST_F(CraftingHelperTest, CanCraftTrue) {
    registerShaped("ctest:cancraft_t", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_cancraft_out"));

    auto grid = makeGrid();
    grid[4] = iron();

    EXPECT_TRUE(CraftingHelper::canCraft(
        *RecipeRegistry::global().getRecipe("ctest:cancraft_t"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, CanCraftFalse) {
    registerShaped("ctest:cancraft_f", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_cancraft_f_out"));

    auto grid = makeGrid();
    grid[4] = stick();  // wrong item

    EXPECT_FALSE(CraftingHelper::canCraft(
        *RecipeRegistry::global().getRecipe("ctest:cancraft_f"),
        grid.data(), 3, 3));
}

TEST_F(CraftingHelperTest, CanCraftSmeltingReturnsFalse) {
    auto furnace = StationTypeId::fromName("ctest_furnace");
    registerSmelting("ctest:cancraft_smelt", ItemMatch::exact(iron()),
                     ItemTypeId::fromName("ctest_cancraft_smelt_out"), furnace);

    auto grid = makeGrid();
    grid[0] = iron();

    EXPECT_FALSE(CraftingHelper::canCraft(
        *RecipeRegistry::global().getRecipe("ctest:cancraft_smelt"),
        grid.data(), 3, 3));
}

// ============================================================================
// executeCraft Tests
// ============================================================================

class CraftingExecuteTest : public CraftingHelperTest {
protected:
    DataContainer dc_;
    NameRegistry registry_;

    void setupGrid(InventoryView& inv, int32_t slots) {
        inv.setSlotCount(slots);
    }

    void setGridSlot(InventoryView& inv, int32_t index, ItemTypeId type, int32_t count) {
        ItemStack stack;
        stack.type = type;
        stack.count = count;
        inv.setSlot(index, stack);
    }
};

TEST_F(CraftingExecuteTest, ExecuteShapedDecrementsSlots) {
    registerShaped("ctest:exec_shaped", 2, 1, {
        ItemMatch::exact(iron()),
        ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_exec_out"));

    // Set up inventory grid (3x3)
    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);
    setGridSlot(inv, 0, iron(), 3);
    setGridSlot(inv, 1, stick(), 2);

    // Build match
    ItemTypeId grid[9] = {};
    grid[0] = iron();
    grid[1] = stick();
    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:exec_shaped"),
        grid, 3, 3);
    ASSERT_NE(result.recipe, nullptr);

    EXPECT_TRUE(CraftingHelper::executeCraft(result, inv, 3, 3));

    auto slot0 = inv.getSlot(0);
    auto slot1 = inv.getSlot(1);
    EXPECT_EQ(slot0.count, 2);  // 3 - 1
    EXPECT_EQ(slot1.count, 1);  // 2 - 1
}

TEST_F(CraftingExecuteTest, ExecuteShapedClearsOnZero) {
    registerShaped("ctest:exec_clear", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_exec_clear_out"));

    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);
    setGridSlot(inv, 4, iron(), 1);  // count=1, will become 0

    ItemTypeId grid[9] = {};
    grid[4] = iron();
    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:exec_clear"),
        grid, 3, 3);
    ASSERT_NE(result.recipe, nullptr);

    EXPECT_TRUE(CraftingHelper::executeCraft(result, inv, 3, 3));

    auto slot4 = inv.getSlot(4);
    EXPECT_TRUE(slot4.isEmpty());
}

TEST_F(CraftingExecuteTest, ExecuteShapedWithOffset) {
    registerShaped("ctest:exec_offset", 2, 2, {
        ItemMatch::exact(iron()), ItemMatch::exact(iron()),
        ItemMatch::exact(stick()), ItemMatch::exact(stick()),
    }, ItemTypeId::fromName("ctest_exec_offset_out"));

    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);
    // Place at offset (1,1) in 3x3 grid
    setGridSlot(inv, 4, iron(), 5);   // (1,1)
    setGridSlot(inv, 5, iron(), 5);   // (2,1)
    setGridSlot(inv, 7, stick(), 5);  // (1,2)
    setGridSlot(inv, 8, stick(), 5);  // (2,2)

    ItemTypeId grid[9] = {};
    grid[4] = iron();  grid[5] = iron();
    grid[7] = stick(); grid[8] = stick();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:exec_offset"),
        grid, 3, 3);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 1);
    EXPECT_EQ(result.offsetY, 1);

    EXPECT_TRUE(CraftingHelper::executeCraft(result, inv, 3, 3));

    EXPECT_EQ(inv.getSlot(4).count, 4);
    EXPECT_EQ(inv.getSlot(5).count, 4);
    EXPECT_EQ(inv.getSlot(7).count, 4);
    EXPECT_EQ(inv.getSlot(8).count, 4);
}

TEST_F(CraftingExecuteTest, ExecuteShapelessDecrementsSlots) {
    registerShapeless("ctest:exec_sl", {
        ItemMatch::exact(iron()),
        ItemMatch::exact(planks()),
    }, ItemTypeId::fromName("ctest_exec_sl_out"));

    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);
    setGridSlot(inv, 2, iron(), 3);
    setGridSlot(inv, 6, planks(), 2);

    ItemTypeId grid[9] = {};
    grid[2] = iron();
    grid[6] = planks();

    // Verify shapeless match
    const Recipe* recipe = RecipeRegistry::global().getRecipe("ctest:exec_sl");
    ASSERT_TRUE(CraftingHelper::matchShapeless(*recipe, grid, 3, 3));

    CraftingHelper::MatchResult result;
    result.recipe = recipe;
    EXPECT_TRUE(CraftingHelper::executeCraft(result, inv, 3, 3));

    EXPECT_EQ(inv.getSlot(2).count, 2);  // 3 - 1
    EXPECT_EQ(inv.getSlot(6).count, 1);  // 2 - 1
}

TEST_F(CraftingExecuteTest, ExecutePreservesRemaining) {
    registerShaped("ctest:exec_preserve", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_exec_preserve_out"));

    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);
    setGridSlot(inv, 0, iron(), 10);

    ItemTypeId grid[9] = {};
    grid[0] = iron();

    auto result = CraftingHelper::matchShaped(
        *RecipeRegistry::global().getRecipe("ctest:exec_preserve"),
        grid, 3, 3);
    ASSERT_NE(result.recipe, nullptr);

    EXPECT_TRUE(CraftingHelper::executeCraft(result, inv, 3, 3));

    auto slot0 = inv.getSlot(0);
    EXPECT_EQ(slot0.type, iron());
    EXPECT_EQ(slot0.count, 9);  // 10 - 1
}

TEST_F(CraftingExecuteTest, ExecuteNullRecipeFails) {
    CraftingHelper::MatchResult result;  // recipe = nullptr
    InventoryView inv(dc_, registry_);
    setupGrid(inv, 9);

    EXPECT_FALSE(CraftingHelper::executeCraft(result, inv, 3, 3));
}

// ============================================================================
// Edge Case Tests
// ============================================================================

TEST_F(CraftingHelperTest, EmptyGridNoMatch) {
    registerShaped("ctest:edge_empty", 1, 1, {
        ItemMatch::exact(iron()),
    }, ItemTypeId::fromName("ctest_edge_empty_out"));

    auto grid = makeGrid();

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    EXPECT_EQ(result.recipe, nullptr);
}

TEST_F(CraftingHelperTest, SingleItemInGrid) {
    registerShaped("ctest:edge_single", 1, 1, {
        ItemMatch::exact(log()),
    }, planks(), 4);

    auto grid = makeGrid();
    grid[8] = log();  // bottom-right corner

    auto result = CraftingHelper::findRecipe(grid.data(), 3, 3, EMPTY_STATION);
    ASSERT_NE(result.recipe, nullptr);
    EXPECT_EQ(result.offsetX, 2);
    EXPECT_EQ(result.offsetY, 2);
}
