#include <gtest/gtest.h>
#include "finevox/core/recipe.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/recipe_loader.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"

#include <unordered_set>
#include <thread>

using namespace finevox;

// ============================================================================
// RecipeId Tests
// ============================================================================

class RecipeIdTest : public ::testing::Test {};

TEST_F(RecipeIdTest, DefaultIsEmpty) {
    RecipeId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
    EXPECT_EQ(id.id, 0u);
}

TEST_F(RecipeIdTest, EmptyConstant) {
    EXPECT_TRUE(EMPTY_RECIPE.isEmpty());
}

TEST_F(RecipeIdTest, FromName) {
    auto id = RecipeId::fromName("recipe_test_a");
    EXPECT_FALSE(id.isEmpty());
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.name(), "recipe_test_a");
}

TEST_F(RecipeIdTest, SameNameSameId) {
    auto a = RecipeId::fromName("recipe_test_same");
    auto b = RecipeId::fromName("recipe_test_same");
    EXPECT_EQ(a, b);
}

TEST_F(RecipeIdTest, DifferentNameDifferentId) {
    auto a = RecipeId::fromName("recipe_test_x");
    auto b = RecipeId::fromName("recipe_test_y");
    EXPECT_NE(a, b);
}

TEST_F(RecipeIdTest, Ordering) {
    auto a = RecipeId::fromName("recipe_ord_a");
    auto b = RecipeId::fromName("recipe_ord_b");
    // Both should be valid and comparable (one must be less than the other)
    EXPECT_TRUE((a < b) || (b < a));
}

TEST_F(RecipeIdTest, Hashable) {
    std::unordered_set<RecipeId> set;
    set.insert(RecipeId::fromName("recipe_hash_a"));
    set.insert(RecipeId::fromName("recipe_hash_b"));
    set.insert(RecipeId::fromName("recipe_hash_a"));
    EXPECT_EQ(set.size(), 2u);
}

TEST_F(RecipeIdTest, NameRoundTrip) {
    auto id = RecipeId::fromName("recipe_roundtrip");
    EXPECT_EQ(id.name(), "recipe_roundtrip");
}

// ============================================================================
// StationTypeId Tests
// ============================================================================

class StationTypeIdTest : public ::testing::Test {};

TEST_F(StationTypeIdTest, DefaultIsEmpty) {
    StationTypeId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
}

TEST_F(StationTypeIdTest, EmptyStationConstant) {
    EXPECT_TRUE(EMPTY_STATION.isEmpty());
}

TEST_F(StationTypeIdTest, FromName) {
    auto id = StationTypeId::fromName("station_test_furnace");
    EXPECT_FALSE(id.isEmpty());
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.name(), "station_test_furnace");
}

TEST_F(StationTypeIdTest, Hashable) {
    std::unordered_set<StationTypeId> set;
    set.insert(StationTypeId::fromName("station_hash_a"));
    set.insert(StationTypeId::fromName("station_hash_b"));
    set.insert(StationTypeId::fromName("station_hash_a"));
    EXPECT_EQ(set.size(), 2u);
}

// ============================================================================
// Recipe Struct Tests
// ============================================================================

class RecipeStructTest : public ::testing::Test {};

TEST_F(RecipeStructTest, DefaultValues) {
    Recipe r;
    EXPECT_TRUE(r.id.isEmpty());
    EXPECT_EQ(r.type, RecipeType::Shaped);
    EXPECT_TRUE(r.station.isEmpty());
    EXPECT_TRUE(r.category.empty());
    EXPECT_EQ(r.width, 0);
    EXPECT_EQ(r.height, 0);
    EXPECT_TRUE(r.pattern.empty());
    EXPECT_FALSE(r.allowMirror);
    EXPECT_TRUE(r.ingredients.empty());
    EXPECT_EQ(r.smeltTicks, 200);
    EXPECT_EQ(r.experience, 0.0f);
    EXPECT_TRUE(r.outputItem.isEmpty());
    EXPECT_EQ(r.outputCount, 1);
}

TEST_F(RecipeStructTest, TypePredicates) {
    Recipe shaped;
    shaped.type = RecipeType::Shaped;
    EXPECT_TRUE(shaped.isShaped());
    EXPECT_FALSE(shaped.isShapeless());
    EXPECT_FALSE(shaped.isSmelting());

    Recipe shapeless;
    shapeless.type = RecipeType::Shapeless;
    EXPECT_FALSE(shapeless.isShaped());
    EXPECT_TRUE(shapeless.isShapeless());
    EXPECT_FALSE(shapeless.isSmelting());

    Recipe smelting;
    smelting.type = RecipeType::Smelting;
    EXPECT_FALSE(smelting.isShaped());
    EXPECT_FALSE(smelting.isShapeless());
    EXPECT_TRUE(smelting.isSmelting());
}

