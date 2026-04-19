#include <gtest/gtest.h>

#include "finevox/core/block_handler.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/item_stack.hpp"
#include "finevox/core/loot_conditions.hpp"
#include "finevox/core/loot_modifiers.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/loot_table.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/name_registry.hpp"
#include "finevox/core/world.hpp"
#include "finevox/script/event_value.hpp"

#include <finescript/value.h>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/// Unique per-test-run name suffix; every test registers loot tables and
/// block types in globals, so each test must pick fresh names to avoid
/// cross-test contamination (registries dedupe on first registration).
static int gUniqueCounter = 0;
std::string uniqueName(const char* base) {
    return std::string(base) + "_drop_pipeline_" + std::to_string(++gUniqueCounter);
}

/// Build a simple "drop self" loot table yielding `count` items of `itemName`.
LootTable makeSelfDropTable(std::string_view itemName, int32_t count = 1) {
    LootTable table;
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;
    LootEntry entry;
    entry.type = LootEntry::Type::Item;
    entry.item = ItemTypeId::fromName(itemName);
    entry.countMin = count;
    entry.countMax = count;
    pool.entries.push_back(std::move(entry));
    table.addPool(std::move(pool));
    return table;
}

/// Build a precise-break-aware loot table: precise → `preciseItem`, otherwise → `normalItem`.
LootTable makePreciseSplitTable(std::string_view preciseItem,
                                 std::string_view normalItem) {
    LootTable table;
    LootPool pool;
    pool.rollsMin = 1;
    pool.rollsMax = 1;

    {
        LootEntry e;
        e.type = LootEntry::Type::Item;
        e.item = ItemTypeId::fromName(preciseItem);
        e.countMin = 1;
        e.countMax = 1;
        e.conditions.push_back(std::make_unique<PreciseBreakCondition>());
        pool.entries.push_back(std::move(e));
    }
    {
        LootEntry e;
        e.type = LootEntry::Type::Item;
        e.item = ItemTypeId::fromName(normalItem);
        e.countMin = 1;
        e.countMax = 1;
        e.conditions.push_back(std::make_unique<InvertedCondition>(
            std::make_unique<PreciseBreakCondition>()));
        pool.entries.push_back(std::move(e));
    }
    table.addPool(std::move(pool));
    return table;
}

/// Register a block type with the given loot-table name and return its ID.
BlockTypeId registerBlockWithLoot(std::string_view blockName,
                                   std::string_view lootTableName) {
    auto id = BlockTypeId::fromName(blockName);
    auto& registry = BlockRegistry::global();
    if (!registry.hasType(id)) {
        BlockType bt;
        bt.setLootTable(LootTableId::fromName(lootTableName));
        registry.registerType(id, bt);
    }
    return id;
}

}  // namespace

// ============================================================================
// BlockType::lootTable + rollBlockLoot
// ============================================================================

class BlockLootTest : public ::testing::Test {
protected:
    std::string lootName = uniqueName("stone_table");
    std::string blockName;
    BlockTypeId blockId;

    void SetUp() override {
        blockName = uniqueName("block_stone");
        ASSERT_TRUE(LootRegistry::global().registerTable(
            lootName, makePreciseSplitTable("pipeline:stone", "pipeline:cobble")));
        blockId = registerBlockWithLoot(blockName, lootName);
    }
};

TEST_F(BlockLootTest, DefaultConditionDropsCobble) {
    LootContext ctx;
    ctx.brokenBlock = blockId;
    ctx.preciseBreak = false;

    auto drops = LootRegistry::global().roll(lootName, ctx);
    ASSERT_EQ(drops.size(), 1u);
    EXPECT_EQ(drops[0].type, ItemTypeId::fromName("pipeline:cobble"));
    EXPECT_EQ(drops[0].count, 1);
}

TEST_F(BlockLootTest, PreciseBreakDropsStone) {
    LootContext ctx;
    ctx.brokenBlock = blockId;
    ctx.preciseBreak = true;

    auto drops = LootRegistry::global().roll(lootName, ctx);
    ASSERT_EQ(drops.size(), 1u);
    EXPECT_EQ(drops[0].type, ItemTypeId::fromName("pipeline:stone"));
    EXPECT_EQ(drops[0].count, 1);
}

