#include <gtest/gtest.h>
#include "finevox/core/game_session.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_events.hpp"
#include "finevox/core/event_bus.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include "finevox/core/recipe_registry.hpp"
#include "finevox/core/inventory.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/io_manager.hpp"
#include "finevox/script/event_value.hpp"
#include <filesystem>

using namespace finevox;
using namespace finevox::script;

// ============================================================================
// Helper: register a test block type with sound
// ============================================================================
static BlockTypeId ensureTestBlock(const char* name, bool withSound = true) {
    auto id = BlockTypeId::fromName(name);
    auto& registry = BlockRegistry::global();
    if (!registry.hasType(id)) {
        BlockType bt;
        if (withSound) {
            bt.setSoundSet(SoundSetId::fromName(name));
        }
        registry.registerType(id, bt);
    }
    return id;
}

// ============================================================================
// Session creation
// ============================================================================

TEST(GameSessionTest, CreateLocal) {
    auto session = GameSession::createLocal();
    ASSERT_NE(session, nullptr);
}

TEST(GameSessionTest, SubsystemsAccessible) {
    auto session = GameSession::createLocal();
    // These should not throw or crash
    [[maybe_unused]] World& w = session->world();
    [[maybe_unused]] UpdateScheduler& s = session->scheduler();
    [[maybe_unused]] LightEngine& le = session->lightEngine();
    [[maybe_unused]] EntityManager& em = session->entities();
    [[maybe_unused]] WorldTime& wt = session->worldTime();
    [[maybe_unused]] SoundEventQueue& sq = session->soundEvents();
    [[maybe_unused]] GraphicsEventQueue& gq = session->graphicsEvents();
    [[maybe_unused]] GameActions& a = session->actions();
}

// ============================================================================
// Block mutations via actions()
// Note: World::placeBlock/breakBlock are event-driven — the actual block change
// happens in processEvents(). So we call tick() after actions to flush events.
// For setting up test state directly, use world.setBlock().
// ============================================================================

TEST(GameSessionTest, PlaceBlock) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");
    auto dirt = ensureTestBlock("test_dirt");

    // Ensure chunk exists by placing a block directly
    session->world().setBlock({0, 0, 0}, dirt);

    bool placed = session->actions().placeBlock({0, 1, 0}, stone);
    EXPECT_TRUE(placed);

    // Event-driven: block change happens in processEvents
    session->tick(0.0f);
    EXPECT_EQ(session->world().getBlock({0, 1, 0}), stone);
}

TEST(GameSessionTest, BreakBlock) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");

    // Use setBlock for direct placement (bypasses event system)
    session->world().setBlock({0, 0, 0}, stone);
    ASSERT_EQ(session->world().getBlock({0, 0, 0}), stone);

    bool broken = session->actions().breakBlock({0, 0, 0});
    EXPECT_TRUE(broken);

    session->tick(0.0f);
    EXPECT_TRUE(session->world().getBlock({0, 0, 0}).isAir());
}

TEST(GameSessionTest, BreakAirReturnsFalse) {
    auto session = GameSession::createLocal();

    // Breaking air should return false
    bool broken = session->actions().breakBlock({999, 999, 999});
    EXPECT_FALSE(broken);
}

// ============================================================================
// Sound events generated on block mutations
// ============================================================================

TEST(GameSessionTest, BreakBlockGeneratesSound) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone_snd", true);

    // Set up block directly
    session->world().setBlock({0, 0, 0}, stone);

    session->actions().breakBlock({0, 0, 0});

    auto events = session->soundEvents().drainAll();
    ASSERT_EQ(events.size(), 1u);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readString(events[0], s.action), "break");
    EXPECT_EQ(readString(events[0], s.sound_set), "test_stone_snd");
}

TEST(GameSessionTest, PlaceBlockGeneratesSound) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone_snd2", true);

    session->actions().placeBlock({0, 0, 0}, stone);

    auto events = session->soundEvents().drainAll();
    ASSERT_EQ(events.size(), 1u);
    const auto& s = EventSymbols::instance();
    EXPECT_EQ(readString(events[0], s.action), "place");
    EXPECT_EQ(readString(events[0], s.sound_set), "test_stone_snd2");
}