TEST_F(RecipeStructTest, ShapedRecipeFields) {
    Recipe r;
    r.type = RecipeType::Shaped;
    r.width = 3;
    r.height = 3;
    r.pattern.resize(9, ItemMatch::empty());
    r.pattern[1] = ItemMatch::exact(ItemTypeId::fromName("rtest_stick"));
    EXPECT_EQ(r.pattern.size(), 9u);
    EXPECT_TRUE(r.pattern[0].isEmpty());
    EXPECT_TRUE(r.pattern[1].isExact());
}

TEST_F(RecipeStructTest, ShapelessRecipeFields) {
    Recipe r;
    r.type = RecipeType::Shapeless;
    r.ingredients.push_back(ItemMatch::exact(ItemTypeId::fromName("rtest_log")));
    r.outputItem = ItemTypeId::fromName("rtest_planks");
    r.outputCount = 4;
    EXPECT_EQ(r.ingredients.size(), 1u);
    EXPECT_EQ(r.outputCount, 4);
}

TEST_F(RecipeStructTest, SmeltingRecipeFields) {
    Recipe r;
    r.type = RecipeType::Smelting;
    r.smeltInput = ItemMatch::exact(ItemTypeId::fromName("rtest_raw_iron"));
    r.smeltTicks = 300;
    r.experience = 0.7f;
    r.outputItem = ItemTypeId::fromName("rtest_iron_ingot");
    EXPECT_EQ(r.smeltTicks, 300);
    EXPECT_FLOAT_EQ(r.experience, 0.7f);
}

TEST_F(RecipeStructTest, StationAssignment) {
    Recipe r;
    r.station = StationTypeId::fromName("rtest_furnace");
    EXPECT_FALSE(r.station.isEmpty());
    EXPECT_EQ(r.station.name(), "rtest_furnace");
}

TEST_F(RecipeStructTest, CategoryAssignment) {
    Recipe r;
    r.category = "tools";
    EXPECT_EQ(r.category, "tools");
}

TEST_F(RecipeStructTest, MirrorFlag) {
    Recipe r;
    r.allowMirror = true;
    EXPECT_TRUE(r.allowMirror);
}

// ============================================================================
// RecipeRegistry Tests
// ============================================================================

class RecipeRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }

    void TearDown() override {
        RecipeRegistry::global().clear();
    }

    Recipe makeShapedRecipe(std::string_view name) {
        Recipe r;
        r.type = RecipeType::Shaped;
        r.width = 2;
        r.height = 2;
        r.pattern.resize(4, ItemMatch::exact(ItemTypeId::fromName("rtest_planks")));
        r.outputItem = ItemTypeId::fromName(std::string("rtest_out_") + std::string(name));
        r.outputCount = 1;
        return r;
    }

    Recipe makeShapelessRecipe(std::string_view name) {
        Recipe r;
        r.type = RecipeType::Shapeless;
        r.ingredients.push_back(ItemMatch::exact(ItemTypeId::fromName("rtest_log")));
        r.outputItem = ItemTypeId::fromName(std::string("rtest_out_") + std::string(name));
        r.outputCount = 4;
        return r;
    }

    Recipe makeSmeltingRecipe(std::string_view name, std::string_view station = "rtest_furnace") {
        Recipe r;
        r.type = RecipeType::Smelting;
        r.station = StationTypeId::fromName(station);
        r.smeltInput = ItemMatch::exact(ItemTypeId::fromName(std::string("rtest_in_") + std::string(name)));
        r.smeltTicks = 200;
        r.experience = 0.5f;
        r.outputItem = ItemTypeId::fromName(std::string("rtest_out_") + std::string(name));
        r.outputCount = 1;
        return r;
    }
};

TEST_F(RecipeRegistryTest, GlobalSingleton) {
    auto& a = RecipeRegistry::global();
    auto& b = RecipeRegistry::global();
    EXPECT_EQ(&a, &b);
}

