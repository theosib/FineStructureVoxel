#include <gtest/gtest.h>
#include "finevox/core/name_registry.hpp"
#include "finevox/core/item_type.hpp"
#include "finevox/core/item_stack.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/item_registry.hpp"
#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/entity.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/data_container.hpp"

using namespace finevox;

// ============================================================================
// NameRegistry Tests
// ============================================================================

class NameRegistryTest : public ::testing::Test {
protected:
    NameRegistry registry;
};

TEST_F(NameRegistryTest, StartsWithReservedEmptyId) {
    EXPECT_EQ(registry.size(), 1u);  // Just ID 0
    EXPECT_EQ(registry.getName(NameRegistry::EMPTY_ID), "");
}

TEST_F(NameRegistryTest, AssignsSequentialIds) {
    auto id1 = registry.getOrAssign("stone");
    auto id2 = registry.getOrAssign("dirt");
    auto id3 = registry.getOrAssign("grass");

    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(id2, 2u);
    EXPECT_EQ(id3, 3u);
    EXPECT_EQ(registry.size(), 4u);  // 0 + 3
}

TEST_F(NameRegistryTest, ReturnsSameIdForDuplicateName) {
    auto id1 = registry.getOrAssign("stone");
    auto id2 = registry.getOrAssign("stone");
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(registry.size(), 2u);  // 0 + 1
}

TEST_F(NameRegistryTest, LookupByName) {
    registry.getOrAssign("stone");
    auto found = registry.find("stone");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), 1u);

    auto notFound = registry.find("unknown");
    EXPECT_FALSE(notFound.has_value());
}

TEST_F(NameRegistryTest, LookupByIdRoundTrip) {
    auto id = registry.getOrAssign("diamond_pickaxe");
    auto name = registry.getName(id);
    EXPECT_EQ(name, "diamond_pickaxe");
}

TEST_F(NameRegistryTest, UnknownIdReturnsEmpty) {
    auto name = registry.getName(999);
    EXPECT_TRUE(name.empty());
}

TEST_F(NameRegistryTest, SerializationRoundTrip) {
    registry.getOrAssign("stone");
    registry.getOrAssign("dirt");
    registry.getOrAssign("oak_log");

    // Save
    DataContainer dc;
    registry.saveTo(dc, "names");

    // Load into new registry
    NameRegistry loaded = NameRegistry::loadFrom(dc, "names");

    EXPECT_EQ(loaded.size(), registry.size());

    // Verify same IDs for same names
    EXPECT_EQ(loaded.find("stone").value(), 1u);
    EXPECT_EQ(loaded.find("dirt").value(), 2u);
    EXPECT_EQ(loaded.find("oak_log").value(), 3u);
    EXPECT_EQ(loaded.getName(1), "stone");
    EXPECT_EQ(loaded.getName(2), "dirt");
    EXPECT_EQ(loaded.getName(3), "oak_log");
}

TEST_F(NameRegistryTest, LoadFromMissingKey) {
    DataContainer dc;
    NameRegistry loaded = NameRegistry::loadFrom(dc, "nonexistent");
    EXPECT_EQ(loaded.size(), 1u);  // Just reserved ID 0
}

TEST_F(NameRegistryTest, IdsStableAfterSaveLoad) {
    auto id1 = registry.getOrAssign("alpha");
    auto id2 = registry.getOrAssign("beta");

    DataContainer dc;
    registry.saveTo(dc, "reg");
    NameRegistry loaded = NameRegistry::loadFrom(dc, "reg");

    // New assignments after load continue from where we left off
    auto id3 = loaded.getOrAssign("gamma");
    EXPECT_GT(id3, id2);

    // Old names still map to same IDs
    EXPECT_EQ(loaded.getOrAssign("alpha"), id1);
    EXPECT_EQ(loaded.getOrAssign("beta"), id2);
}

TEST_F(NameRegistryTest, MoveConstruction) {
    registry.getOrAssign("test");
    NameRegistry moved(std::move(registry));
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_EQ(moved.getName(1), "test");
}

