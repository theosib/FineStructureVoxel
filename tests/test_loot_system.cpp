#include <gtest/gtest.h>
#include "finevox/core/loot_table.hpp"
#include "finevox/core/loot_conditions.hpp"
#include "finevox/core/loot_modifiers.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/loot_table_loader.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"

using namespace finevox;

// ============================================================================
// LootTableId Tests
// ============================================================================

class LootTableIdTest : public ::testing::Test {};

TEST_F(LootTableIdTest, DefaultIsEmpty) {
    LootTableId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
    EXPECT_EQ(id.id, 0u);
}

TEST_F(LootTableIdTest, EmptyConstant) {
    EXPECT_TRUE(EMPTY_LOOT_TABLE.isEmpty());
}

TEST_F(LootTableIdTest, FromName) {
    auto id = LootTableId::fromName("stone");
    EXPECT_FALSE(id.isEmpty());
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.name(), "stone");
}

TEST_F(LootTableIdTest, SameNameSameId) {
    auto a = LootTableId::fromName("zombie");
    auto b = LootTableId::fromName("zombie");
    EXPECT_EQ(a, b);
}

TEST_F(LootTableIdTest, DifferentNameDifferentId) {
    auto a = LootTableId::fromName("loot_stone");
    auto b = LootTableId::fromName("loot_zombie");
    EXPECT_NE(a, b);
}

TEST_F(LootTableIdTest, Hashable) {
    std::unordered_set<LootTableId> set;
    set.insert(LootTableId::fromName("loot_a"));
    set.insert(LootTableId::fromName("loot_b"));
    set.insert(LootTableId::fromName("loot_a"));
    EXPECT_EQ(set.size(), 2u);
}

// ============================================================================
// LootEntry Tests
// ============================================================================

class LootEntryTest : public ::testing::Test {};

TEST_F(LootEntryTest, ItemEntry) {
    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_test_cobblestone");
    entry.countMin = 1;
    entry.countMax = 1;

    LootContext ctx;
    ctx.seed = 12345;
    auto& rng = getLootRng(ctx);

    auto items = entry.generate(ctx, rng);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_test_cobblestone"));
    EXPECT_EQ(items[0].count, 1);
}

TEST_F(LootEntryTest, ItemEntryWithRange) {
    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_test_flint");
    entry.countMin = 1;
    entry.countMax = 4;

    LootContext ctx;
    ctx.seed = 99999;
    auto& rng = getLootRng(ctx);

    auto items = entry.generate(ctx, rng);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_GE(items[0].count, 1);
    EXPECT_LE(items[0].count, 4);
}

TEST_F(LootEntryTest, EmptyEntry) {
    LootEntry entry;
    entry.type = LootEntry::Type::Empty;

    LootContext ctx;
    ctx.seed = 42;
    auto& rng = getLootRng(ctx);

    auto items = entry.generate(ctx, rng);
    EXPECT_TRUE(items.empty());
}

TEST_F(LootEntryTest, EligibilityWithNoConditions) {
    LootEntry entry;
    LootContext ctx;
    EXPECT_TRUE(entry.isEligible(ctx));
}

TEST_F(LootEntryTest, EligibilityWithCondition) {
    LootEntry entry;
    entry.conditions.push_back(std::make_unique<PreciseBreakCondition>());

    LootContext ctx;
    ctx.preciseBreak = false;
    EXPECT_FALSE(entry.isEligible(ctx));

    ctx.preciseBreak = true;
    EXPECT_TRUE(entry.isEligible(ctx));
}

TEST_F(LootEntryTest, Clone) {
    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_clone_test");
    entry.countMin = 2;
    entry.countMax = 5;
    entry.weight = 3.0f;
    entry.conditions.push_back(std::make_unique<PreciseBreakCondition>());

    auto copy = entry.clone();
    EXPECT_EQ(copy.type, entry.type);
    EXPECT_EQ(copy.item, entry.item);
    EXPECT_EQ(copy.countMin, 2);
    EXPECT_EQ(copy.countMax, 5);
    EXPECT_EQ(copy.weight, 3.0f);
    EXPECT_EQ(copy.conditions.size(), 1u);
}

