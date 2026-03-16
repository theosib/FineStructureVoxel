#include <gtest/gtest.h>

#include "finevox/script/inventory_bridge.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/item_stack.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/recipe.hpp"
#include "finevox/core/recipe_registry.hpp"

#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <finescript/value.h>
#include <finescript/map_data.h>

using namespace finevox;
using namespace finevox::script;
using finescript::ScriptEngine;
using finescript::ExecutionContext;
using finescript::Value;

class InventoryNativesTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<ScriptEngine>();

        // Set up player DataContainer with a "main" inventory section
        playerDC = std::make_unique<DataContainer>();
        auto& mainSection = playerDC->getOrCreateChild("main");
        InventoryView mainInv(mainSection, registry);
        mainInv.setSlotCount(9);

        // Pre-populate some items
        ItemStack stone;
        stone.type = ItemTypeId::fromName("stone");
        stone.count = 32;
        mainInv.setSlot(0, stone);

        ItemStack dirt;
        dirt.type = ItemTypeId::fromName("dirt");
        dirt.count = 16;
        mainInv.setSlot(1, dirt);

        bridge = std::make_unique<InventoryBridge>();
        bridge->registerOwner("player", *playerDC, registry);
        bridge->registerNativeFunctions(*engine);
    }

    // Helper: execute a finescript command and return the result value
    Value eval(const std::string& cmd) {
        ExecutionContext ctx(*engine);
        auto result = engine->executeCommand(cmd, ctx);
        return std::move(result.returnValue);
    }

    std::unique_ptr<ScriptEngine> engine;
    std::unique_ptr<DataContainer> playerDC;
    std::unique_ptr<InventoryBridge> bridge;
    NameRegistry registry;
};

// ============================================================================
// inv_get
// ============================================================================

TEST_F(InventoryNativesTest, InvGetReturnsItemMap) {
    auto result = eval("inv_get \"player\" \"main\" 0");
    ASSERT_TRUE(result.isMap());

    auto& si = StringInterner::global();
    auto typeVal = result.asMap().get(si.intern("type"));
    auto countVal = result.asMap().get(si.intern("count"));

    EXPECT_TRUE(typeVal.isString());
    EXPECT_EQ(typeVal.asString(), "stone");
    EXPECT_TRUE(countVal.isInt());
    EXPECT_EQ(countVal.asInt(), 32);
}

TEST_F(InventoryNativesTest, InvGetEmptySlotReturnsNil) {
    auto result = eval("inv_get \"player\" \"main\" 5");
    EXPECT_TRUE(result.isNil());
}

TEST_F(InventoryNativesTest, InvGetInvalidOwnerReturnsNil) {
    auto result = eval("inv_get \"nonexistent\" \"main\" 0");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// inv_size
// ============================================================================

TEST_F(InventoryNativesTest, InvSizeReturnsSlotCount) {
    auto result = eval("inv_size \"player\" \"main\"");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 9);
}

TEST_F(InventoryNativesTest, InvSizeMissingSectionReturnsZero) {
    auto result = eval("inv_size \"player\" \"equipment\"");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 0);
}

// ============================================================================
// inv_count
// ============================================================================

TEST_F(InventoryNativesTest, InvCountItemType) {
    auto result = eval("inv_count \"player\" \"main\" \"stone\"");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 32);
}

TEST_F(InventoryNativesTest, InvCountMissingTypeReturnsZero) {
    auto result = eval("inv_count \"player\" \"main\" \"gold\"");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 0);
}

// ============================================================================
// inv_type
// ============================================================================

TEST_F(InventoryNativesTest, InvTypeReturnsName) {
    auto result = eval("inv_type \"player\" \"main\" 0");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.asString(), "stone");
}

TEST_F(InventoryNativesTest, InvTypeEmptyReturnsNil) {
    auto result = eval("inv_type \"player\" \"main\" 5");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// inv_add
// ============================================================================

TEST_F(InventoryNativesTest, InvAddItems) {
    auto result = eval("inv_add \"player\" \"main\" \"cobble\" 10");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 0);  // All items placed

    // Verify cobble was added to slot 2 (first empty)
    auto check = eval("inv_type \"player\" \"main\" 2");
    ASSERT_TRUE(check.isString());
    EXPECT_EQ(check.asString(), "cobble");
}