// ============================================================================
// ItemTypeId Tests
// ============================================================================

TEST(ItemTypeIdTest, DefaultIsEmpty) {
    ItemTypeId id;
    EXPECT_TRUE(id.isEmpty());
    EXPECT_FALSE(id.isValid());
    EXPECT_EQ(id, EMPTY_ITEM_TYPE);
}

TEST(ItemTypeIdTest, FromNameRoundTrip) {
    auto id = ItemTypeId::fromName("iron_sword");
    EXPECT_FALSE(id.isEmpty());
    EXPECT_TRUE(id.isValid());
    EXPECT_EQ(id.name(), "iron_sword");
}

TEST(ItemTypeIdTest, SameNameSameId) {
    auto id1 = ItemTypeId::fromName("diamond");
    auto id2 = ItemTypeId::fromName("diamond");
    EXPECT_EQ(id1, id2);
}

TEST(ItemTypeIdTest, DifferentNameDifferentId) {
    auto id1 = ItemTypeId::fromName("gold_ingot");
    auto id2 = ItemTypeId::fromName("iron_ingot");
    EXPECT_NE(id1, id2);
}

TEST(ItemTypeIdTest, Hashable) {
    auto id = ItemTypeId::fromName("test_hash_item");
    std::hash<ItemTypeId> hasher;
    auto h = hasher(id);
    (void)h;  // Just verify it compiles and doesn't crash
}

// ============================================================================
// ItemStack Tests
// ============================================================================

TEST(ItemStackTest, DefaultIsEmpty) {
    ItemStack stack;
    EXPECT_TRUE(stack.isEmpty());
    EXPECT_EQ(stack.count, 0);
    EXPECT_TRUE(stack.type.isEmpty());
}

TEST(ItemStackTest, NonEmptyStack) {
    ItemStack stack;
    stack.type = ItemTypeId::fromName("cobblestone");
    stack.count = 32;
    EXPECT_FALSE(stack.isEmpty());
}

TEST(ItemStackTest, ClearStack) {
    ItemStack stack;
    stack.type = ItemTypeId::fromName("cobblestone");
    stack.count = 32;
    stack.durability = 10;

    stack.clear();
    EXPECT_TRUE(stack.isEmpty());
    EXPECT_EQ(stack.durability, 0);
    EXPECT_EQ(stack.metadata, nullptr);
}

TEST(ItemStackTest, CanStackWithSameType) {
    ItemStack a;
    a.type = ItemTypeId::fromName("stone");
    a.count = 10;

    ItemStack b;
    b.type = ItemTypeId::fromName("stone");
    b.count = 5;

    EXPECT_TRUE(a.canStackWith(b));
}

TEST(ItemStackTest, CannotStackWithDifferentType) {
    ItemStack a;
    a.type = ItemTypeId::fromName("stone");
    a.count = 10;

    ItemStack b;
    b.type = ItemTypeId::fromName("dirt");
    b.count = 5;

    EXPECT_FALSE(a.canStackWith(b));
}

TEST(ItemStackTest, CannotStackWithDurability) {
    ItemStack a;
    a.type = ItemTypeId::fromName("iron_pick");
    a.count = 1;
    a.durability = 50;

    ItemStack b;
    b.type = ItemTypeId::fromName("iron_pick");
    b.count = 1;

    EXPECT_FALSE(a.canStackWith(b));
}

TEST(ItemStackTest, CannotStackWithMetadata) {
    ItemStack a;
    a.type = ItemTypeId::fromName("enchanted_book");
    a.count = 1;
    a.metadata = std::make_unique<DataContainer>();

    ItemStack b;
    b.type = ItemTypeId::fromName("enchanted_book");
    b.count = 1;

    EXPECT_FALSE(a.canStackWith(b));
}

TEST(ItemStackTest, BothEmptyCanStack) {
    ItemStack a;
    ItemStack b;
    EXPECT_TRUE(a.canStackWith(b));
}