TEST(GameSessionTest, NoSoundWithoutSoundSet) {
    auto session = GameSession::createLocal();
    auto silent = ensureTestBlock("test_silent", false);

    session->actions().placeBlock({0, 0, 0}, silent);
    auto events = session->soundEvents().drainAll();
    EXPECT_TRUE(events.empty());

    // Place directly so breakBlock can find it
    session->tick(0.0f);
    session->actions().breakBlock({0, 0, 0});
    events = session->soundEvents().drainAll();
    EXPECT_TRUE(events.empty());
}

// ============================================================================
// Tick processing
// ============================================================================

TEST(GameSessionTest, TickAdvancesWorldTime) {
    auto session = GameSession::createLocal();

    int64_t ticksBefore = session->worldTime().totalTicks();
    session->tick(1.0f);  // 1 second at 20 TPS = 20 ticks
    int64_t ticksAfter = session->worldTime().totalTicks();

    EXPECT_GT(ticksAfter, ticksBefore);
}

TEST(GameSessionTest, TickProcessesEvents) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");

    // Place a block directly to set up state
    session->world().setBlock({5, 5, 5}, stone);

    // Push an external event
    session->scheduler().pushExternalEvent(BlockEvent::blockUpdate({5, 5, 5}));

    // Tick should process it without crashing
    session->tick(0.05f);
}

TEST(GameSessionTest, PlaceBlockThenTickMakesBlockAppear) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");
    auto dirt = ensureTestBlock("test_dirt");

    // Ensure the chunk is loaded by placing a block directly
    session->world().setBlock({10, 10, 10}, dirt);

    session->actions().placeBlock({10, 11, 10}, stone);
    // Before tick: block not yet visible
    EXPECT_TRUE(session->world().getBlock({10, 11, 10}).isAir());

    session->tick(0.0f);
    // After tick: block is placed
    EXPECT_EQ(session->world().getBlock({10, 11, 10}), stone);
}

// ============================================================================
// UseBlock / HitBlock route through event system
// ============================================================================

TEST(GameSessionTest, UseBlockOnAirReturnsFalse) {
    auto session = GameSession::createLocal();
    bool used = session->actions().useBlock({999, 999, 999}, Face::PosY);
    EXPECT_FALSE(used);
}

TEST(GameSessionTest, UseBlockOnBlockReturnsTrue) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");

    session->world().setBlock({0, 0, 0}, stone);
    bool used = session->actions().useBlock({0, 0, 0}, Face::PosY);
    EXPECT_TRUE(used);
}

TEST(GameSessionTest, HitBlockOnAirReturnsFalse) {
    auto session = GameSession::createLocal();
    bool hit = session->actions().hitBlock({999, 999, 999}, Face::PosY);
    EXPECT_FALSE(hit);
}

TEST(GameSessionTest, HitBlockOnBlockReturnsTrue) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone");

    session->world().setBlock({0, 0, 0}, stone);
    bool hit = session->actions().hitBlock({0, 0, 0}, Face::PosY);
    EXPECT_TRUE(hit);
}

// ============================================================================
// Config
// ============================================================================

TEST(GameSessionTest, CustomConfig) {
    GameSessionConfig config;
    config.tickRate = 10;
    config.gravity = -9.8f;
    config.randomTicksPerChunk = 12;
    config.enableLighting = false;
    config.enableSound = false;

    auto session = GameSession::createLocal(config);
    EXPECT_FLOAT_EQ(session->worldTime().ticksPerSecond(), 10.0f);
    EXPECT_FLOAT_EQ(session->entities().physics().gravity(), -9.8f);
    EXPECT_EQ(session->scheduler().tickConfig().randomTicksPerSubchunk, 12u);
    EXPECT_EQ(session->world().lightEngine(), nullptr);

    // With enableSound = false, placing a block with a soundSet does not emit sound events
    auto stone = ensureTestBlock("test_stone_muted", true);
    session->actions().placeBlock({0, 0, 0}, stone);
    EXPECT_TRUE(session->soundEvents().empty());
}