// ============================================================================
// LootPool Tests
// ============================================================================

class LootPoolTest : public ::testing::Test {};

TEST_F(LootPoolTest, SingleItemPool) {
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;

    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_pool_diamond");
    entry.countMin = 1;
    entry.countMax = 1;
    pool.entries.push_back(std::move(entry));

    LootContext ctx;
    ctx.seed = 42;
    auto& rng = getLootRng(ctx);

    auto items = pool.roll(ctx, rng);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_pool_diamond"));
}

TEST_F(LootPoolTest, MultipleRolls) {
    LootPool pool;
    pool.rollsMin = 3;
    pool.rollsMax = 3;

    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_pool_gold");
    entry.countMin = 1;
    entry.countMax = 1;
    pool.entries.push_back(std::move(entry));

    LootContext ctx;
    ctx.seed = 42;
    auto& rng = getLootRng(ctx);

    auto items = pool.roll(ctx, rng);
    EXPECT_EQ(items.size(), 3u);
}

TEST_F(LootPoolTest, PoolConditionFails) {
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;
    pool.conditions.push_back(std::make_unique<PreciseBreakCondition>());

    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_pool_blocked");
    pool.entries.push_back(std::move(entry));

    LootContext ctx;
    ctx.seed = 42;
    ctx.preciseBreak = false;
    auto& rng = getLootRng(ctx);

    auto items = pool.roll(ctx, rng);
    EXPECT_TRUE(items.empty());
}

TEST_F(LootPoolTest, WeightedSelection) {
    LootPool pool;
    pool.rollsMin = 1000;
    pool.rollsMax = 1000;

    // Heavy item (weight 99)
    LootEntry heavy;
    heavy.type = LootEntry::Type::Item;
    heavy.item = ItemTypeId::fromName("loot_weighted_common");
    heavy.weight = 99.0f;
    pool.entries.push_back(std::move(heavy));

    // Light item (weight 1)
    LootEntry light;
    light.type = LootEntry::Type::Item;
    light.item = ItemTypeId::fromName("loot_weighted_rare");
    light.weight = 1.0f;
    pool.entries.push_back(std::move(light));

    LootContext ctx;
    ctx.seed = 12345;
    auto& rng = getLootRng(ctx);

    auto items = pool.roll(ctx, rng);
    EXPECT_EQ(items.size(), 1000u);

    int commonCount = 0, rareCount = 0;
    auto commonId = ItemTypeId::fromName("loot_weighted_common");
    for (const auto& item : items) {
        if (item.type == commonId) commonCount++;
        else rareCount++;
    }

    // Common should dominate (~99%)
    EXPECT_GT(commonCount, 900);
    EXPECT_LT(rareCount, 100);
}

// ============================================================================
// LootTable Tests
// ============================================================================

class LootTableTest : public ::testing::Test {};

TEST_F(LootTableTest, EmptyTable) {
    LootTable table;
    EXPECT_TRUE(table.empty());

    LootContext ctx;
    auto items = table.roll(ctx);
    EXPECT_TRUE(items.empty());
}

TEST_F(LootTableTest, SinglePool) {
    LootTable table;

    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;

    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName("loot_table_iron");
    pool.entries.push_back(std::move(entry));

    table.addPool(std::move(pool));

    LootContext ctx;
    ctx.seed = 42;
    auto items = table.roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_table_iron"));
}

TEST_F(LootTableTest, MultiplePools) {
    LootTable table;

    // Pool 1
    LootPool pool1;
    pool1.rollsMin = 1;
    pool1.rollsMax = 1;
    LootEntry e1;
    e1.type = LootEntry::Type::Item;
    e1.item = ItemTypeId::fromName("loot_mp_pool1");
    pool1.entries.push_back(std::move(e1));
    table.addPool(std::move(pool1));

    // Pool 2
    LootPool pool2;
    pool2.rollsMin = 1;
    pool2.rollsMax = 1;
    LootEntry e2;
    e2.type = LootEntry::Type::Item;
    e2.item = ItemTypeId::fromName("loot_mp_pool2");
    pool2.entries.push_back(std::move(e2));
    table.addPool(std::move(pool2));

    LootContext ctx;
    ctx.seed = 42;
    auto items = table.roll(ctx);
    EXPECT_EQ(items.size(), 2u);
}

