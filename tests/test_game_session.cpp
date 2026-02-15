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

using namespace finevox;

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
    EXPECT_EQ(events[0].action, SoundAction::Break);
    EXPECT_EQ(events[0].soundSet, SoundSetId::fromName("test_stone_snd"));
}

TEST(GameSessionTest, PlaceBlockGeneratesSound) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("test_stone_snd2", true);

    session->actions().placeBlock({0, 0, 0}, stone);

    auto events = session->soundEvents().drainAll();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, SoundAction::Place);
    EXPECT_EQ(events[0].soundSet, SoundSetId::fromName("test_stone_snd2"));
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

    auto session = GameSession::createLocal(config);
    EXPECT_FLOAT_EQ(session->worldTime().ticksPerSecond(), 10.0f);
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