TEST_F(RecipeRegistryTest, RegisterAndRetrieveById) {
    auto recipe = makeShapedRecipe("workbench");
    EXPECT_TRUE(RecipeRegistry::global().registerRecipe("rtest:workbench", recipe));

    auto id = RecipeId::fromName("rtest:workbench");
    const Recipe* found = RecipeRegistry::global().getRecipe(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(found->type, RecipeType::Shaped);
}

TEST_F(RecipeRegistryTest, RegisterAndRetrieveByName) {
    auto recipe = makeShapedRecipe("table");
    EXPECT_TRUE(RecipeRegistry::global().registerRecipe("rtest:table", recipe));

    const Recipe* found = RecipeRegistry::global().getRecipe("rtest:table");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id.name(), "rtest:table");
}

TEST_F(RecipeRegistryTest, DuplicateRegistrationFails) {
    auto recipe = makeShapedRecipe("dup");
    EXPECT_TRUE(RecipeRegistry::global().registerRecipe("rtest:dup", recipe));
    EXPECT_FALSE(RecipeRegistry::global().registerRecipe("rtest:dup", recipe));
}

TEST_F(RecipeRegistryTest, HasRecipe) {
    auto recipe = makeShapedRecipe("has");
    RecipeRegistry::global().registerRecipe("rtest:has", recipe);

    EXPECT_TRUE(RecipeRegistry::global().hasRecipe(RecipeId::fromName("rtest:has")));
    EXPECT_FALSE(RecipeRegistry::global().hasRecipe(RecipeId::fromName("rtest:nope")));
}

TEST_F(RecipeRegistryTest, HasRecipeByName) {
    auto recipe = makeShapedRecipe("hasname");
    RecipeRegistry::global().registerRecipe("rtest:hasname", recipe);

    EXPECT_TRUE(RecipeRegistry::global().hasRecipe("rtest:hasname"));
    EXPECT_FALSE(RecipeRegistry::global().hasRecipe("rtest:nope2"));
}

TEST_F(RecipeRegistryTest, GetNonexistentReturnsNull) {
    EXPECT_EQ(RecipeRegistry::global().getRecipe("rtest:nosuch"), nullptr);
}

TEST_F(RecipeRegistryTest, Size) {
    EXPECT_EQ(RecipeRegistry::global().size(), 0u);
    RecipeRegistry::global().registerRecipe("rtest:s1", makeShapedRecipe("s1"));
    EXPECT_EQ(RecipeRegistry::global().size(), 1u);
    RecipeRegistry::global().registerRecipe("rtest:s2", makeShapedRecipe("s2"));
    EXPECT_EQ(RecipeRegistry::global().size(), 2u);
}

TEST_F(RecipeRegistryTest, Clear) {
    RecipeRegistry::global().registerRecipe("rtest:c1", makeShapedRecipe("c1"));
    RecipeRegistry::global().registerRecipe("rtest:c2", makeShapedRecipe("c2"));
    EXPECT_EQ(RecipeRegistry::global().size(), 2u);

    RecipeRegistry::global().clear();
    EXPECT_EQ(RecipeRegistry::global().size(), 0u);
    EXPECT_EQ(RecipeRegistry::global().getRecipe("rtest:c1"), nullptr);
}

TEST_F(RecipeRegistryTest, AllRecipes) {
    RecipeRegistry::global().registerRecipe("rtest:all1", makeShapedRecipe("all1"));
    RecipeRegistry::global().registerRecipe("rtest:all2", makeShapelessRecipe("all2"));

    auto all = RecipeRegistry::global().allRecipes();
    EXPECT_EQ(all.size(), 2u);
}

TEST_F(RecipeRegistryTest, GetRecipesForStation) {
    auto r1 = makeSmeltingRecipe("iron", "rtest_furnace");
    auto r2 = makeSmeltingRecipe("gold", "rtest_furnace");
    auto r3 = makeSmeltingRecipe("copper", "rtest_kiln");

    RecipeRegistry::global().registerRecipe("rtest:smelt_iron", r1);
    RecipeRegistry::global().registerRecipe("rtest:smelt_gold", r2);
    RecipeRegistry::global().registerRecipe("rtest:smelt_copper", r3);

    auto furnaceRecipes = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest_furnace"));
    EXPECT_EQ(furnaceRecipes.size(), 2u);

    auto kilnRecipes = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest_kiln"));
    EXPECT_EQ(kilnRecipes.size(), 1u);

    auto emptyStation = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest_nonexistent"));
    EXPECT_EQ(emptyStation.size(), 0u);
}