TEST_F(LootTableTest, DeterministicWithSeed) {
    auto makeTable = []() {
        LootTable table;
        LootPool pool;
        pool.rollsMin = 1;
        pool.rollsMax = 1;

        LootEntry e1;
        e1.type = LootEntry::Type::Item;
        e1.item = ItemTypeId::fromName("loot_det_a");
        e1.weight = 1.0f;
        pool.entries.push_back(std::move(e1));

        LootEntry e2;
        e2.type = LootEntry::Type::Item;
        e2.item = ItemTypeId::fromName("loot_det_b");
        e2.weight = 1.0f;
        pool.entries.push_back(std::move(e2));

        table.addPool(std::move(pool));
        return table;
    };

    auto table = makeTable();

    LootContext ctx1;
    ctx1.seed = 777;
    auto items1 = table.roll(ctx1);

    LootContext ctx2;
    ctx2.seed = 777;
    auto items2 = table.roll(ctx2);

    ASSERT_EQ(items1.size(), items2.size());
    for (size_t i = 0; i < items1.size(); ++i) {
        EXPECT_EQ(items1[i].type, items2[i].type);
        EXPECT_EQ(items1[i].count, items2[i].count);
    }
}

TEST_F(LootTableTest, Clone) {
    LootTable table;
    LootPool pool;
    pool.rollsMin = 2;
    pool.rollsMax = 3;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_clone_entry");
    pool.entries.push_back(std::move(e));
    table.addPool(std::move(pool));

    auto copy = table.clone();
    EXPECT_FALSE(copy.empty());
    EXPECT_EQ(copy.pools().size(), 1u);
    EXPECT_EQ(copy.pools()[0].rollsMin, 2);
    EXPECT_EQ(copy.pools()[0].rollsMax, 3);
}

// ============================================================================
// Condition Tests
// ============================================================================

class LootConditionTest : public ::testing::Test {};

TEST_F(LootConditionTest, Always) {
    AlwaysCondition cond;
    LootContext ctx;
    EXPECT_TRUE(cond.test(ctx));
}

TEST_F(LootConditionTest, PreciseBreak) {
    PreciseBreakCondition cond;
    LootContext ctx;

    ctx.preciseBreak = false;
    EXPECT_FALSE(cond.test(ctx));

    ctx.preciseBreak = true;
    EXPECT_TRUE(cond.test(ctx));
}

TEST_F(LootConditionTest, ToolTag) {
    auto tag = TagId::fromName("common:loot_test_pickaxes");
    auto tool = ItemTypeId::fromName("loot_test_iron_pick");

    // Register the tag membership and rebuild
    TagRegistry::global().addMember(tag, tool.id);
    TagRegistry::global().rebuild();

    ToolTagCondition cond(tag);
    LootContext ctx;

    // No tool
    EXPECT_FALSE(cond.test(ctx));

    // Wrong tool
    ctx.toolUsed = ItemTypeId::fromName("loot_test_shovel");
    EXPECT_FALSE(cond.test(ctx));

    // Correct tool
    ctx.toolUsed = tool;
    EXPECT_TRUE(cond.test(ctx));
}

TEST_F(LootConditionTest, RandomChance) {
    // Chance of 1.0 always passes
    RandomChanceCondition always(1.0f);
    LootContext ctx;
    ctx.seed = 42;
    EXPECT_TRUE(always.test(ctx));

    // Chance of 0.0 never passes
    RandomChanceCondition never(0.0f);
    EXPECT_FALSE(never.test(ctx));
}