TEST(ItemStackTest, SplitPartial) {
    ItemStack stack;
    stack.type = ItemTypeId::fromName("arrow");
    stack.count = 64;

    auto split = stack.split(16);

    EXPECT_EQ(split.count, 16);
    EXPECT_EQ(split.type.name(), "arrow");
    EXPECT_EQ(stack.count, 48);
    EXPECT_FALSE(stack.isEmpty());
}

TEST(ItemStackTest, SplitAll) {
    ItemStack stack;
    stack.type = ItemTypeId::fromName("arrow");
    stack.count = 10;

    auto split = stack.split(20);  // More than available

    EXPECT_EQ(split.count, 10);
    EXPECT_TRUE(stack.isEmpty());
}

TEST(ItemStackTest, SplitFromEmpty) {
    ItemStack stack;
    auto split = stack.split(5);
    EXPECT_TRUE(split.isEmpty());
}

TEST(ItemStackTest, Clone) {
    ItemStack original;
    original.type = ItemTypeId::fromName("golden_apple");
    original.count = 3;
    original.durability = 0;
    original.metadata = std::make_unique<DataContainer>();
    original.metadata->set("enchant", std::string("luck"));

    auto copy = original.clone();
    EXPECT_EQ(copy.type, original.type);
    EXPECT_EQ(copy.count, original.count);
    EXPECT_NE(copy.metadata, nullptr);
    // Verify deep copy — modifying one doesn't affect the other
    copy.count = 99;
    EXPECT_EQ(original.count, 3);
}

// ============================================================================
// InventoryView Tests
// ============================================================================

class InventoryViewTest : public ::testing::Test {
protected:
    DataContainer dc;
    NameRegistry registry;

    void SetUp() override {
        // Register some items for testing
        ItemType stone;
        stone.id = ItemTypeId::fromName("inv_stone");
        stone.maxStackSize = 64;
        ItemRegistry::global().registerType(stone);

        ItemType sword;
        sword.id = ItemTypeId::fromName("inv_sword");
        sword.maxStackSize = 1;
        sword.maxDurability = 100;
        ItemRegistry::global().registerType(sword);
    }

    InventoryView makeView() {
        return InventoryView(dc, registry);
    }
};

TEST_F(InventoryViewTest, EmptyInventory) {
    auto view = makeView();
    view.setSlotCount(9);
    EXPECT_EQ(view.slotCount(), 9);
    EXPECT_TRUE(view.isEmpty());
    EXPECT_FALSE(view.isFull());
}

TEST_F(InventoryViewTest, SetAndGetSlot) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 32;
    view.setSlot(0, stack);

    auto retrieved = view.getSlot(0);
    EXPECT_EQ(retrieved.type.name(), "inv_stone");
    EXPECT_EQ(retrieved.count, 32);
}

TEST_F(InventoryViewTest, EmptySlotReturnsEmptyStack) {
    auto view = makeView();
    view.setSlotCount(9);

    auto retrieved = view.getSlot(3);
    EXPECT_TRUE(retrieved.isEmpty());
}

TEST_F(InventoryViewTest, ClearSlot) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 10;
    view.setSlot(0, stack);

    view.clearSlot(0);
    EXPECT_TRUE(view.getSlot(0).isEmpty());
}

TEST_F(InventoryViewTest, SetEmptyStackClearsSlot) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 10;
    view.setSlot(0, stack);

    view.setSlot(0, ItemStack{});
    EXPECT_TRUE(view.getSlot(0).isEmpty());
}

TEST_F(InventoryViewTest, AddItemToEmptyInventory) {
    auto view = makeView();
    view.setSlotCount(3);

    int32_t remaining = view.addItem(ItemTypeId::fromName("inv_stone"), 32);
    EXPECT_EQ(remaining, 0);

    auto slot = view.getSlot(0);
    EXPECT_EQ(slot.type.name(), "inv_stone");
    EXPECT_EQ(slot.count, 32);
}