TEST_F(RecipeRegistryTest, GetRecipesForOutput) {
    auto r1 = makeShapedRecipe("planks_a");
    r1.outputItem = ItemTypeId::fromName("rtest_planks");
    auto r2 = makeShapelessRecipe("planks_b");
    r2.outputItem = ItemTypeId::fromName("rtest_planks");
    auto r3 = makeShapedRecipe("sticks");
    r3.outputItem = ItemTypeId::fromName("rtest_sticks");

    RecipeRegistry::global().registerRecipe("rtest:planks_a", r1);
    RecipeRegistry::global().registerRecipe("rtest:planks_b", r2);
    RecipeRegistry::global().registerRecipe("rtest:sticks", r3);

    auto plankRecipes = RecipeRegistry::global().getRecipesForOutput(
        ItemTypeId::fromName("rtest_planks"));
    EXPECT_EQ(plankRecipes.size(), 2u);

    auto stickRecipes = RecipeRegistry::global().getRecipesForOutput(
        ItemTypeId::fromName("rtest_sticks"));
    EXPECT_EQ(stickRecipes.size(), 1u);
}

TEST_F(RecipeRegistryTest, FindSmeltingRecipe) {
    auto r1 = makeSmeltingRecipe("iron_smelt", "rtest_furnace");
    RecipeRegistry::global().registerRecipe("rtest:iron_smelt", r1);

    auto found = RecipeRegistry::global().findSmeltingRecipe(
        ItemTypeId::fromName("rtest_in_iron_smelt"),
        StationTypeId::fromName("rtest_furnace"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->type, RecipeType::Smelting);

    // Wrong station
    auto notFound = RecipeRegistry::global().findSmeltingRecipe(
        ItemTypeId::fromName("rtest_in_iron_smelt"),
        StationTypeId::fromName("rtest_kiln"));
    EXPECT_EQ(notFound, nullptr);

    // Wrong input
    auto notFound2 = RecipeRegistry::global().findSmeltingRecipe(
        ItemTypeId::fromName("rtest_in_nonexistent"),
        StationTypeId::fromName("rtest_furnace"));
    EXPECT_EQ(notFound2, nullptr);
}

TEST_F(RecipeRegistryTest, HandCraftingEmptyStation) {
    auto r = makeShapedRecipe("hand");
    r.station = EMPTY_STATION;  // hand-crafting
    RecipeRegistry::global().registerRecipe("rtest:hand", r);

    auto handRecipes = RecipeRegistry::global().getRecipesForStation(EMPTY_STATION);
    EXPECT_EQ(handRecipes.size(), 1u);
}

TEST_F(RecipeRegistryTest, ThreadSafety) {
    // Register from multiple threads
    std::vector<std::thread> threads;
    constexpr int numThreads = 4;
    constexpr int recipesPerThread = 25;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([t]() {
            for (int i = 0; i < recipesPerThread; ++i) {
                std::string name = "rtest:thread_" + std::to_string(t) + "_" + std::to_string(i);
                Recipe r;
                r.type = RecipeType::Shapeless;
                r.ingredients.push_back(ItemMatch::exact(ItemTypeId::fromName("rtest_any")));
                r.outputItem = ItemTypeId::fromName("rtest_out_thread");
                RecipeRegistry::global().registerRecipe(name, r);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(RecipeRegistry::global().size(), numThreads * recipesPerThread);
}

// ============================================================================
// RecipeLoader — parseIngredient Tests
// ============================================================================

class RecipeLoaderIngredientTest : public ::testing::Test {};

TEST_F(RecipeLoaderIngredientTest, EmptyUnderscore) {
    auto m = RecipeLoader::parseIngredient("_");
    EXPECT_TRUE(m.isEmpty());
}

TEST_F(RecipeLoaderIngredientTest, EmptyWord) {
    auto m = RecipeLoader::parseIngredient("empty");
    EXPECT_TRUE(m.isEmpty());
}

TEST_F(RecipeLoaderIngredientTest, EmptyString) {
    auto m = RecipeLoader::parseIngredient("");
    EXPECT_TRUE(m.isEmpty());
}

TEST_F(RecipeLoaderIngredientTest, ExactItem) {
    auto m = RecipeLoader::parseIngredient("finevox:oak_planks");
    EXPECT_TRUE(m.isExact());
}

TEST_F(RecipeLoaderIngredientTest, TaggedIngredient) {
    auto m = RecipeLoader::parseIngredient("#common:planks");
    EXPECT_TRUE(m.isTagged());
}

// ============================================================================
// RecipeLoader — Shaped Recipe Tests
// ============================================================================

class RecipeLoaderShapedTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }
    void TearDown() override {
        RecipeRegistry::global().clear();
    }
};

TEST_F(RecipeLoaderShapedTest, Basic2x2) {
    const char* content = R"(
name: rtest:workbench
type: shaped
station: none
category: building
width: 2
height: 2
row: rtest:planks rtest:planks
row: rtest:planks rtest:planks
output: rtest:workbench_block
count: 1
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->type, RecipeType::Shaped);
    EXPECT_EQ(recipe->id.name(), "rtest:workbench");
    EXPECT_TRUE(recipe->station.isEmpty());
    EXPECT_EQ(recipe->category, "building");
    EXPECT_EQ(recipe->width, 2);
    EXPECT_EQ(recipe->height, 2);
    ASSERT_EQ(recipe->pattern.size(), 4u);
    for (const auto& m : recipe->pattern) {
        EXPECT_TRUE(m.isExact());
    }
    EXPECT_EQ(recipe->outputCount, 1);
}

TEST_F(RecipeLoaderShapedTest, InferDimensions) {
    const char* content = R"(
name: rtest:infer
type: shaped
row: rtest:a rtest:b rtest:c
row: rtest:d rtest:e rtest:f
output: rtest:infer_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->width, 3);
    EXPECT_EQ(recipe->height, 2);
    EXPECT_EQ(recipe->pattern.size(), 6u);
}