// ============================================================================
// inv_clear
// ============================================================================

TEST_F(InventoryNativesTest, InvClearSlot) {
    eval("inv_clear \"player\" \"main\" 0");
    auto result = eval("inv_get \"player\" \"main\" 0");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// inv_set
// ============================================================================

TEST_F(InventoryNativesTest, InvSetSlot) {
    // Build the item map directly via C++ and call the native function
    auto& si = StringInterner::global();
    auto itemMap = Value::map();
    itemMap.asMap().set(si.intern("type"), Value::string("gold"));
    itemMap.asMap().set(si.intern("count"), Value::integer(8));

    // Call inv_set directly
    ExecutionContext ctx(*engine);
    std::vector<Value> args;
    args.push_back(Value::string("player"));
    args.push_back(Value::string("main"));
    args.push_back(Value::integer(5));
    args.push_back(std::move(itemMap));

    // Look up the function and call it
    auto invSet = ctx.get("inv_set");
    ASSERT_TRUE(invSet.isCallable()) << "inv_set should be callable";
    engine->callFunction(invSet, std::move(args), ctx);

    auto result = eval("inv_get \"player\" \"main\" 5");
    ASSERT_TRUE(result.isMap()) << "Slot 5 should contain the gold item";
    EXPECT_EQ(result.asMap().get(si.intern("type")).asString(), "gold");
    EXPECT_EQ(result.asMap().get(si.intern("count")).asInt(), 8);
}

TEST_F(InventoryNativesTest, InvSetNilClearsSlot) {
    eval("inv_set \"player\" \"main\" 0 nil");
    auto result = eval("inv_get \"player\" \"main\" 0");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// inv_take
// ============================================================================

TEST_F(InventoryNativesTest, InvTakePartial) {
    auto result = eval("inv_take \"player\" \"main\" 0 10");
    ASSERT_TRUE(result.isMap());

    auto& si = StringInterner::global();
    EXPECT_EQ(result.asMap().get(si.intern("type")).asString(), "stone");
    EXPECT_EQ(result.asMap().get(si.intern("count")).asInt(), 10);

    // Original slot should have 22 remaining
    auto remaining = eval("inv_get \"player\" \"main\" 0");
    ASSERT_TRUE(remaining.isMap());
    EXPECT_EQ(remaining.asMap().get(si.intern("count")).asInt(), 22);
}

// ============================================================================
// inv_swap
// ============================================================================

TEST_F(InventoryNativesTest, InvSwapSlots) {
    auto result = eval("inv_swap \"player\" \"main\" 0 \"player\" \"main\" 1");
    ASSERT_TRUE(result.isBool());
    EXPECT_TRUE(result.asBool());

    // Slot 0 should now have dirt, slot 1 should have stone
    auto slot0 = eval("inv_type \"player\" \"main\" 0");
    auto slot1 = eval("inv_type \"player\" \"main\" 1");
    EXPECT_EQ(slot0.asString(), "dirt");
    EXPECT_EQ(slot1.asString(), "stone");
}

// ============================================================================
// inv_init
// ============================================================================

TEST_F(InventoryNativesTest, InvInitSection) {
    eval("inv_init \"player\" \"hotbar\" 8");
    auto result = eval("inv_size \"player\" \"hotbar\"");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 8);
}

// ============================================================================
// inv_move
// ============================================================================

TEST_F(InventoryNativesTest, InvMoveToEmptySlot) {
    auto result = eval("inv_move \"player\" \"main\" 0 \"player\" \"main\" 5 10");
    ASSERT_TRUE(result.isInt());
    EXPECT_EQ(result.asInt(), 0);  // All moved

    // Check source decreased
    auto& si = StringInterner::global();
    auto src = eval("inv_get \"player\" \"main\" 0");
    ASSERT_TRUE(src.isMap());
    EXPECT_EQ(src.asMap().get(si.intern("count")).asInt(), 22);

    // Check destination has items
    auto dst = eval("inv_get \"player\" \"main\" 5");
    ASSERT_TRUE(dst.isMap());
    EXPECT_EQ(dst.asMap().get(si.intern("type")).asString(), "stone");
    EXPECT_EQ(dst.asMap().get(si.intern("count")).asInt(), 10);
}

// ============================================================================
// L (Label lookup)
// ============================================================================

TEST_F(InventoryNativesTest, LabelLookup) {
    LabelRegistry::global().clear();
    LabelRegistry::global().loadFromString("inventory.title: Inventory");

    auto result = eval("L \"inventory.title\"");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.asString(), "Inventory");
}