TEST_F(InventoryViewTest, AddItemFillsExistingFirst) {
    auto view = makeView();
    view.setSlotCount(3);

    // Put 32 stone in slot 0
    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 32;
    view.setSlot(0, stack);

    // Add 16 more — should fill slot 0 first
    int32_t remaining = view.addItem(ItemTypeId::fromName("inv_stone"), 16);
    EXPECT_EQ(remaining, 0);
    EXPECT_EQ(view.getSlot(0).count, 48);
}

TEST_F(InventoryViewTest, AddItemOverflowToNextSlot) {
    auto view = makeView();
    view.setSlotCount(3);

    // Put 60 stone in slot 0
    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 60;
    view.setSlot(0, stack);

    // Add 20 more — 4 fit in slot 0 (64 max), 16 go to slot 1
    int32_t remaining = view.addItem(ItemTypeId::fromName("inv_stone"), 20);
    EXPECT_EQ(remaining, 0);
    EXPECT_EQ(view.getSlot(0).count, 64);
    EXPECT_EQ(view.getSlot(1).count, 16);
}

TEST_F(InventoryViewTest, AddItemFullInventoryReturnsLeftover) {
    auto view = makeView();
    view.setSlotCount(1);

    // Fill the only slot
    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 64;
    view.setSlot(0, stack);

    // Try to add more
    int32_t remaining = view.addItem(ItemTypeId::fromName("inv_stone"), 10);
    EXPECT_EQ(remaining, 10);
}

TEST_F(InventoryViewTest, TakeItem) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 32;
    view.setSlot(0, stack);

    auto taken = view.takeItem(0, 10);
    EXPECT_EQ(taken.count, 10);
    EXPECT_EQ(taken.type.name(), "inv_stone");
    EXPECT_EQ(view.getSlot(0).count, 22);
}

TEST_F(InventoryViewTest, TakeAllItems) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 10;
    view.setSlot(0, stack);

    auto taken = view.takeItem(0, 100);  // More than available
    EXPECT_EQ(taken.count, 10);
    EXPECT_TRUE(view.getSlot(0).isEmpty());
}

TEST_F(InventoryViewTest, SwapSlots) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack stoneStack;
    stoneStack.type = ItemTypeId::fromName("inv_stone");
    stoneStack.count = 32;
    view.setSlot(0, stoneStack);

    ItemStack swordStack;
    swordStack.type = ItemTypeId::fromName("inv_sword");
    swordStack.count = 1;
    view.setSlot(1, swordStack);

    view.swapSlots(0, 1);

    EXPECT_EQ(view.getSlot(0).type.name(), "inv_sword");
    EXPECT_EQ(view.getSlot(0).count, 1);
    EXPECT_EQ(view.getSlot(1).type.name(), "inv_stone");
    EXPECT_EQ(view.getSlot(1).count, 32);
}

TEST_F(InventoryViewTest, CountItem) {
    auto view = makeView();
    view.setSlotCount(3);

    ItemStack s1;
    s1.type = ItemTypeId::fromName("inv_stone");
    s1.count = 32;
    view.setSlot(0, s1);

    ItemStack s2;
    s2.type = ItemTypeId::fromName("inv_stone");
    s2.count = 16;
    view.setSlot(2, s2);

    EXPECT_EQ(view.countItem(ItemTypeId::fromName("inv_stone")), 48);
    EXPECT_EQ(view.countItem(ItemTypeId::fromName("inv_sword")), 0);
}

TEST_F(InventoryViewTest, HasItem) {
    auto view = makeView();
    view.setSlotCount(3);

    ItemStack s;
    s.type = ItemTypeId::fromName("inv_stone");
    s.count = 10;
    view.setSlot(0, s);

    EXPECT_TRUE(view.hasItem(ItemTypeId::fromName("inv_stone"), 10));
    EXPECT_TRUE(view.hasItem(ItemTypeId::fromName("inv_stone"), 1));
    EXPECT_FALSE(view.hasItem(ItemTypeId::fromName("inv_stone"), 11));
    EXPECT_FALSE(view.hasItem(ItemTypeId::fromName("inv_sword")));
}