TEST(BlockLootBasic, BlockWithoutLootTableProducesNoDrops) {
    auto noLootBlock = uniqueName("bare_block");
    auto id = BlockTypeId::fromName(noLootBlock);
    BlockRegistry::global().registerType(id, BlockType{});

    const BlockType& bt = BlockRegistry::global().getType(id);
    EXPECT_TRUE(bt.lootTable().isEmpty());
}

// ============================================================================
// Integration: break block via GameSession → ItemDropEntity spawned
// ============================================================================

TEST(BlockDropIntegration, BreakSpawnsItemDropEntity) {
    auto tableName = uniqueName("sand_table");
    auto blockName = uniqueName("block_sand");
    auto itemName = std::string("pipeline:") + blockName;

    ASSERT_TRUE(LootRegistry::global().registerTable(
        tableName, makeSelfDropTable(itemName, 1)));
    auto blockId = registerBlockWithLoot(blockName, tableName);

    auto session = GameSession::createLocal();
    World& world = session->world();
    world.setBlock({4, 5, 6}, blockId);

    size_t entitiesBefore = session->entities().entityCount();

    ASSERT_TRUE(session->actions().breakBlock({4, 5, 6}));
    session->tick(0.0f);

    // Block is gone and the drop entity is in the world
    EXPECT_TRUE(world.getBlock({4, 5, 6}).isAir());
    EXPECT_EQ(session->entities().entityCount(), entitiesBefore + 1);

    // Verify the drop is an ItemDropEntity with the expected stack
    bool foundDrop = false;
    for (const auto& [id, entity] : session->entities().entities()) {
        if (auto* drop = dynamic_cast<ItemDropEntity*>(entity.get())) {
            EXPECT_EQ(drop->item().type, ItemTypeId::fromName(itemName));
            EXPECT_EQ(drop->item().count, 1);
            foundDrop = true;
        }
    }
    EXPECT_TRUE(foundDrop);
}

TEST(BlockDropIntegration, BreakAirProducesNoDrop) {
    auto session = GameSession::createLocal();
    size_t entitiesBefore = session->entities().entityCount();
    (void)session->actions().breakBlock({100, 100, 100});
    session->tick(0.0f);
    EXPECT_EQ(session->entities().entityCount(), entitiesBefore);
}

TEST(BlockDropIntegration, BlockWithoutLootTableBreaksButYieldsNothing) {
    auto blockName = uniqueName("unlooted");
    auto id = BlockTypeId::fromName(blockName);
    BlockRegistry::global().registerType(id, BlockType{});  // no loot

    auto session = GameSession::createLocal();
    session->world().setBlock({0, 0, 0}, id);

    size_t entitiesBefore = session->entities().entityCount();
    ASSERT_TRUE(session->actions().breakBlock({0, 0, 0}));
    session->tick(0.0f);

    EXPECT_TRUE(session->world().getBlock({0, 0, 0}).isAir());
    EXPECT_EQ(session->entities().entityCount(), entitiesBefore);
}

TEST(BlockDropIntegration, PreciseBreakFlagRoutesThroughEventValue) {
    auto tableName = uniqueName("precise_split");
    auto blockName = uniqueName("hard_block");
    auto preciseName = std::string("pipeline:") + blockName + "_precise";
    auto normalName = std::string("pipeline:") + blockName + "_normal";

    ASSERT_TRUE(LootRegistry::global().registerTable(
        tableName, makePreciseSplitTable(preciseName, normalName)));
    auto blockId = registerBlockWithLoot(blockName, tableName);

    auto session = GameSession::createLocal();
    session->world().setBlock({0, 0, 0}, blockId);

    // Bypass LocalGameActions::breakBlock and push the Value directly so we
    // can set precise_break=true (LocalGameActions doesn't wire tools yet).
    session->actions().sendAction(
        makeBlockBrokenValue({0, 0, 0}, blockId, ItemTypeId{}, /*precise=*/true));
    session->tick(0.0f);

    bool sawPrecise = false;
    for (const auto& [id, e] : session->entities().entities()) {
        if (auto* drop = dynamic_cast<ItemDropEntity*>(e.get())) {
            EXPECT_EQ(drop->item().type, ItemTypeId::fromName(preciseName));
            sawPrecise = true;
        }
    }
    EXPECT_TRUE(sawPrecise);
}