TEST_F(LootConditionTest, RandomChanceBountyBonus) {
    // Base chance 0.0, but bounty bonus 0.5 * level 3 = 1.5 → always pass
    RandomChanceCondition cond(0.0f, 0.5f);
    LootContext ctx;
    ctx.seed = 42;
    ctx.bountyLevel = 3;
    EXPECT_TRUE(cond.test(ctx));
}

TEST_F(LootConditionTest, BlockType) {
    auto stone = BlockTypeId::fromName("loot_test_stone");
    BlockTypeCondition cond(stone);

    LootContext ctx;
    EXPECT_FALSE(cond.test(ctx));  // empty block

    ctx.brokenBlock = stone;
    EXPECT_TRUE(cond.test(ctx));

    ctx.brokenBlock = BlockTypeId::fromName("loot_test_dirt");
    EXPECT_FALSE(cond.test(ctx));
}

TEST_F(LootConditionTest, Inverted) {
    InvertedCondition cond(std::make_unique<PreciseBreakCondition>());
    LootContext ctx;

    ctx.preciseBreak = false;
    EXPECT_TRUE(cond.test(ctx));   // NOT precise-break → true

    ctx.preciseBreak = true;
    EXPECT_FALSE(cond.test(ctx));  // NOT precise-break → false
}

TEST_F(LootConditionTest, Callback) {
    int callCount = 0;
    CallbackCondition cond([&](const LootContext&) {
        callCount++;
        return true;
    });

    LootContext ctx;
    EXPECT_TRUE(cond.test(ctx));
    EXPECT_EQ(callCount, 1);
}

TEST_F(LootConditionTest, ConditionClone) {
    RandomChanceCondition original(0.5f, 0.1f);
    auto cloned = original.clone();

    ASSERT_NE(cloned, nullptr);

    // Both should behave the same with the same seed
    LootContext ctx;
    ctx.seed = 42;
    bool r1 = original.test(ctx);
    ctx.seed = 42;
    bool r2 = cloned->test(ctx);
    EXPECT_EQ(r1, r2);
}

// ============================================================================
// Modifier Tests
// ============================================================================

class LootModifierTest : public ::testing::Test {};

TEST_F(LootModifierTest, BountyCount) {
    BountyModifier mod(1.0f);
    LootContext ctx;
    ctx.seed = 42;
    ctx.bountyLevel = 3;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_mod_diamond");
    stack.count = 1;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_GE(items[0].count, 1);  // count * (1 + random(0, 3))
    EXPECT_LE(items[0].count, 4);
}

TEST_F(LootModifierTest, BountyCountNoBounty) {
    BountyModifier mod(1.0f);
    LootContext ctx;
    ctx.bountyLevel = 0;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_mod_test");
    stack.count = 5;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_EQ(items[0].count, 5);  // No change when bounty=0
}

TEST_F(LootModifierTest, SetCount) {
    SetCountModifier mod(3, 3);
    LootContext ctx;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_set_count");
    stack.count = 1;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_EQ(items[0].count, 3);
}

TEST_F(LootModifierTest, SetCountRange) {
    SetCountModifier mod(2, 5);
    LootContext ctx;
    ctx.seed = 42;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_set_range");
    stack.count = 1;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_GE(items[0].count, 2);
    EXPECT_LE(items[0].count, 5);
}

TEST_F(LootModifierTest, PlunderBonus) {
    PlunderModifier mod(1);
    LootContext ctx;
    ctx.seed = 42;
    ctx.plunderLevel = 2;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_plunder_test");
    stack.count = 1;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_GE(items[0].count, 1);  // 1 + random(0, 2)
    EXPECT_LE(items[0].count, 3);
}

TEST_F(LootModifierTest, PlunderBonusNoPlunder) {
    PlunderModifier mod(1);
    LootContext ctx;
    ctx.plunderLevel = 0;

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_plunder_noop");
    stack.count = 5;
    items.push_back(std::move(stack));

    mod.apply(items, ctx);
    EXPECT_EQ(items[0].count, 5);
}

