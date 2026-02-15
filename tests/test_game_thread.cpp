#include <gtest/gtest.h>
#include "finevox/core/game_session.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/core/sound_event.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_state.hpp"

#include <thread>
#include <chrono>

using namespace finevox;
using namespace std::chrono_literals;

// ============================================================================
// Helper: register a test block type
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

// Helper: poll until a condition is true, with timeout
template<typename Pred>
bool pollUntil(Pred pred, std::chrono::milliseconds timeout = 500ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(1ms);
    }
    return pred();
}

// ============================================================================
// Start/Stop Lifecycle
// ============================================================================

TEST(GameThreadTest, StartStop) {
    auto session = GameSession::createLocal();
    EXPECT_FALSE(session->isGameThreadRunning());

    session->startGameThread();
    EXPECT_TRUE(session->isGameThreadRunning());

    session->stopGameThread();
    EXPECT_FALSE(session->isGameThreadRunning());
}

TEST(GameThreadTest, DoubleStartIsNoOp) {
    auto session = GameSession::createLocal();
    session->startGameThread();
    session->startGameThread();  // Should not crash or deadlock
    EXPECT_TRUE(session->isGameThreadRunning());
    session->stopGameThread();
}

TEST(GameThreadTest, DoubleStopIsNoOp) {
    auto session = GameSession::createLocal();
    session->startGameThread();
    session->stopGameThread();
    session->stopGameThread();  // Should not crash or deadlock
    EXPECT_FALSE(session->isGameThreadRunning());
}

TEST(GameThreadTest, DestructorStopsThread) {
    {
        auto session = GameSession::createLocal();
        session->startGameThread();
        EXPECT_TRUE(session->isGameThreadRunning());
        // Destructor should stop the game thread without deadlock
    }
    // If we get here, no deadlock
}

// ============================================================================
// Block mutations via game thread
// ============================================================================

TEST(GameThreadTest, BreakBlockViaGameThread) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone");

    // Set up state directly (bypasses event system)
    session->world().setBlock({0, 0, 0}, stone);
    ASSERT_EQ(session->world().getBlock({0, 0, 0}), stone);

    session->startGameThread();

    // Break block through actions (routed via command queue)
    session->actions().breakBlock({0, 0, 0});

    // Poll until block is broken
    bool broken = pollUntil([&]() {
        return session->world().getBlock({0, 0, 0}).isAir();
    });
    EXPECT_TRUE(broken) << "Block should have been broken by game thread";

    session->stopGameThread();
}

TEST(GameThreadTest, PlaceBlockViaGameThread) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone2");
    auto dirt = ensureTestBlock("gt_dirt");

    // Ensure chunk exists
    session->world().setBlock({0, 0, 0}, dirt);

    session->startGameThread();

    session->actions().placeBlock({0, 1, 0}, stone);

    bool placed = pollUntil([&]() {
        return session->world().getBlock({0, 1, 0}) == stone;
    });
    EXPECT_TRUE(placed) << "Block should have been placed by game thread";

    session->stopGameThread();
}

// ============================================================================
// Command processing is immediate (not waiting for tick)
// ============================================================================

TEST(GameThreadTest, CommandProcessedImmediately) {
    // Use 1 TPS to make ticks very infrequent
    GameSessionConfig config;
    config.tickRate = 1;

    auto session = GameSession::createLocal(config);
    auto stone = ensureTestBlock("gt_stone_imm");

    session->world().setBlock({5, 5, 5}, stone);

    session->startGameThread();

    session->actions().breakBlock({5, 5, 5});

    // Should be processed well before the 1-second tick interval
    bool broken = pollUntil([&]() {
        return session->world().getBlock({5, 5, 5}).isAir();
    }, 100ms);
    EXPECT_TRUE(broken) << "Command should be processed immediately, not waiting for tick";

    session->stopGameThread();
}

// ============================================================================
// Sound events are generated eagerly (on calling thread)
// ============================================================================

TEST(GameThreadTest, SoundEventsEager) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone_snd", true);

    session->world().setBlock({0, 0, 0}, stone);

    session->startGameThread();

    session->actions().breakBlock({0, 0, 0});

    // Sound should be available immediately (pushed on calling thread)
    auto events = session->soundEvents().drainAll();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].action, SoundAction::Break);

    session->stopGameThread();
}

// ============================================================================
// Ticks advance world time
// ============================================================================