TEST_F(RecipeLoaderShapedTest, EmptyCells) {
    const char* content = R"(
name: rtest:sword
type: shaped
width: 1
height: 3
row: rtest:iron
row: rtest:iron
row: rtest:stick
output: rtest:sword_item
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->width, 1);
    EXPECT_EQ(recipe->height, 3);
    ASSERT_EQ(recipe->pattern.size(), 3u);
    EXPECT_TRUE(recipe->pattern[0].isExact());
    EXPECT_TRUE(recipe->pattern[1].isExact());
    EXPECT_TRUE(recipe->pattern[2].isExact());
}

TEST_F(RecipeLoaderShapedTest, EmptyCellsUnderscore) {
    const char* content = R"(
name: rtest:hoe
type: shaped
row: rtest:iron rtest:iron
row: _ rtest:stick
row: _ rtest:stick
output: rtest:hoe_item
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->width, 2);
    EXPECT_EQ(recipe->height, 3);
    ASSERT_EQ(recipe->pattern.size(), 6u);
    EXPECT_TRUE(recipe->pattern[0].isExact());  // iron
    EXPECT_TRUE(recipe->pattern[1].isExact());  // iron
    EXPECT_TRUE(recipe->pattern[2].isEmpty());   // _
    EXPECT_TRUE(recipe->pattern[3].isExact());  // stick
    EXPECT_TRUE(recipe->pattern[4].isEmpty());   // _
    EXPECT_TRUE(recipe->pattern[5].isExact());  // stick
}

TEST_F(RecipeLoaderShapedTest, TaggedIngredients) {
    const char* content = R"(
name: rtest:tagged_bench
type: shaped
row: #common:planks #common:planks
row: #common:planks #common:planks
output: rtest:tagged_bench_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    ASSERT_EQ(recipe->pattern.size(), 4u);
    for (const auto& m : recipe->pattern) {
        EXPECT_TRUE(m.isTagged());
    }
}

TEST_F(RecipeLoaderShapedTest, MirrorFlag) {
    const char* content = R"(
name: rtest:mirror_recipe
type: shaped
mirror: true
row: rtest:a rtest:b
row: _ rtest:c
output: rtest:mirror_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_TRUE(recipe->allowMirror);
}

TEST_F(RecipeLoaderShapedTest, MirrorFalseByDefault) {
    const char* content = R"(
name: rtest:nomirror
type: shaped
row: rtest:a
output: rtest:nomirror_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_FALSE(recipe->allowMirror);
}

TEST_F(RecipeLoaderShapedTest, WithStation) {
    const char* content = R"(
name: rtest:station_recipe
type: shaped
station: rtest:stonecutter
row: rtest:stone
output: rtest:cut_stone
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_FALSE(recipe->station.isEmpty());
    EXPECT_EQ(recipe->station.name(), "rtest:stonecutter");
}

TEST_F(RecipeLoaderShapedTest, UnevenRows) {
    // Shorter rows should be padded with empty
    const char* content = R"(
name: rtest:uneven
type: shaped
width: 3
row: rtest:a rtest:b rtest:c
row: rtest:d
output: rtest:uneven_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->width, 3);
    EXPECT_EQ(recipe->height, 2);
    ASSERT_EQ(recipe->pattern.size(), 6u);
    EXPECT_TRUE(recipe->pattern[0].isExact());   // a
    EXPECT_TRUE(recipe->pattern[1].isExact());   // b
    EXPECT_TRUE(recipe->pattern[2].isExact());   // c
    EXPECT_TRUE(recipe->pattern[3].isExact());   // d
    EXPECT_TRUE(recipe->pattern[4].isEmpty());    // padded
    EXPECT_TRUE(recipe->pattern[5].isEmpty());    // padded
}