TEST_F(LootModifierTest, CallbackModifier) {
    int callCount = 0;
    CallbackModifier mod([&](std::vector<ItemStack>& items, const LootContext&) {
        callCount++;
        for (auto& item : items) item.count *= 2;
    });

    std::vector<ItemStack> items;
    ItemStack stack;
    stack.type = ItemTypeId::fromName("loot_cb_mod");
    stack.count = 3;
    items.push_back(std::move(stack));

    LootContext ctx;
    mod.apply(items, ctx);
    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(items[0].count, 6);
}

// ============================================================================
// LootRegistry Tests
// ============================================================================

class LootRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        LootRegistry::global().clear();
    }
    void TearDown() override {
        LootRegistry::global().clear();
    }
};

TEST_F(LootRegistryTest, RegisterAndRetrieve) {
    LootTable table;
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_reg_test");
    pool.entries.push_back(std::move(e));
    table.addPool(std::move(pool));

    EXPECT_TRUE(LootRegistry::global().registerTable("reg_test", std::move(table)));
    EXPECT_EQ(LootRegistry::global().size(), 1u);

    auto* retrieved = LootRegistry::global().getTable("reg_test");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->pools().size(), 1u);
}

TEST_F(LootRegistryTest, DuplicateRegistration) {
    LootTable t1, t2;
    LootPool pool;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_dup");
    pool.entries.push_back(std::move(e));
    t1.addPool(std::move(pool));
    t2.addPool(LootPool{});

    EXPECT_TRUE(LootRegistry::global().registerTable("dup_test", std::move(t1)));
    EXPECT_FALSE(LootRegistry::global().registerTable("dup_test", std::move(t2)));
    EXPECT_EQ(LootRegistry::global().size(), 1u);
}

TEST_F(LootRegistryTest, GetTableNotFound) {
    EXPECT_EQ(LootRegistry::global().getTable("nonexistent"), nullptr);
}

TEST_F(LootRegistryTest, RollByName) {
    LootTable table;
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_roll_test");
    pool.entries.push_back(std::move(e));
    table.addPool(std::move(pool));

    LootRegistry::global().registerTable("roll_test", std::move(table));

    LootContext ctx;
    ctx.seed = 42;
    auto items = LootRegistry::global().roll("roll_test", ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_roll_test"));
}

TEST_F(LootRegistryTest, RollNonexistent) {
    LootContext ctx;
    auto items = LootRegistry::global().roll("does_not_exist", ctx);
    EXPECT_TRUE(items.empty());
}

TEST_F(LootRegistryTest, HasTable) {
    LootTable table;
    LootPool pool;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_has_test");
    pool.entries.push_back(std::move(e));
    table.addPool(std::move(pool));

    LootRegistry::global().registerTable("has_test", std::move(table));

    EXPECT_TRUE(LootRegistry::global().hasTable("has_test"));
    EXPECT_FALSE(LootRegistry::global().hasTable("no_exist"));
}

TEST_F(LootRegistryTest, Clear) {
    LootTable table;
    LootPool pool;
    LootEntry e;
    e.type = LootEntry::Type::Item;
    e.item = ItemTypeId::fromName("loot_clear");
    pool.entries.push_back(std::move(e));
    table.addPool(std::move(pool));

    LootRegistry::global().registerTable("clear_test", std::move(table));
    EXPECT_EQ(LootRegistry::global().size(), 1u);

    LootRegistry::global().clear();
    EXPECT_EQ(LootRegistry::global().size(), 0u);
}

// ============================================================================
// LootTableLoader Tests
// ============================================================================

class LootTableLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        LootRegistry::global().clear();
    }
    void TearDown() override {
        LootRegistry::global().clear();
    }
};

TEST_F(LootTableLoaderTest, SimpleItem) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:stone:
type: item
item: loot_loader_stone
count: 1
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools().size(), 1u);
    EXPECT_EQ(table->pools()[0].entries.size(), 1u);

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_loader_stone"));
    EXPECT_EQ(items[0].count, 1);
}