TEST(GameThreadTest, TicksAdvanceWorldTime) {
    auto session = GameSession::createLocal();  // 20 TPS default

    int64_t ticksBefore = session->worldTime().totalTicks();

    session->startGameThread();

    // Wait enough time for several ticks (150ms = ~3 ticks at 20 TPS)
    std::this_thread::sleep_for(150ms);

    session->stopGameThread();

    int64_t ticksAfter = session->worldTime().totalTicks();
    EXPECT_GT(ticksAfter, ticksBefore) << "World time should have advanced";
}

// ============================================================================
// Entity snapshots published on tick
// ============================================================================

TEST(GameThreadTest, EntitySnapshotsPublished) {
    auto session = GameSession::createLocal();

    EntityId playerId = session->entities().spawnPlayer(Vec3(0, 64, 0));
    session->entities().setLocalPlayerId(playerId);

    session->startGameThread();

    // Wait for at least one tick
    std::this_thread::sleep_for(100ms);

    session->stopGameThread();

    // Drain graphics events — should have at least one entity snapshot
    auto events = session->graphicsEvents().drainAll();
    bool hasSnapshot = false;
    for (const auto& event : events) {
        if (event.type == GraphicsEventType::EntitySnapshot) {
            hasSnapshot = true;
            break;
        }
    }
    EXPECT_TRUE(hasSnapshot) << "Should have published entity snapshots";
}

// ============================================================================
// Multiple commands processed in order
// ============================================================================

TEST(GameThreadTest, MultipleCommandsInOrder) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone_order");
    auto dirt = ensureTestBlock("gt_dirt_order");

    // Ensure chunk loaded
    session->world().setBlock({0, 0, 0}, dirt);

    session->startGameThread();

    // Place then break — should end up as air
    session->actions().placeBlock({0, 1, 0}, stone);
    session->actions().breakBlock({0, 1, 0});

    bool isAir = pollUntil([&]() {
        return session->world().getBlock({0, 1, 0}).isAir();
    });
    EXPECT_TRUE(isAir) << "After place+break, block should be air";

    session->stopGameThread();
}

// ============================================================================
// Synchronous tick() backwards compatibility
// ============================================================================

TEST(GameThreadTest, SynchronousTickBackwardsCompat) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone_sync");
    auto dirt = ensureTestBlock("gt_dirt_sync");

    session->world().setBlock({0, 0, 0}, dirt);

    // Use tick() without starting game thread (old-style synchronous)
    session->actions().placeBlock({0, 1, 0}, stone);

    // Before tick: not visible yet (command is in queue)
    EXPECT_TRUE(session->world().getBlock({0, 1, 0}).isAir());

    session->tick(0.0f);

    // After tick: command processed
    EXPECT_EQ(session->world().getBlock({0, 1, 0}), stone);
}

// ============================================================================
// Graceful shutdown with pending commands
// ============================================================================

TEST(GameThreadTest, GracefulShutdownPendingCommands) {
    auto session = GameSession::createLocal();
    auto stone = ensureTestBlock("gt_stone_shutdown");

    session->world().setBlock({0, 0, 0}, stone);

    session->startGameThread();

    // Push a bunch of commands
    for (int i = 0; i < 10; ++i) {
        session->actions().breakBlock({0, 0, 0});
        session->actions().placeBlock({0, 0, 0}, stone);
    }

    // Stop should not deadlock even with pending commands
    session->stopGameThread();
    // If we get here, no deadlock
}

// ============================================================================
// Player state via sendPlayerState
// ============================================================================

TEST(GameThreadTest, SendPlayerState) {
    auto session = GameSession::createLocal();

    EntityId playerId = session->entities().spawnPlayer(Vec3(0, 64, 0));
    session->entities().setLocalPlayerId(playerId);

    session->startGameThread();

    EntityState state;
    state.position = glm::dvec3(10.0, 70.0, 20.0);
    state.velocity = glm::dvec3(0.0, 0.0, 0.0);  // Zero velocity to avoid physics drift
    state.onGround = true;
    state.yaw = 45.0f;
    state.pitch = -10.0f;
    state.inputSequence = 42;

    session->actions().sendPlayerState(playerId, state);

    // Wait for game thread to process
    std::this_thread::sleep_for(50ms);

    session->stopGameThread();

    // Verify player state was updated
    auto* player = session->entities().getEntity(playerId);
    ASSERT_NE(player, nullptr);

    // Position may have drifted slightly due to physics ticks (gravity),
    // but should be close to what we sent
    EXPECT_NEAR(player->position().x, 10.0f, 1.0f);
    EXPECT_NEAR(player->position().y, 70.0f, 1.0f);
    EXPECT_NEAR(player->position().z, 20.0f, 1.0f);
}