// ============================================================================
// RecipeLoader — Shapeless Recipe Tests
// ============================================================================

class RecipeLoaderShapelessTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }
    void TearDown() override {
        RecipeRegistry::global().clear();
    }
};

TEST_F(RecipeLoaderShapelessTest, SingleIngredient) {
    const char* content = R"(
name: rtest:oak_planks
type: shapeless
station: none
category: materials
ingredient: rtest:oak_log
output: rtest:oak_planks_item
count: 4
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->type, RecipeType::Shapeless);
    EXPECT_EQ(recipe->id.name(), "rtest:oak_planks");
    EXPECT_TRUE(recipe->station.isEmpty());
    EXPECT_EQ(recipe->category, "materials");
    ASSERT_EQ(recipe->ingredients.size(), 1u);
    EXPECT_TRUE(recipe->ingredients[0].isExact());
    EXPECT_EQ(recipe->outputCount, 4);
}

TEST_F(RecipeLoaderShapelessTest, MultipleIngredients) {
    const char* content = R"(
name: rtest:multi_shapeless
type: shapeless
ingredient: rtest:item_a
ingredient: rtest:item_b
ingredient: rtest:item_c
output: rtest:multi_out
count: 2
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    ASSERT_EQ(recipe->ingredients.size(), 3u);
    EXPECT_EQ(recipe->outputCount, 2);
}

TEST_F(RecipeLoaderShapelessTest, TaggedIngredients) {
    const char* content = R"(
name: rtest:tagged_shapeless
type: shapeless
ingredient: #common:dyes
ingredient: rtest:wool
output: rtest:colored_wool
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    ASSERT_EQ(recipe->ingredients.size(), 2u);
    EXPECT_TRUE(recipe->ingredients[0].isTagged());
    EXPECT_TRUE(recipe->ingredients[1].isExact());
}

// ============================================================================
// RecipeLoader — Smelting Recipe Tests
// ============================================================================

class RecipeLoaderSmeltingTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }
    void TearDown() override {
        RecipeRegistry::global().clear();
    }
};

TEST_F(RecipeLoaderSmeltingTest, Basic) {
    const char* content = R"(
name: rtest:iron_ingot
type: smelting
station: rtest:furnace
category: materials
input: rtest:raw_iron
smelt_time: 200
experience: 0.7
output: rtest:iron_ingot_item
count: 1
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->type, RecipeType::Smelting);
    EXPECT_EQ(recipe->id.name(), "rtest:iron_ingot");
    EXPECT_EQ(recipe->station.name(), "rtest:furnace");
    EXPECT_EQ(recipe->category, "materials");
    EXPECT_TRUE(recipe->smeltInput.isExact());
    EXPECT_EQ(recipe->smeltTicks, 200);
    EXPECT_FLOAT_EQ(recipe->experience, 0.7f);
    EXPECT_EQ(recipe->outputCount, 1);
}

TEST_F(RecipeLoaderSmeltingTest, TaggedInput) {
    const char* content = R"(
name: rtest:charcoal
type: smelting
station: rtest:furnace
input: #common:logs
smelt_time: 100
experience: 0.15
output: rtest:charcoal_item
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_TRUE(recipe->smeltInput.isTagged());
    EXPECT_EQ(recipe->smeltTicks, 100);
    EXPECT_FLOAT_EQ(recipe->experience, 0.15f);
}

TEST_F(RecipeLoaderSmeltingTest, DefaultSmeltTime) {
    const char* content = R"(
name: rtest:default_smelt
type: smelting
station: rtest:furnace
input: rtest:some_ore
output: rtest:some_ingot
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->smeltTicks, 200);  // default
    EXPECT_EQ(recipe->experience, 0.0f);  // default
}

// ============================================================================
// RecipeLoader — Error/Edge Cases
// ============================================================================

class RecipeLoaderEdgeCaseTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }
    void TearDown() override {
        RecipeRegistry::global().clear();
    }
};

TEST_F(RecipeLoaderEdgeCaseTest, EmptyContent) {
    auto recipe = RecipeLoader::loadFromString("");
    EXPECT_FALSE(recipe.has_value());
}