TEST(GameSessionTest, SendPlayerStatePreservesYawAndPitchAndPlaceBlockPreservesRotation) {
    auto session = GameSession::createLocal();
    EntityId playerId = session->entities().spawnPlayer(Vec3(0.0f, 64.0f, 0.0f));

    EntityState state;
    state.id = playerId;
    state.position = glm::dvec3(2.0, 64.0, 3.0);
    state.velocity = glm::dvec3(0.0, 0.0, 0.0);
    state.onGround = true;
    state.yaw = 1.25f;
    state.pitch = -0.45f;
    state.inputSequence = 7;

    session->actions().sendPlayerState(playerId, state);

    // Also send a rotated block placement command
    auto stairs = ensureTestBlock("test_rotated_stairs", false);
    session->world().setBlock({0, 0, 0}, stairs);
    session->actions().sendAction(
        makeBlockPlacedValue({1, 2, 3}, stairs, AIR_BLOCK_TYPE, Rotation::ROTATE_Y_90));

    session->tick(0.0f);

    const Entity* player = session->entities().getEntity(playerId);
    ASSERT_NE(player, nullptr);
    EXPECT_FLOAT_EQ(player->yaw(), 1.25f);
    EXPECT_FLOAT_EQ(player->pitch(), -0.45f);

    SubChunk* sc = session->world().getSubChunk(ChunkPos::fromBlock({1, 2, 3}));
    ASSERT_NE(sc, nullptr);
    EXPECT_EQ(sc->getBlock(1, 2, 3), stairs);
    EXPECT_EQ(sc->getRotation(1, 2, 3), Rotation::ROTATE_Y_90);
}

// ============================================================================
// Entity system accessible
// ============================================================================

TEST(GameSessionTest, EntitySystemWorks) {
    auto session = GameSession::createLocal();

    EntityId id = session->entities().spawnPlayer(Vec3(0, 64, 0));
    EXPECT_NE(id, INVALID_ENTITY_ID);

    session->entities().setLocalPlayerId(id);
    EXPECT_EQ(session->entities().localPlayerId(), id);
    EXPECT_NE(session->entities().getLocalPlayer(), nullptr);
}

// ============================================================================
// Phase R2: EventBus live gameplay event publishing & Crafting / Persistence
// ============================================================================