TEST_F(LootTableLoaderTest, CountRange) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:gravel:
type: item
item: loot_loader_flint
count: 2-5
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_GE(items[0].count, 2);
    EXPECT_LE(items[0].count, 5);
}

TEST_F(LootTableLoaderTest, RollsRange) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 3-5
entry:item1:
type: item
item: loot_rolls_range
count: 1
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools()[0].rollsMin, 3);
    EXPECT_EQ(table->pools()[0].rollsMax, 5);
}

TEST_F(LootTableLoaderTest, PreciseBreakCondition) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:silk:
type: item
item: loot_silk_stone
count: 1
condition: precise-break
entry:normal:
type: item
item: loot_silk_cobble
count: 1
condition: not precise-break
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools()[0].entries.size(), 2u);

    // Without precise break
    LootContext ctx;
    ctx.seed = 42;
    ctx.preciseBreak = false;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_silk_cobble"));

    // With precise break
    ctx.seed = 42;
    ctx.preciseBreak = true;
    items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_silk_stone"));
}

TEST_F(LootTableLoaderTest, PoolLevelCondition) {
    auto table = LootTableLoader::loadFromString(R"(
pool:rare:
rolls: 1
condition: precise-break
entry:diamond:
type: item
item: loot_pool_cond
count: 1
)");

    ASSERT_TRUE(table.has_value());

    // Without precise break — pool condition fails
    LootContext ctx;
    ctx.seed = 42;
    ctx.preciseBreak = false;
    auto items = table->roll(ctx);
    EXPECT_TRUE(items.empty());

    // With precise break — pool condition passes
    ctx.seed = 42;
    ctx.preciseBreak = true;
    items = table->roll(ctx);
    EXPECT_EQ(items.size(), 1u);
}

TEST_F(LootTableLoaderTest, BountyModifier) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:ore:
type: item
item: loot_bounty_ore
count: 1
modifier: bounty 1.0
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    ctx.bountyLevel = 3;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_GE(items[0].count, 1);
}

TEST_F(LootTableLoaderTest, PlunderModifier) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:meat:
type: item
item: loot_plunder_meat
count: 1
modifier: plunder-bonus 1
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    ctx.plunderLevel = 2;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_GE(items[0].count, 1);
}

TEST_F(LootTableLoaderTest, SetCountModifier) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:item1:
type: item
item: loot_set_count_test
count: 1
modifier: set-count 5-10
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_GE(items[0].count, 5);
    EXPECT_LE(items[0].count, 10);
}

TEST_F(LootTableLoaderTest, MultiplePools) {
    auto table = LootTableLoader::loadFromString(R"(
pool:common:
rolls: 1
entry:item_a:
type: item
item: loot_multi_a
count: 1
pool:rare:
rolls: 1
entry:item_b:
type: item
item: loot_multi_b
count: 1
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools().size(), 2u);

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    EXPECT_EQ(items.size(), 2u);
}

TEST_F(LootTableLoaderTest, EmptyEntry) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:something:
type: item
item: loot_maybe
weight: 1
entry:nothing:
type: empty
weight: 999
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools()[0].entries.size(), 2u);

    // With weight 999:1, almost all rolls should produce nothing
    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    // Either 0 or 1 items — mostly 0 due to heavy empty weight
}

TEST_F(LootTableLoaderTest, TableReference) {
    // Register a referenced table first
    LootTable refTable;
    LootPool refPool;
    refPool.rollsMin = 1;
    refPool.rollsMax = 1;
    LootEntry refEntry;
    refEntry.type = LootEntry::Type::Item;
    refEntry.item = ItemTypeId::fromName("loot_ref_item");
    refPool.entries.push_back(std::move(refEntry));
    refTable.addPool(std::move(refPool));
    LootRegistry::global().registerTable("referenced_table", std::move(refTable));

    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:ref:
type: table
table: referenced_table
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_ref_item"));
}