TEST_F(InventoryNativesTest, LabelLookupMissing) {
    LabelRegistry::global().clear();
    auto result = eval("L \"missing.key\"");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.asString(), "missing.key");
}

TEST_F(InventoryNativesTest, LabelFormat) {
    LabelRegistry::global().clear();
    LabelRegistry::global().loadFromString("hotbar.slot: Slot {0}");

    auto result = eval("L \"hotbar.slot\" 3");
    ASSERT_TRUE(result.isString());
    EXPECT_EQ(result.asString(), "Slot 3");
}

// ============================================================================
// item_icon
// ============================================================================

TEST_F(InventoryNativesTest, ItemIconReturnsMapWhenCallbackSet) {
    bridge->setIconLookup([](std::string_view name) -> std::optional<InventoryBridge::IconInfo> {
        if (name == "stone") {
            return InventoryBridge::IconInfo{"block_atlas", 0.0f, 0.0f, 0.0625f, 0.0625f};
        }
        return std::nullopt;
    });

    auto result = eval("item_icon \"stone\"");
    ASSERT_TRUE(result.isMap());

    auto& si = StringInterner::global();
    auto tex = result.asMap().get(si.intern("texture"));
    ASSERT_TRUE(tex.isString());
    EXPECT_EQ(tex.asString(), "block_atlas");

    auto uv0 = result.asMap().get(si.intern("uv0"));
    ASSERT_TRUE(uv0.isArray());
    EXPECT_EQ(uv0.asArray().size(), 2u);
    EXPECT_NEAR(uv0.asArray()[0].asNumber(), 0.0, 0.001);
    EXPECT_NEAR(uv0.asArray()[1].asNumber(), 0.0, 0.001);

    auto uv1 = result.asMap().get(si.intern("uv1"));
    ASSERT_TRUE(uv1.isArray());
    EXPECT_NEAR(uv1.asArray()[0].asNumber(), 0.0625, 0.001);
    EXPECT_NEAR(uv1.asArray()[1].asNumber(), 0.0625, 0.001);
}

TEST_F(InventoryNativesTest, ItemIconReturnsNilForUnknownType) {
    bridge->setIconLookup([](std::string_view) { return std::nullopt; });
    auto result = eval("item_icon \"unknown\"");
    EXPECT_TRUE(result.isNil());
}

TEST_F(InventoryNativesTest, ItemIconReturnsNilWithNoCallback) {
    auto result = eval("item_icon \"stone\"");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// Crafting natives test fixture (needs recipe registry setup)
// ============================================================================

class CraftingNativesTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<ScriptEngine>();

        // Set up player DC with a crafting grid (2x2 = 4 slots)
        playerDC = std::make_unique<DataContainer>();
        auto& mainSection = playerDC->getOrCreateChild("main");
        InventoryView mainInv(mainSection, registry);
        mainInv.setSlotCount(9);

        auto& gridSection = playerDC->getOrCreateChild("craft_grid");
        InventoryView gridInv(gridSection, registry);
        gridInv.setSlotCount(4);

        bridge = std::make_unique<script::InventoryBridge>();
        bridge->registerOwner("player", *playerDC, registry);
        bridge->registerNativeFunctions(*engine);

        // Register a simple shaped recipe: 2x planks → sticks
        RecipeRegistry::global().clear();
        Recipe stickRecipe;
        stickRecipe.id = RecipeId::fromName("test:sticks");
        stickRecipe.type = RecipeType::Shaped;
        stickRecipe.station = EMPTY_STATION;
        stickRecipe.category = "materials";
        stickRecipe.width = 1;
        stickRecipe.height = 2;
        stickRecipe.pattern = {
            ItemMatch::exact(ItemTypeId::fromName("planks")),
            ItemMatch::exact(ItemTypeId::fromName("planks"))
        };
        stickRecipe.outputItem = ItemTypeId::fromName("sticks");
        stickRecipe.outputCount = 4;
        RecipeRegistry::global().registerRecipe("test:sticks", std::move(stickRecipe));
    }

    void TearDown() override {
        RecipeRegistry::global().clear();
    }

    Value eval(const std::string& cmd) {
        ExecutionContext ctx(*engine);
        auto result = engine->executeCommand(cmd, ctx);
        return std::move(result.returnValue);
    }

    std::unique_ptr<ScriptEngine> engine;
    std::unique_ptr<DataContainer> playerDC;
    std::unique_ptr<script::InventoryBridge> bridge;
    NameRegistry registry;
};