TEST_F(InventoryViewTest, RemoveItem) {
    auto view = makeView();
    view.setSlotCount(3);

    ItemStack s1;
    s1.type = ItemTypeId::fromName("inv_stone");
    s1.count = 32;
    view.setSlot(0, s1);

    ItemStack s2;
    s2.type = ItemTypeId::fromName("inv_stone");
    s2.count = 16;
    view.setSlot(2, s2);

    int32_t removed = view.removeItem(ItemTypeId::fromName("inv_stone"), 40);
    EXPECT_EQ(removed, 40);
    EXPECT_EQ(view.countItem(ItemTypeId::fromName("inv_stone")), 8);
}

TEST_F(InventoryViewTest, RemoveMoreThanAvailable) {
    auto view = makeView();
    view.setSlotCount(1);

    ItemStack s;
    s.type = ItemTypeId::fromName("inv_stone");
    s.count = 5;
    view.setSlot(0, s);

    int32_t removed = view.removeItem(ItemTypeId::fromName("inv_stone"), 100);
    EXPECT_EQ(removed, 5);
    EXPECT_TRUE(view.isEmpty());
}

TEST_F(InventoryViewTest, IsFullCheck) {
    auto view = makeView();
    view.setSlotCount(2);

    ItemStack s1;
    s1.type = ItemTypeId::fromName("inv_stone");
    s1.count = 64;
    view.setSlot(0, s1);

    EXPECT_FALSE(view.isFull());  // Slot 1 still empty

    ItemStack s2;
    s2.type = ItemTypeId::fromName("inv_stone");
    s2.count = 64;
    view.setSlot(1, s2);

    EXPECT_TRUE(view.isFull());
}

TEST_F(InventoryViewTest, SwordMaxStackSizeOne) {
    auto view = makeView();
    view.setSlotCount(3);

    // Swords can't stack beyond 1
    int32_t remaining = view.addItem(ItemTypeId::fromName("inv_sword"), 3);
    EXPECT_EQ(remaining, 0);

    // Should be spread across 3 slots
    EXPECT_EQ(view.getSlot(0).count, 1);
    EXPECT_EQ(view.getSlot(1).count, 1);
    EXPECT_EQ(view.getSlot(2).count, 1);
}

TEST_F(InventoryViewTest, DurabilityPreserved) {
    auto view = makeView();
    view.setSlotCount(9);

    ItemStack sword;
    sword.type = ItemTypeId::fromName("inv_sword");
    sword.count = 1;
    sword.durability = 42;
    view.setSlot(0, sword);

    auto retrieved = view.getSlot(0);
    EXPECT_EQ(retrieved.durability, 42);
}

TEST_F(InventoryViewTest, ViewIsEphemeral) {
    // Modifications via one view are visible via another view on the same DC
    auto view1 = makeView();
    view1.setSlotCount(9);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("inv_stone");
    stack.count = 10;
    view1.setSlot(0, stack);

    // Create a second view on the same DC
    auto view2 = makeView();
    auto retrieved = view2.getSlot(0);
    EXPECT_EQ(retrieved.count, 10);
    EXPECT_EQ(retrieved.type.name(), "inv_stone");
}

// ============================================================================
// ItemRegistry Tests
// ============================================================================

TEST(ItemRegistryTest, RegisterAndLookupById) {
    ItemType type;
    type.id = ItemTypeId::fromName("test_item_reg_1");
    type.maxStackSize = 16;
    type.attackDamage = 5.0f;

    bool registered = ItemRegistry::global().registerType(type);
    EXPECT_TRUE(registered);

    const auto* retrieved = ItemRegistry::global().getType(type.id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->maxStackSize, 16);
    EXPECT_FLOAT_EQ(retrieved->attackDamage, 5.0f);
}