TEST(GameSessionTest, ActionsPublishTypedEventsOnEventBus) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("r2_evt_stone", false);

    FluidTypeId waterId = FluidTypeId::fromName("r2_evt_water");
    if (!FluidRegistry::global().hasType(waterId)) {
        FluidType ft;
        ft.name = "r2_evt_water";
        ft.id = waterId;
        FluidRegistry::global().registerType("r2_evt_water", ft);
    }

    int placedCount = 0;
    int brokenCount = 0;
    int fluidPlacedCount = 0;
    int fluidRemovedCount = 0;
    int useCount = 0;
    int hitCount = 0;
    int posCount = 0;
    int lookCount = 0;
    int jumpCount = 0;
    int sprintCount = 0;
    int sneakCount = 0;
    int timeCount = 0;
    int craftCount = 0;

    auto& bus = session->eventBus();
    bus.subscribe<BlockPlacedEvent>([&](const BlockPlacedEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{1, 2, 3}));
        EXPECT_EQ(e.newType, stone);
        ++placedCount;
    });
    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{1, 2, 3}));
        EXPECT_EQ(e.oldType, stone);
        ++brokenCount;
    });
    bus.subscribe<FluidPlacedEvent>([&](const FluidPlacedEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{2, 2, 2}));
        EXPECT_EQ(e.type, waterId);
        EXPECT_EQ(e.level, 15);
        ++fluidPlacedCount;
    });
    bus.subscribe<FluidRemovedEvent>([&](const FluidRemovedEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{2, 2, 2}));
        EXPECT_EQ(e.previousType, waterId);
        ++fluidRemovedCount;
    });
    bus.subscribe<PlayerUseEvent>([&](const PlayerUseEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{4, 4, 4}));
        EXPECT_EQ(e.face, Face::PosY);
        ++useCount;
    });
    bus.subscribe<PlayerHitEvent>([&](const PlayerHitEvent& e) {
        EXPECT_EQ(e.pos, (BlockCoord{4, 4, 4}));
        EXPECT_EQ(e.face, Face::NegX);
        ++hitCount;
    });
    bus.subscribe<PlayerPositionEvent>([&](const PlayerPositionEvent& e) {
        EXPECT_DOUBLE_EQ(e.state.position.x, 5.0);
        ++posCount;
    });
    bus.subscribe<PlayerLookEvent>([&](const PlayerLookEvent& e) {
        EXPECT_FLOAT_EQ(e.state.yaw, 0.75f);
        ++lookCount;
    });
    bus.subscribe<PlayerJumpEvent>([&](const PlayerJumpEvent&) { ++jumpCount; });
    bus.subscribe<PlayerSprintEvent>([&](const PlayerSprintEvent& e) {
        EXPECT_TRUE(e.starting);
        ++sprintCount;
    });
    bus.subscribe<PlayerSneakEvent>([&](const PlayerSneakEvent& e) {
        EXPECT_TRUE(e.starting);
        ++sneakCount;
    });
    bus.subscribe<SetWorldTimeEvent>([&](const SetWorldTimeEvent& e) {
        EXPECT_EQ(e.ticks, 12345);
        ++timeCount;
    });
    bus.subscribe<CraftItemEvent>([&](const CraftItemEvent& e) {
        EXPECT_EQ(e.stationPos, (BlockCoord{9, 9, 9}));
        ++craftCount;
    });

    EntityId playerId = session->entities().spawnPlayer(Vec3(0.0f, 64.0f, 0.0f));
    session->world().setBlock({4, 4, 4}, stone);

    // 1. Place block + fluid + use + hit + player actions + world time + craft
    session->actions().placeBlock({1, 2, 3}, stone);
    session->actions().placeFluid({2, 2, 2}, waterId, 15);
    session->actions().useBlock({4, 4, 4}, Face::PosY);
    session->actions().hitBlock({4, 4, 4}, Face::NegX);

    EntityState st;
    st.id = playerId;
    st.position = glm::dvec3(5.0, 64.0, 0.0);
    session->actions().sendPlayerState(playerId, st);
    session->actions().sendAction(makePlayerLookValue(playerId, 0.75f, -0.25f));
    session->actions().sendAction(makePlayerJumpValue(playerId));
    session->actions().sendAction(makePlayerSprintValue(playerId, true));
    session->actions().sendAction(makePlayerSneakValue(playerId, true));
    session->actions().setWorldTime(12345);
    session->actions().craftItem({9, 9, 9}, RecipeId::fromName("r2_nonexistent_recipe"));

    session->tick(0.0f);

    // 2. Now break the placed block and remove the placed fluid
    session->actions().breakBlock({1, 2, 3});
    session->actions().removeFluid({2, 2, 2});
    session->tick(0.0f);

    EXPECT_EQ(placedCount, 1);
    EXPECT_EQ(brokenCount, 1);
    EXPECT_EQ(fluidPlacedCount, 1);
    EXPECT_EQ(fluidRemovedCount, 1);
    EXPECT_EQ(useCount, 1);
    EXPECT_EQ(hitCount, 1);
    EXPECT_EQ(posCount, 1);
    EXPECT_EQ(lookCount, 1);
    EXPECT_EQ(jumpCount, 1);
    EXPECT_EQ(sprintCount, 1);
    EXPECT_EQ(sneakCount, 1);
    EXPECT_EQ(timeCount, 1);
    EXPECT_EQ(craftCount, 1);
}