// ============================================================================
// craft_find
// ============================================================================

TEST_F(CraftingNativesTest, CraftFindMatchesRecipe) {
    // Place planks in grid slots 0 and 1 (column of 2)
    auto& gridSection = playerDC->getOrCreateChild("craft_grid");
    InventoryView grid(gridSection, registry);
    ItemStack planks;
    planks.type = ItemTypeId::fromName("planks");
    planks.count = 1;
    grid.setSlot(0, planks);
    grid.setSlot(2, planks);  // Row-major 2x2: (0,0) and (0,1)

    auto result = eval("craft_find \"player\" \"craft_grid\" 2 2");
    ASSERT_TRUE(result.isMap());

    auto& si = StringInterner::global();
    EXPECT_EQ(result.asMap().get(si.intern("recipe")).asString(), "test:sticks");
    EXPECT_EQ(result.asMap().get(si.intern("output")).asString(), "sticks");
    EXPECT_EQ(result.asMap().get(si.intern("count")).asInt(), 4);
}

TEST_F(CraftingNativesTest, CraftFindNoMatchReturnsNil) {
    // Empty grid
    auto result = eval("craft_find \"player\" \"craft_grid\" 2 2");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// craft_execute
// ============================================================================

TEST_F(CraftingNativesTest, CraftExecuteConsumesIngredients) {
    auto& gridSection = playerDC->getOrCreateChild("craft_grid");
    InventoryView grid(gridSection, registry);
    ItemStack planks;
    planks.type = ItemTypeId::fromName("planks");
    planks.count = 3;
    grid.setSlot(0, planks);
    planks.count = 2;
    grid.setSlot(2, planks);  // slot (0,1) in row-major 2x2

    auto result = eval("craft_execute \"player\" \"craft_grid\" 2 2");
    ASSERT_TRUE(result.isMap());

    auto& si = StringInterner::global();
    EXPECT_EQ(result.asMap().get(si.intern("type")).asString(), "sticks");
    EXPECT_EQ(result.asMap().get(si.intern("count")).asInt(), 4);

    // Check that ingredients were consumed (1 each)
    auto slot0 = grid.getSlot(0);
    EXPECT_EQ(slot0.count, 2);  // 3 - 1
    auto slot2 = grid.getSlot(2);
    EXPECT_EQ(slot2.count, 1);  // 2 - 1
}

TEST_F(CraftingNativesTest, CraftExecuteNoMatchReturnsNil) {
    auto result = eval("craft_execute \"player\" \"craft_grid\" 2 2");
    EXPECT_TRUE(result.isNil());
}

// ============================================================================
// craft_recipes
// ============================================================================

TEST_F(CraftingNativesTest, CraftRecipesReturnsHandCraftingRecipes) {
    auto result = eval("craft_recipes");
    ASSERT_TRUE(result.isArray());
    EXPECT_GE(result.asArray().size(), 1u);

    auto& si = StringInterner::global();
    auto& first = result.asArray()[0];
    ASSERT_TRUE(first.isMap());
    EXPECT_EQ(first.asMap().get(si.intern("recipe")).asString(), "test:sticks");
}

TEST_F(CraftingNativesTest, CraftRecipesFiltersByStation) {
    // No recipes for "furnace" station
    auto result = eval("craft_recipes \"furnace\"");
    ASSERT_TRUE(result.isArray());
    EXPECT_EQ(result.asArray().size(), 0u);
}