// ============================================================================
// ItemDropEntity pickup + magnetism
// ============================================================================

/// Build a minimal test harness with its own EntityManager + player + callback.
/// Deliberately does NOT go through GameSession::tick so each tick applies only
/// the ItemDropEntity logic — no gravity/physics skews the velocity assertions.
struct PickupHarness {
    World world;
    GraphicsEventQueue graphicsQueue;
    EntityManager manager;
    EntityId playerId = INVALID_ENTITY_ID;
    int pickupCalls = 0;

    explicit PickupHarness(int32_t bagCapacity = 36)
        : manager(world, graphicsQueue)
    {
        playerId = manager.spawnPlayer(Vec3(0.0f, 0.0f, 0.0f));
        manager.setLocalPlayerId(playerId);

        bag = std::make_unique<DataContainer>();
        registry = std::make_unique<NameRegistry>();
        {
            InventoryView inv(*bag, *registry);
            inv.setSlotCount(bagCapacity);
        }

        manager.setItemPickupCallback(
            [this](EntityId pid, ItemStack& stack) -> bool {
                ++pickupCalls;
                EXPECT_EQ(pid, playerId);
                InventoryView inv(*bag, *registry);
                int32_t leftover = inv.addItem(stack.type, stack.count);
                if (leftover == stack.count) return false;
                if (leftover <= 0) stack.clear();
                else stack.count = leftover;
                return true;
            });
    }

    void setPlayerAt(const Vec3& pos) {
        auto* p = manager.getEntity(playerId);
        ASSERT_NE(p, nullptr);
        p->setPosition(pos);
        manager.spatialIndex().update(playerId, pos);
    }

    /// Spawn a drop and return a raw pointer (stays valid until removed)
    ItemDropEntity* spawnDrop(ItemStack stack, const Vec3& pos) {
        auto entity = std::make_unique<ItemDropEntity>(INVALID_ENTITY_ID, std::move(stack));
        entity->setPosition(pos);
        entity->setHasGravity(false);  // tests: isolate magnetism from gravity
        entity->setVelocity(Vec3(0.0f, 0.0f, 0.0f));
        entity->setPickupDelay(0.0f);  // let pickup fire immediately
        auto* raw = entity.get();
        manager.spawnEntity(std::move(entity));
        return raw;
    }

    std::unique_ptr<DataContainer> bag;
    std::unique_ptr<NameRegistry> registry;
};

TEST(ItemDropPickup, PickupWithRoomMovesItemAndRemovesEntity) {
    PickupHarness h;
    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:pickup_coal");
    stack.count = 3;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(0.5f, 0.0f, 0.0f));

    drop->tick(1.0f / 30.0f, h.world);

    EXPECT_TRUE(drop->isMarkedForRemoval());
    EXPECT_EQ(h.pickupCalls, 1);

    InventoryView inv(*h.bag, *h.registry);
    EXPECT_EQ(inv.countItem(ItemTypeId::fromName("pipeline:pickup_coal")), 3);
}

TEST(ItemDropPickup, PickupRespectsPickupDelay) {
    PickupHarness h;
    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:delay_coal");
    stack.count = 1;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(0.5f, 0.0f, 0.0f));
    drop->setPickupDelay(1.0f);

    // Before the delay elapses, no pickup should happen.
    drop->tick(0.3f, h.world);
    EXPECT_FALSE(drop->isMarkedForRemoval());
    EXPECT_EQ(h.pickupCalls, 0);

    // After crossing the delay threshold, pickup fires.
    drop->tick(0.8f, h.world);
    EXPECT_TRUE(drop->isMarkedForRemoval());
    EXPECT_EQ(h.pickupCalls, 1);
}