TEST(ItemRegistryTest, RegisterAndLookupByName) {
    ItemType type;
    type.id = ItemTypeId::fromName("test_item_reg_2");
    type.maxStackSize = 32;
    ItemRegistry::global().registerType(type);

    const auto* retrieved = ItemRegistry::global().getType("test_item_reg_2");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->maxStackSize, 32);
}

TEST(ItemRegistryTest, RegisterByNameConvenience) {
    bool registered = ItemRegistry::global().registerType("test_item_reg_3");
    EXPECT_TRUE(registered);

    EXPECT_TRUE(ItemRegistry::global().hasType("test_item_reg_3"));
}

TEST(ItemRegistryTest, DuplicateRegistrationFails) {
    ItemType type;
    type.id = ItemTypeId::fromName("test_item_dup");
    ItemRegistry::global().registerType(type);

    bool second = ItemRegistry::global().registerType(type);
    EXPECT_FALSE(second);
}

TEST(ItemRegistryTest, UnknownTypeReturnsNull) {
    EXPECT_EQ(ItemRegistry::global().getType("nonexistent_item_xyz"), nullptr);
}

// ============================================================================
// ItemDropEntity Tests
// ============================================================================

TEST(ItemDropEntityTest, Construction) {
    ItemStack item;
    item.type = ItemTypeId::fromName("diamond");
    item.count = 3;

    ItemDropEntity entity(42, std::move(item));

    EXPECT_EQ(entity.id(), 42u);
    EXPECT_EQ(entity.type(), EntityType::ItemDrop);
    EXPECT_EQ(entity.item().type.name(), "diamond");
    EXPECT_EQ(entity.item().count, 3);
    EXPECT_FLOAT_EQ(entity.age(), 0.0f);
    EXPECT_FALSE(entity.isPickupable());  // Pickup delay hasn't elapsed
}

TEST(ItemDropEntityTest, PickupDelay) {
    ItemStack item;
    item.type = ItemTypeId::fromName("coal");
    item.count = 1;

    ItemDropEntity entity(1, std::move(item));
    entity.setPickupDelay(1.0f);

    World world;
    entity.tick(0.5f, world);
    EXPECT_FALSE(entity.isPickupable());

    entity.tick(0.6f, world);
    EXPECT_TRUE(entity.isPickupable());
}

TEST(ItemDropEntityTest, DespawnAfterMaxAge) {
    ItemStack item;
    item.type = ItemTypeId::fromName("stick");
    item.count = 1;

    ItemDropEntity entity(1, std::move(item));
    entity.setMaxAge(2.0f);

    World world;
    entity.tick(1.0f, world);
    EXPECT_TRUE(entity.isAlive());

    entity.tick(1.5f, world);
    EXPECT_FALSE(entity.isAlive());  // Marked for removal
    EXPECT_TRUE(entity.isMarkedForRemoval());
}

TEST(ItemDropEntityTest, TakeItem) {
    ItemStack item;
    item.type = ItemTypeId::fromName("emerald");
    item.count = 5;

    ItemDropEntity entity(1, std::move(item));
    auto taken = entity.takeItem();

    EXPECT_EQ(taken.type.name(), "emerald");
    EXPECT_EQ(taken.count, 5);
    EXPECT_TRUE(entity.item().isEmpty());
}

TEST(ItemDropEntityTest, TypeName) {
    ItemDropEntity entity(1, ItemStack{});
    EXPECT_EQ(entity.typeName(), "ItemDrop");
}

TEST(ItemDropEntityTest, SmallBoundingBox) {
    ItemDropEntity entity(1, ItemStack{});
    auto he = entity.halfExtents();
    EXPECT_FLOAT_EQ(he.x, 0.125f);
    EXPECT_FLOAT_EQ(he.y, 0.125f);
    EXPECT_FLOAT_EQ(he.z, 0.125f);
}

// ============================================================================
// Entity DataContainer Tests
// ============================================================================

TEST(EntityDataTest, StartsNull) {
    Entity entity(1, EntityType::Player);
    EXPECT_EQ(entity.entityData(), nullptr);
}