TEST_F(RecipeLoaderEdgeCaseTest, MissingName) {
    const char* content = R"(
type: shaped
row: rtest:a
output: rtest:out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    EXPECT_FALSE(recipe.has_value());
}

TEST_F(RecipeLoaderEdgeCaseTest, OnlyName) {
    // Missing type — defaults to Shaped, but has no pattern. Should still parse.
    const char* content = R"(
name: rtest:minimal
output: rtest:minimal_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->type, RecipeType::Shaped);
    EXPECT_TRUE(recipe->pattern.empty());
}

TEST_F(RecipeLoaderEdgeCaseTest, DefaultCount) {
    const char* content = R"(
name: rtest:default_count
type: shapeless
ingredient: rtest:x
output: rtest:default_count_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->outputCount, 1);  // default
}

TEST_F(RecipeLoaderEdgeCaseTest, NonexistentFile) {
    auto recipe = RecipeLoader::loadFromFile("/nonexistent/path/recipe.recipe");
    EXPECT_FALSE(recipe.has_value());
}

TEST_F(RecipeLoaderEdgeCaseTest, CommentsIgnored) {
    const char* content = R"(
# This is a comment
name: rtest:commented
type: shapeless
# Another comment
ingredient: rtest:item
output: rtest:commented_out
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_EQ(recipe->id.name(), "rtest:commented");
}

TEST_F(RecipeLoaderEdgeCaseTest, EmptyDirectory) {
    auto count = RecipeLoader::loadDirectory("/nonexistent/dir");
    EXPECT_EQ(count, 0u);
}

// ============================================================================
// Integration Tests — Loader + Registry
// ============================================================================

class RecipeIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        RecipeRegistry::global().clear();
    }
    void TearDown() override {
        RecipeRegistry::global().clear();
    }
};

TEST_F(RecipeIntegrationTest, LoadAndRegisterShaped) {
    const char* content = R"(
name: rtest:int_workbench
type: shaped
station: none
category: building
row: rtest:planks rtest:planks
row: rtest:planks rtest:planks
output: rtest:workbench_block
count: 1
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());

    EXPECT_TRUE(RecipeRegistry::global().registerRecipe("rtest:int_workbench", std::move(*recipe)));

    const Recipe* found = RecipeRegistry::global().getRecipe("rtest:int_workbench");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->isShaped());
    EXPECT_EQ(found->width, 2);
    EXPECT_EQ(found->height, 2);
    EXPECT_EQ(found->pattern.size(), 4u);
}

TEST_F(RecipeIntegrationTest, LoadAndRegisterSmelting) {
    const char* content = R"(
name: rtest:int_smelt
type: smelting
station: rtest:furnace
input: rtest:raw_ore
smelt_time: 150
experience: 1.0
output: rtest:refined_ore
count: 1
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());

    EXPECT_TRUE(RecipeRegistry::global().registerRecipe("rtest:int_smelt", std::move(*recipe)));

    auto found = RecipeRegistry::global().findSmeltingRecipe(
        ItemTypeId::fromName("rtest:raw_ore"),
        StationTypeId::fromName("rtest:furnace"));
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->smeltTicks, 150);
    EXPECT_FLOAT_EQ(found->experience, 1.0f);
}

TEST_F(RecipeIntegrationTest, StationQueryAfterLoad) {
    const char* r1 = R"(
name: rtest:int_st1
type: smelting
station: rtest:int_furnace
input: rtest:a
output: rtest:b
)";
    const char* r2 = R"(
name: rtest:int_st2
type: smelting
station: rtest:int_furnace
input: rtest:c
output: rtest:d
)";
    const char* r3 = R"(
name: rtest:int_st3
type: shaped
station: rtest:int_anvil
row: rtest:x
output: rtest:y
)";

    auto rec1 = RecipeLoader::loadFromString(r1);
    auto rec2 = RecipeLoader::loadFromString(r2);
    auto rec3 = RecipeLoader::loadFromString(r3);

    RecipeRegistry::global().registerRecipe("rtest:int_st1", std::move(*rec1));
    RecipeRegistry::global().registerRecipe("rtest:int_st2", std::move(*rec2));
    RecipeRegistry::global().registerRecipe("rtest:int_st3", std::move(*rec3));

    auto furnace = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest:int_furnace"));
    EXPECT_EQ(furnace.size(), 2u);

    auto anvil = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest:int_anvil"));
    EXPECT_EQ(anvil.size(), 1u);
}