TEST(ItemDropPickup, FullInventoryLeavesDropInWorld) {
    PickupHarness h(/*bagCapacity=*/1);  // 1 slot
    // Fill the single slot with a different item so pickup has no home.
    {
        InventoryView inv(*h.bag, *h.registry);
        inv.addItem(ItemTypeId::fromName("pipeline:other"), 64);
        ASSERT_TRUE(inv.isFull());
    }

    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:cant_fit");
    stack.count = 1;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(0.5f, 0.0f, 0.0f));

    drop->tick(1.0f / 30.0f, h.world);

    EXPECT_FALSE(drop->isMarkedForRemoval());
    EXPECT_FALSE(drop->item().isEmpty());
    EXPECT_EQ(drop->item().count, 1);
}

TEST(ItemDropMagnetism, OutsideAttractRadiusLeavesVelocityUnchanged) {
    PickupHarness h;
    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:far_drop");
    stack.count = 1;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(10.0f, 0.0f, 0.0f));
    drop->setAttractRadius(3.0f);
    drop->setPickupRadius(1.25f);

    Vec3 before = drop->velocity();
    drop->tick(1.0f / 30.0f, h.world);
    Vec3 after = drop->velocity();

    EXPECT_FLOAT_EQ(after.x, before.x);
    EXPECT_FLOAT_EQ(after.y, before.y);
    EXPECT_FLOAT_EQ(after.z, before.z);
    EXPECT_FALSE(drop->isMarkedForRemoval());
    EXPECT_EQ(h.pickupCalls, 0);
}

TEST(ItemDropMagnetism, InsideAttractRadiusAcceleratesTowardPlayer) {
    PickupHarness h;
    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:attracted");
    stack.count = 1;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(2.0f, 0.0f, 0.0f));
    drop->setAttractRadius(3.0f);
    drop->setPickupRadius(1.25f);
    drop->setAttractAccel(10.0f);

    Vec3 before = drop->velocity();
    drop->tick(0.1f, h.world);
    Vec3 after = drop->velocity();

    // Player is at -x relative to the drop → expect negative x velocity.
    EXPECT_LT(after.x, before.x);
    EXPECT_FALSE(drop->isMarkedForRemoval());
    EXPECT_EQ(h.pickupCalls, 0);
}

TEST(ItemDropMagnetism, AttractSpeedIsClamped) {
    PickupHarness h;
    h.setPlayerAt(Vec3(0.0f, 0.0f, 0.0f));
    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:clamped");
    stack.count = 1;
    auto* drop = h.spawnDrop(std::move(stack), Vec3(2.9f, 0.0f, 0.0f));
    drop->setMaxAttractSpeed(5.0f);
    drop->setAttractAccel(10000.0f);  // absurdly high accel

    drop->tick(1.0f / 30.0f, h.world);
    Vec3 v = drop->velocity();
    float horiz = std::sqrt(v.x * v.x + v.z * v.z);
    EXPECT_LE(horiz, 5.0f + 1e-3f);
}

TEST(ItemDropPickup, WithoutCallbackDropLingers) {
    World world;
    GraphicsEventQueue gq;
    EntityManager manager(world, gq);
    EntityId playerId = manager.spawnPlayer(Vec3(0.0f, 0.0f, 0.0f));
    manager.setLocalPlayerId(playerId);

    ItemStack stack;
    stack.type = ItemTypeId::fromName("pipeline:unloved");
    stack.count = 1;
    auto entity = std::make_unique<ItemDropEntity>(INVALID_ENTITY_ID, std::move(stack));
    entity->setPosition(Vec3(0.5f, 0.0f, 0.0f));
    entity->setHasGravity(false);
    entity->setPickupDelay(0.0f);
    auto* drop = entity.get();
    manager.spawnEntity(std::move(entity));

    drop->tick(1.0f / 30.0f, world);

    // No callback → no pickup; drop stays alive.
    EXPECT_FALSE(drop->isMarkedForRemoval());
    EXPECT_FALSE(drop->item().isEmpty());
}