TEST(GameSessionTest, CraftItemExecutesRecipeOnPlayerCraftingGrid) {
    auto session = GameSession::createLocal();
    EntityId playerId = session->entities().spawnPlayer(Vec3(0.0f, 64.0f, 0.0f));
    session->entities().setLocalPlayerId(playerId);

    // Register a simple shapeless recipe: 1 log -> 4 planks
    ItemTypeId logItem = ItemTypeId::fromName("r2_craft:log");
    ItemTypeId plankItem = ItemTypeId::fromName("r2_craft:plank");
    Recipe recipe;
    recipe.id = RecipeId::fromName("r2_craft:planks");
    recipe.type = RecipeType::Shapeless;
    recipe.station = EMPTY_STATION;
    recipe.ingredients = {ItemMatch::exact(logItem)};
    recipe.outputItem = plankItem;
    recipe.outputCount = 4;
    RecipeRegistry::global().registerRecipe("r2_craft:planks", recipe);

    // Attach a 2x2 crafting_grid and a bag to the player entity's DataContainer
    Entity* player = session->entities().getEntity(playerId);
    ASSERT_NE(player, nullptr);
    DataContainer& dc = player->getOrCreateEntityData();
    DataContainer& gridDc = dc.getOrCreateChild("crafting_grid");
    gridDc.set<int32_t>("width", 2);
    gridDc.set<int32_t>("height", 2);
    InventoryView gridView(gridDc, session->world().nameRegistry());
    gridView.setSlotCount(4);
    gridView.setSlot(0, ItemStack{logItem, 2});

    DataContainer& bagDc = dc.getOrCreateChild("bag");
    InventoryView bagView(bagDc, session->world().nameRegistry());
    bagView.setSlotCount(9);

    bool craftEventFired = false;
    bool craftSucceeded = false;
    session->eventBus().subscribe<CraftItemEvent>([&](const CraftItemEvent& e) {
        craftEventFired = true;
        craftSucceeded = e.crafted;
    });

    session->actions().craftItem({0, 0, 0}, recipe.id);
    session->tick(0.0f);

    EXPECT_TRUE(craftEventFired);
    EXPECT_TRUE(craftSucceeded);
    // 1 log consumed from slot 0 (leaving 1), 4 planks added to bag
    EXPECT_EQ(gridView.getSlot(0).count, 1);
    EXPECT_EQ(bagView.countItem(plankItem), 4);
}

TEST(GameSessionTest, ColumnManagerAndEntityPersistenceWiredInGameSession) {
    auto tempDir = std::filesystem::temp_directory_path() / "finevox_r2_session_persist";
    std::filesystem::remove_all(tempDir);

    BlockTypeId stone = ensureTestBlock("r2_persist_stone", false);

    {
        GameSessionConfig cfg;
        cfg.worldSaveDir = tempDir.string();
        auto session = GameSession::createLocal(cfg);
        ASSERT_NE(session->ioManager(), nullptr);

        // Spawn an arrow entity in column (0, 0)
        session->entities().spawnEntity(EntityType::Arrow, Vec3(5.0f, 64.0f, 5.0f));
        EXPECT_EQ(session->entities().entityCount(), 1u);

        // Create and populate column (0, 0) in ColumnManager
        auto col = std::make_unique<ChunkColumn>(ColumnPos{0, 0});
        col->setBlock(2, 10, 2, stone);
        session->columnManager().add(std::move(col));
        session->columnManager().addRef(ColumnPos{0, 0});
        session->columnManager().markDirty(ColumnPos{0, 0});
        session->columnManager().release(ColumnPos{0, 0});

        // Tick session to process ColumnLifecycleSubsystem -> saves column + entities
        session->tick(1.0f / 30.0f);
        session->ioManager()->flush();
    }

    // Reload column into a new session and verify both block and entity were persisted
    {
        GameSessionConfig cfg;
        cfg.worldSaveDir = tempDir.string();
        auto session2 = GameSession::createLocal(cfg);

        std::atomic<bool> loaded{false};
        ASSERT_TRUE(session2->columnManager().requestLoad(
            ColumnPos{0, 0},
            [&](ColumnPos, std::unique_ptr<ChunkColumn>) { loaded = true; }));

        while (!loaded.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        EXPECT_EQ(session2->world().getBlock(2, 10, 2), stone);
        EXPECT_EQ(session2->entities().entityCount(), 1u);
    }

    std::filesystem::remove_all(tempDir);
}