TEST_F(RecipeIntegrationTest, OutputQueryAfterLoad) {
    const char* r1 = R"(
name: rtest:int_out1
type: shapeless
ingredient: rtest:log
output: rtest:int_planks
count: 4
)";
    const char* r2 = R"(
name: rtest:int_out2
type: shaped
row: rtest:a rtest:b
row: rtest:c rtest:d
output: rtest:int_planks
)";

    auto rec1 = RecipeLoader::loadFromString(r1);
    auto rec2 = RecipeLoader::loadFromString(r2);

    RecipeRegistry::global().registerRecipe("rtest:int_out1", std::move(*rec1));
    RecipeRegistry::global().registerRecipe("rtest:int_out2", std::move(*rec2));

    auto planks = RecipeRegistry::global().getRecipesForOutput(
        ItemTypeId::fromName("rtest:int_planks"));
    EXPECT_EQ(planks.size(), 2u);
}

TEST_F(RecipeIntegrationTest, TagBasedMatchThroughTagRegistry) {
    // Register a tag and verify ItemMatch::tagged works through the recipe
    TagRegistry::global().addMember(
        TagId::fromName("rtest:logs"),
        ItemTypeId::fromName("rtest:oak_log_item"));
    TagRegistry::global().addMember(
        TagId::fromName("rtest:logs"),
        ItemTypeId::fromName("rtest:birch_log_item"));
    TagRegistry::global().rebuild();

    const char* content = R"(
name: rtest:int_tagged
type: smelting
station: rtest:furnace
input: #rtest:logs
experience: 0.15
output: rtest:charcoal_item
)";

    auto recipe = RecipeLoader::loadFromString(content);
    ASSERT_TRUE(recipe.has_value());
    EXPECT_TRUE(recipe->smeltInput.isTagged());

    // The tag-based input should match both log types
    EXPECT_TRUE(recipe->smeltInput.matches(ItemTypeId::fromName("rtest:oak_log_item")));
    EXPECT_TRUE(recipe->smeltInput.matches(ItemTypeId::fromName("rtest:birch_log_item")));
    EXPECT_FALSE(recipe->smeltInput.matches(ItemTypeId::fromName("rtest:stone_item")));
}

TEST_F(RecipeIntegrationTest, MultipleRecipesFullWorkflow) {
    // Register several recipes and verify all queries work
    const char* recipes[] = {
        R"(
name: rtest:wf_planks
type: shapeless
station: none
ingredient: rtest:wf_log
output: rtest:wf_planks_item
count: 4
)",
        R"(
name: rtest:wf_sticks
type: shaped
station: none
row: rtest:wf_planks_item
row: rtest:wf_planks_item
output: rtest:wf_stick_item
count: 4
)",
        R"(
name: rtest:wf_bench
type: shaped
station: none
row: rtest:wf_planks_item rtest:wf_planks_item
row: rtest:wf_planks_item rtest:wf_planks_item
output: rtest:wf_bench_item
)",
        R"(
name: rtest:wf_smelt
type: smelting
station: rtest:wf_furnace
input: rtest:wf_ore
smelt_time: 200
experience: 0.5
output: rtest:wf_ingot
)"
    };

    for (const auto& content : recipes) {
        auto recipe = RecipeLoader::loadFromString(content);
        ASSERT_TRUE(recipe.has_value());
        auto name = recipe->id.name();
        EXPECT_TRUE(RecipeRegistry::global().registerRecipe(name, std::move(*recipe)));
    }

    EXPECT_EQ(RecipeRegistry::global().size(), 4u);

    // Hand-crafting recipes (station = none)
    auto handRecipes = RecipeRegistry::global().getRecipesForStation(EMPTY_STATION);
    EXPECT_EQ(handRecipes.size(), 3u);

    // Furnace recipes
    auto furnaceRecipes = RecipeRegistry::global().getRecipesForStation(
        StationTypeId::fromName("rtest:wf_furnace"));
    EXPECT_EQ(furnaceRecipes.size(), 1u);

    // Find smelting recipe
    auto smelt = RecipeRegistry::global().findSmeltingRecipe(
        ItemTypeId::fromName("rtest:wf_ore"),
        StationTypeId::fromName("rtest:wf_furnace"));
    ASSERT_NE(smelt, nullptr);
    EXPECT_FLOAT_EQ(smelt->experience, 0.5f);

    // Recipes for planks output
    auto planksRecipes = RecipeRegistry::global().getRecipesForOutput(
        ItemTypeId::fromName("rtest:wf_planks_item"));
    EXPECT_EQ(planksRecipes.size(), 1u);
}