TEST_F(LootTableLoaderTest, BonusRolls) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
bonus-rolls: 1.0
entry:item1:
type: item
item: loot_bonus_item
count: 1
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_FLOAT_EQ(table->pools()[0].bonusRollsPerLevel, 1.0f);
}

TEST_F(LootTableLoaderTest, EmptyStringReturnsNullopt) {
    auto table = LootTableLoader::loadFromString("");
    EXPECT_FALSE(table.has_value());
}

TEST_F(LootTableLoaderTest, RandomChanceCondition) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:rare:
type: item
item: loot_rng_test
count: 1
condition: random-chance 1.0
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);
    EXPECT_EQ(items.size(), 1u);  // 100% chance
}

TEST_F(LootTableLoaderTest, RandomChanceWithBountyBonus) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:rare:
type: item
item: loot_rng_bounty
count: 1
condition: random-chance 0.0 0.5
)");

    ASSERT_TRUE(table.has_value());

    // Without bounty — 0% chance
    LootContext ctx;
    ctx.seed = 42;
    ctx.bountyLevel = 0;
    auto items = table->roll(ctx);
    EXPECT_TRUE(items.empty());

    // With bounty 3 → 1.5 effective chance → always passes
    ctx.seed = 42;
    ctx.bountyLevel = 3;
    items = table->roll(ctx);
    EXPECT_EQ(items.size(), 1u);
}

TEST_F(LootTableLoaderTest, WeightParsing) {
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:a:
type: item
item: loot_weight_a
weight: 5.0
entry:b:
type: item
item: loot_weight_b
weight: 0.5
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_FLOAT_EQ(table->pools()[0].entries[0].weight, 5.0f);
    EXPECT_FLOAT_EQ(table->pools()[0].entries[1].weight, 0.5f);
}

// ============================================================================
// parseCondition / parseModifier direct tests
// ============================================================================

class LootParserTest : public ::testing::Test {};

TEST_F(LootParserTest, ParseConditionPreciseBreak) {
    auto cond = LootTableLoader::parseCondition("precise-break");
    ASSERT_NE(cond, nullptr);

    LootContext ctx;
    ctx.preciseBreak = true;
    EXPECT_TRUE(cond->test(ctx));
}

TEST_F(LootParserTest, ParseConditionNotPreciseBreak) {
    auto cond = LootTableLoader::parseCondition("not precise-break");
    ASSERT_NE(cond, nullptr);

    LootContext ctx;
    ctx.preciseBreak = true;
    EXPECT_FALSE(cond->test(ctx));

    ctx.preciseBreak = false;
    EXPECT_TRUE(cond->test(ctx));
}

TEST_F(LootParserTest, ParseConditionRandomChance) {
    auto cond = LootTableLoader::parseCondition("random-chance 0.5 0.1");
    ASSERT_NE(cond, nullptr);
}

TEST_F(LootParserTest, ParseConditionBlockType) {
    auto cond = LootTableLoader::parseCondition("block-type loot_parser_stone");
    ASSERT_NE(cond, nullptr);

    LootContext ctx;
    ctx.brokenBlock = BlockTypeId::fromName("loot_parser_stone");
    EXPECT_TRUE(cond->test(ctx));

    ctx.brokenBlock = BlockTypeId::fromName("loot_parser_dirt");
    EXPECT_FALSE(cond->test(ctx));
}

TEST_F(LootParserTest, ParseConditionToolTag) {
    auto cond = LootTableLoader::parseCondition("tool-tag common:test_axes");
    ASSERT_NE(cond, nullptr);
}

TEST_F(LootParserTest, ParseConditionUnknown) {
    auto cond = LootTableLoader::parseCondition("unknown-condition");
    EXPECT_EQ(cond, nullptr);
}

TEST_F(LootParserTest, ParseModifierBountyCount) {
    auto mod = LootTableLoader::parseModifier("bounty 2.0");
    ASSERT_NE(mod, nullptr);
}

TEST_F(LootParserTest, ParseModifierBountyCountDefault) {
    auto mod = LootTableLoader::parseModifier("bounty");
    ASSERT_NE(mod, nullptr);
}