TEST(EntityDataTest, GetOrCreateWorks) {
    Entity entity(1, EntityType::Player);
    auto& data = entity.getOrCreateEntityData();
    data.set("health", int64_t(20));

    EXPECT_NE(entity.entityData(), nullptr);
    EXPECT_EQ(entity.entityData()->get<int64_t>("health"), 20);
}

TEST(EntityDataTest, GetOrCreateIdempotent) {
    Entity entity(1, EntityType::Player);
    auto& data1 = entity.getOrCreateEntityData();
    data1.set("test", int64_t(42));

    auto& data2 = entity.getOrCreateEntityData();
    EXPECT_EQ(data2.get<int64_t>("test"), 42);
    EXPECT_EQ(&data1, &data2);
}

// ============================================================================
// World NameRegistry Tests
// ============================================================================

TEST(WorldNameRegistryTest, WorldHasNameRegistry) {
    World world;
    auto& reg = world.nameRegistry();
    auto id = reg.getOrAssign("test_block");
    EXPECT_EQ(id, 1u);
    EXPECT_EQ(reg.getName(id), "test_block");
}

// ============================================================================
// Integration: InventoryView with NameRegistry round-trip
// ============================================================================

TEST(InventoryIntegrationTest, SaveAndLoadViaNameRegistry) {
    DataContainer dc;
    NameRegistry registry;

    // Set up inventory
    {
        InventoryView view(dc, registry);
        view.setSlotCount(3);

        ItemStack stone;
        stone.type = ItemTypeId::fromName("round_trip_stone");
        stone.count = 32;
        view.setSlot(0, stone);

        ItemStack sword;
        sword.type = ItemTypeId::fromName("round_trip_sword");
        sword.count = 1;
        sword.durability = 75;
        view.setSlot(2, sword);
    }

    // Simulate save/load: the NameRegistry persists PersistentIds,
    // the DataContainer persists the inventory data using those IDs.
    // On load, a new InventoryView reads the same DC + registry.
    {
        InventoryView view(dc, registry);
        EXPECT_EQ(view.slotCount(), 3);

        auto slot0 = view.getSlot(0);
        EXPECT_EQ(slot0.type.name(), "round_trip_stone");
        EXPECT_EQ(slot0.count, 32);

        auto slot1 = view.getSlot(1);
        EXPECT_TRUE(slot1.isEmpty());

        auto slot2 = view.getSlot(2);
        EXPECT_EQ(slot2.type.name(), "round_trip_sword");
        EXPECT_EQ(slot2.count, 1);
        EXPECT_EQ(slot2.durability, 75);
    }
}

TEST(InventoryIntegrationTest, BlockInventoryPattern) {
    // Simulate a chest block storing inventory in its DataContainer
    DataContainer blockData;
    NameRegistry registry;

    // Place items in the chest
    {
        InventoryView inv(blockData, registry);
        inv.setSlotCount(27);
        inv.addItem(ItemTypeId::fromName("chest_stone"), 100);
    }

    // Break the chest — read the inventory back
    {
        InventoryView inv(blockData, registry);
        int32_t total = inv.countItem(ItemTypeId::fromName("chest_stone"));
        EXPECT_EQ(total, 100);
    }
}

TEST(InventoryIntegrationTest, EntityInventoryPattern) {
    // Simulate a player entity with inventory
    Entity player(1, EntityType::Player);
    NameRegistry registry;

    // Give player items
    {
        auto& data = player.getOrCreateEntityData();
        InventoryView inv(data, registry);
        inv.setSlotCount(36);
        inv.addItem(ItemTypeId::fromName("player_pickaxe"), 1);
        inv.addItem(ItemTypeId::fromName("player_stone"), 64);
    }

    // Query player inventory
    {
        auto* data = player.entityData();
        ASSERT_NE(data, nullptr);
        InventoryView inv(*data, registry);
        EXPECT_TRUE(inv.hasItem(ItemTypeId::fromName("player_pickaxe")));
        EXPECT_EQ(inv.countItem(ItemTypeId::fromName("player_stone")), 64);
    }
}