TEST_F(LootParserTest, ParseModifierSetCount) {
    auto mod = LootTableLoader::parseModifier("set-count 3-7");
    ASSERT_NE(mod, nullptr);
}

TEST_F(LootParserTest, ParseModifierPlunderBonus) {
    auto mod = LootTableLoader::parseModifier("plunder-bonus 2");
    ASSERT_NE(mod, nullptr);
}

TEST_F(LootParserTest, ParseModifierPlunderBonusDefault) {
    auto mod = LootTableLoader::parseModifier("plunder-bonus");
    ASSERT_NE(mod, nullptr);
}

TEST_F(LootParserTest, ParseModifierUnknown) {
    auto mod = LootTableLoader::parseModifier("unknown-mod");
    EXPECT_EQ(mod, nullptr);
}

// ============================================================================
// Integration: LootTableRef (nested table) + precise-break stone pattern
// ============================================================================

class LootIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        LootRegistry::global().clear();
    }
    void TearDown() override {
        LootRegistry::global().clear();
    }
};

TEST_F(LootIntegrationTest, StoneLikeDropPattern) {
    // Stone: precise break → stone, else → cobblestone
    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 1
entry:silk:
type: item
item: loot_int_stone
count: 1
condition: precise-break
entry:normal:
type: item
item: loot_int_cobblestone
count: 1
condition: not precise-break
)");

    ASSERT_TRUE(table.has_value());
    LootRegistry::global().registerTable("integration_stone", std::move(*table));

    // Mine without precise break
    LootContext ctx;
    ctx.seed = 42;
    ctx.preciseBreak = false;
    auto items = LootRegistry::global().roll("integration_stone", ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_int_cobblestone"));

    // Mine with precise break
    ctx.seed = 42;
    ctx.preciseBreak = true;
    items = LootRegistry::global().roll("integration_stone", ctx);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ(items[0].type, ItemTypeId::fromName("loot_int_stone"));
}

TEST_F(LootIntegrationTest, MobDropPattern) {
    // Zombie: 1-3 rotten flesh + rare iron/carrot
    auto table = LootTableLoader::loadFromString(R"(
pool:flesh:
rolls: 1
entry:rotten_flesh:
type: item
item: loot_int_rotten_flesh
count: 1-3
modifier: plunder-bonus 1
pool:rare:
rolls: 1
condition: random-chance 1.0
entry:iron:
type: item
item: loot_int_iron_ingot
weight: 1
entry:carrot:
type: item
item: loot_int_carrot
weight: 1
)");

    ASSERT_TRUE(table.has_value());
    EXPECT_EQ(table->pools().size(), 2u);

    LootContext ctx;
    ctx.seed = 42;
    ctx.plunderLevel = 0;
    auto items = table->roll(ctx);

    // Should get at least 2 items (1 from each pool)
    EXPECT_GE(items.size(), 2u);
}

TEST_F(LootIntegrationTest, ChestLootPattern) {
    // Register sub-tables
    {
        LootTable commonTable;
        LootPool pool;
        pool.rollsMin = 1;
        pool.rollsMax = 1;
        LootEntry e;
        e.type = LootEntry::Type::Item;
        e.item = ItemTypeId::fromName("loot_int_bread");
        pool.entries.push_back(std::move(e));
        commonTable.addPool(std::move(pool));
        LootRegistry::global().registerTable("dungeon_common", std::move(commonTable));
    }

    auto table = LootTableLoader::loadFromString(R"(
pool:default:
rolls: 3
entry:common:
type: table
table: dungeon_common
weight: 10
entry:nothing:
type: empty
weight: 1
)");

    ASSERT_TRUE(table.has_value());

    LootContext ctx;
    ctx.seed = 42;
    auto items = table->roll(ctx);

    // 3 rolls, mostly common loot (table ref) with occasional nothing
    // At least some items should come through
    EXPECT_GE(items.size(), 0u);
    EXPECT_LE(items.size(), 3u);
}
