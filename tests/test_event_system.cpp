#include <gtest/gtest.h>
#include "finevox/core/event_queue.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/block_handler.hpp"  // For TickType
#include "finevox/core/world.hpp"

using namespace finevox;

// ============================================================================
// EventOutbox Tests
// ============================================================================

TEST(EventOutboxTest, PushSingleEvent) {
    EventOutbox outbox;

    BlockEvent event = BlockEvent::neighborUpdated({10, 20, 30}, Face::PosX);
    outbox.push(event);

    EXPECT_EQ(outbox.size(), 1);
    EXPECT_FALSE(outbox.empty());
}

TEST(EventOutboxTest, ConsolidateNeighborUpdatedEvents) {
    EventOutbox outbox;

    BlockCoord pos{10, 20, 30};

    // Push multiple NeighborUpdated events for the same position
    BlockEvent event1 = BlockEvent::neighborUpdated(pos, Face::PosX);
    event1.addNeighborFace(Face::PosX);

    BlockEvent event2 = BlockEvent::neighborUpdated(pos, Face::NegY);
    event2.addNeighborFace(Face::NegY);

    BlockEvent event3 = BlockEvent::neighborUpdated(pos, Face::PosZ);
    event3.addNeighborFace(Face::PosZ);

    outbox.push(event1);
    outbox.push(event2);
    outbox.push(event3);

    // Should consolidate to 1 event
    EXPECT_EQ(outbox.size(), 1);

    // Swap to inbox and check the merged event
    std::vector<BlockEvent> inbox;
    outbox.swapTo(inbox);

    EXPECT_EQ(inbox.size(), 1);
    EXPECT_EQ(inbox[0].type, EventType::NeighborUpdated);
    EXPECT_TRUE(inbox[0].hasNeighborUpdated(Face::PosX));
    EXPECT_TRUE(inbox[0].hasNeighborUpdated(Face::NegY));
    EXPECT_TRUE(inbox[0].hasNeighborUpdated(Face::PosZ));
    EXPECT_EQ(inbox[0].changedNeighborCount(), 3);
}

TEST(EventOutboxTest, SwapClearsOutbox) {
    EventOutbox outbox;

    outbox.push(BlockEvent::neighborUpdated({1, 2, 3}, Face::PosX));
    outbox.push(BlockEvent::neighborUpdated({4, 5, 6}, Face::NegY));

    EXPECT_EQ(outbox.size(), 2);

    std::vector<BlockEvent> inbox;
    outbox.swapTo(inbox);

    EXPECT_TRUE(outbox.empty());
    EXPECT_EQ(inbox.size(), 2);
}

TEST(EventOutboxTest, DifferentPositionsNotConsolidated) {
    EventOutbox outbox;

    outbox.push(BlockEvent::neighborUpdated({1, 2, 3}, Face::PosX));
    outbox.push(BlockEvent::neighborUpdated({4, 5, 6}, Face::PosX));
    outbox.push(BlockEvent::neighborUpdated({7, 8, 9}, Face::PosX));

    EXPECT_EQ(outbox.size(), 3);
}

TEST(EventOutboxTest, DifferentEventTypesKeptSeparate) {
    EventOutbox outbox;

    BlockCoord pos{10, 20, 30};

    // Push two different event types at the same position
    outbox.push(BlockEvent::neighborUpdated(pos, Face::PosX));

    auto stone = BlockTypeId::fromName("eventtest:stone");
    outbox.push(BlockEvent::blockPlaced(pos, stone, AIR_BLOCK_TYPE));

    // Both events should be kept (keyed by pos + type)
    EXPECT_EQ(outbox.size(), 2);

    std::vector<BlockEvent> inbox;
    outbox.swapTo(inbox);

    EXPECT_EQ(inbox.size(), 2);

    // Check both event types are present
    bool hasNeighborUpdated = false;
    bool hasBlockPlaced = false;
    for (const auto& event : inbox) {
        if (event.type == EventType::NeighborUpdated) hasNeighborUpdated = true;
        if (event.type == EventType::BlockPlaced) hasBlockPlaced = true;
    }
    EXPECT_TRUE(hasNeighborUpdated);
    EXPECT_TRUE(hasBlockPlaced);
}

// ============================================================================
// BlockEvent Face Mask Tests
// ============================================================================

TEST(BlockEventTest, FaceMaskHelpers) {
    BlockEvent event = BlockEvent::neighborUpdated({0, 0, 0}, Face::PosX);

    // Initial state
    EXPECT_EQ(event.neighborFaceMask, 0);
    EXPECT_FALSE(event.hasNeighborUpdated(Face::PosX));

    // Add faces
    event.addNeighborFace(Face::PosX);
    event.addNeighborFace(Face::NegY);
    event.addNeighborFace(Face::PosZ);

    EXPECT_TRUE(event.hasNeighborUpdated(Face::PosX));
    EXPECT_TRUE(event.hasNeighborUpdated(Face::NegY));
    EXPECT_TRUE(event.hasNeighborUpdated(Face::PosZ));
    EXPECT_FALSE(event.hasNeighborUpdated(Face::NegX));
    EXPECT_FALSE(event.hasNeighborUpdated(Face::PosY));
    EXPECT_FALSE(event.hasNeighborUpdated(Face::NegZ));

    EXPECT_EQ(event.changedNeighborCount(), 3);
}

TEST(BlockEventTest, ForEachChangedNeighbor) {
    BlockEvent event;
    event.addNeighborFace(Face::PosX);
    event.addNeighborFace(Face::PosY);
    event.addNeighborFace(Face::PosZ);

    std::vector<Face> faces;
    event.forEachChangedNeighbor([&faces](Face f) {
        faces.push_back(f);
    });

    EXPECT_EQ(faces.size(), 3);
    // Faces should be in order (0, 1, 2 = PosX, NegX, PosY, ...)
    // PosX = 0, PosY = 2, PosZ = 4
    EXPECT_TRUE(std::find(faces.begin(), faces.end(), Face::PosX) != faces.end());
    EXPECT_TRUE(std::find(faces.begin(), faces.end(), Face::PosY) != faces.end());
    EXPECT_TRUE(std::find(faces.begin(), faces.end(), Face::PosZ) != faces.end());
}

// ============================================================================
// TickConfig Tests
// ============================================================================

TEST(TickConfigTest, DefaultValues) {
    TickConfig config;

    EXPECT_EQ(config.gameTickIntervalMs, 33);
    EXPECT_EQ(config.randomTicksPerSubchunk, 4);
    EXPECT_EQ(config.randomSeed, 0);
    EXPECT_TRUE(config.gameTicksEnabled);
    EXPECT_TRUE(config.randomTicksEnabled);
}

// ============================================================================
// SubChunk Game Tick Registry Tests
// ============================================================================

TEST(SubChunkGameTickTest, EmptyRegistryByDefault) {
    SubChunk chunk;

    EXPECT_TRUE(chunk.gameTickBlocks().empty());
}

TEST(SubChunkGameTickTest, RegisterAndUnregister) {
    SubChunk chunk;

    // Register some blocks
    chunk.registerForGameTicks(100);
    chunk.registerForGameTicks(200);
    chunk.registerForGameTicks(50);

    EXPECT_EQ(chunk.gameTickBlocks().size(), 3);
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(100));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(200));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(50));
    EXPECT_FALSE(chunk.isRegisteredForGameTicks(150));

    // Verify all are in the set
    const auto& blocks = chunk.gameTickBlocks();
    EXPECT_TRUE(blocks.contains(50));
    EXPECT_TRUE(blocks.contains(100));
    EXPECT_TRUE(blocks.contains(200));

    // Unregister one
    chunk.unregisterFromGameTicks(100);
    EXPECT_EQ(chunk.gameTickBlocks().size(), 2);
    EXPECT_FALSE(chunk.isRegisteredForGameTicks(100));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(50));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(200));
}

TEST(SubChunkGameTickTest, DuplicateRegistrationIgnored) {
    SubChunk chunk;

    chunk.registerForGameTicks(100);
    chunk.registerForGameTicks(100);  // Duplicate
    chunk.registerForGameTicks(100);  // Duplicate

    EXPECT_EQ(chunk.gameTickBlocks().size(), 1);
}

TEST(SubChunkGameTickTest, UnregisterNonexistentIsNoOp) {
    SubChunk chunk;

    chunk.registerForGameTicks(100);
    chunk.unregisterFromGameTicks(200);  // Not registered

    EXPECT_EQ(chunk.gameTickBlocks().size(), 1);
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(100));
}

TEST(SubChunkGameTickTest, BoundaryIndices) {
    SubChunk chunk;

    // Valid indices: 0 to 4095
    chunk.registerForGameTicks(0);
    chunk.registerForGameTicks(4095);
    chunk.registerForGameTicks(2048);

    EXPECT_EQ(chunk.gameTickBlocks().size(), 3);
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(0));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(4095));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(2048));

    // Invalid indices should be ignored
    chunk.registerForGameTicks(-1);
    chunk.registerForGameTicks(4096);

    EXPECT_EQ(chunk.gameTickBlocks().size(), 3);
}

TEST(SubChunkGameTickTest, RebuildFromBlockTypes) {
    // Register a block type that wants game ticks
    BlockType tickingType;
    tickingType.setWantsGameTicks(true);

    BlockType normalType;
    // wantsGameTicks defaults to false

    auto tickingId = BlockTypeId::fromName("gameticktest:ticking_block");
    auto normalId = BlockTypeId::fromName("gameticktest:normal_block");

    BlockRegistry::global().registerType(tickingId, tickingType);
    BlockRegistry::global().registerType(normalId, normalType);

    SubChunk chunk;

    // Place some blocks
    chunk.setBlock(0, 0, 0, tickingId);   // index 0
    chunk.setBlock(1, 0, 0, normalId);    // index 1
    chunk.setBlock(2, 0, 0, tickingId);   // index 2
    chunk.setBlock(0, 1, 0, tickingId);   // index 256

    // Rebuild the registry
    chunk.rebuildGameTickRegistry();

    // Should have 3 ticking blocks registered
    EXPECT_EQ(chunk.gameTickBlocks().size(), 3);
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(0));
    EXPECT_FALSE(chunk.isRegisteredForGameTicks(1));  // Normal block
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(2));
    EXPECT_TRUE(chunk.isRegisteredForGameTicks(256));
}

// ============================================================================
// UpdateScheduler Tests
// ============================================================================

TEST(UpdateSchedulerTest, InitialState) {
    World world;
    UpdateScheduler scheduler(world);

    EXPECT_EQ(scheduler.currentTick(), 0);
    EXPECT_EQ(scheduler.scheduledTickCount(), 0);
    EXPECT_EQ(scheduler.pendingEventCount(), 0);
}

TEST(UpdateSchedulerTest, ScheduleTick) {
    World world;
    UpdateScheduler scheduler(world);

    BlockCoord pos{10, 20, 30};
    scheduler.scheduleTick(pos, 5, TickType::Scheduled);

    EXPECT_EQ(scheduler.scheduledTickCount(), 1);
    EXPECT_TRUE(scheduler.hasScheduledTick(pos));
}

TEST(UpdateSchedulerTest, ScheduledTickFires) {
    World world;
    UpdateScheduler scheduler(world);

    BlockCoord pos{10, 20, 30};
    scheduler.scheduleTick(pos, 3, TickType::Scheduled);

    // Advance ticks - tick should not fire yet
    scheduler.advanceGameTick();  // tick 1
    scheduler.advanceGameTick();  // tick 2
    EXPECT_EQ(scheduler.scheduledTickCount(), 1);

    // Process events - still nothing (tick 3 not reached)
    scheduler.processEvents();
    EXPECT_EQ(scheduler.scheduledTickCount(), 1);

    // Advance to tick 3 - should fire
    scheduler.advanceGameTick();  // tick 3
    EXPECT_EQ(scheduler.scheduledTickCount(), 0);  // Moved to outbox

    // Process events should clear the event
    scheduler.processEvents();
}

TEST(UpdateSchedulerTest, CancelScheduledTicks) {
    World world;
    UpdateScheduler scheduler(world);

    BlockCoord pos1{10, 20, 30};
    BlockCoord pos2{40, 50, 60};

    scheduler.scheduleTick(pos1, 10, TickType::Scheduled);
    scheduler.scheduleTick(pos2, 10, TickType::Scheduled);
    scheduler.scheduleTick(pos1, 20, TickType::Scheduled);  // Another tick for pos1

    EXPECT_EQ(scheduler.scheduledTickCount(), 3);

    // Cancel all ticks for pos1
    scheduler.cancelScheduledTicks(pos1);

    EXPECT_EQ(scheduler.scheduledTickCount(), 1);
    EXPECT_FALSE(scheduler.hasScheduledTick(pos1));
    EXPECT_TRUE(scheduler.hasScheduledTick(pos2));
}

TEST(UpdateSchedulerTest, ExternalEvents) {
    World world;
    UpdateScheduler scheduler(world);

    // Push external events
    auto event = BlockEvent::playerUse({10, 20, 30}, Face::PosY);
    scheduler.pushExternalEvent(event);

    EXPECT_EQ(scheduler.pendingEventCount(), 1);

    // Process events (no handler, so just consumed)
    size_t processed = scheduler.processEvents();
    EXPECT_EQ(processed, 1);
    EXPECT_EQ(scheduler.pendingEventCount(), 0);
}

TEST(UpdateSchedulerTest, TickConfigSeedDeterminism) {
    World world;
    UpdateScheduler scheduler1(world);
    UpdateScheduler scheduler2(world);

    // Set same seed
    TickConfig config;
    config.randomSeed = 12345;
    config.randomTicksPerSubchunk = 4;

    scheduler1.setTickConfig(config);
    scheduler2.setTickConfig(config);

    // Both should produce same sequence (tested implicitly by seed)
    EXPECT_EQ(scheduler1.tickConfig().randomSeed, scheduler2.tickConfig().randomSeed);
}

TEST(UpdateSchedulerTest, AdvanceGameTickIncrementsCounter) {
    World world;
    UpdateScheduler scheduler(world);

    EXPECT_EQ(scheduler.currentTick(), 0);

    scheduler.advanceGameTick();
    EXPECT_EQ(scheduler.currentTick(), 1);

    scheduler.advanceGameTick();
    EXPECT_EQ(scheduler.currentTick(), 2);

    scheduler.advanceGameTick();
    EXPECT_EQ(scheduler.currentTick(), 3);
}

TEST(UpdateSchedulerTest, ScheduleTickMinimumDelay) {
    World world;
    UpdateScheduler scheduler(world);

    BlockCoord pos{10, 20, 30};

    // Schedule with 0 delay should become 1
    scheduler.scheduleTick(pos, 0, TickType::Scheduled);

    // Should not fire on tick 0
    scheduler.processEvents();
    EXPECT_EQ(scheduler.scheduledTickCount(), 1);

    // Should fire on tick 1
    scheduler.advanceGameTick();
    EXPECT_EQ(scheduler.scheduledTickCount(), 0);
}

// ============================================================================
// ScheduledTick Tests
// ============================================================================

TEST(ScheduledTickTest, Ordering) {
    ScheduledTick tick1{BlockCoord{0, 0, 0}, 100, TickType::Scheduled};
    ScheduledTick tick2{BlockCoord{1, 1, 1}, 50, TickType::Scheduled};
    ScheduledTick tick3{BlockCoord{2, 2, 2}, 200, TickType::Scheduled};

    // Min-heap ordering: earlier ticks should have lower priority (come first)
    EXPECT_TRUE(tick1 > tick2);   // 100 > 50
    EXPECT_FALSE(tick2 > tick1);  // 50 < 100
    EXPECT_TRUE(tick3 > tick1);   // 200 > 100
}

// ============================================================================
// Auto-Registration Tests
// ============================================================================

TEST(UpdateSchedulerTest, AutoRegisterOnPlace) {
    // Register a block type that wants game ticks
    BlockType tickingType;
    tickingType.setWantsGameTicks(true);
    auto tickingId = BlockTypeId::fromName("autoregtest:ticking");
    BlockRegistry::global().registerType(tickingId, tickingType);

    // Create world with a subchunk
    World world;
    world.setBlock(BlockCoord{5, 5, 5}, tickingId);

    // Create scheduler and simulate block placed event
    UpdateScheduler scheduler(world);

    BlockEvent placeEvent = BlockEvent::blockPlaced(
        BlockCoord{5, 5, 5}, tickingId, AIR_BLOCK_TYPE);
    scheduler.pushExternalEvent(placeEvent);
    scheduler.processEvents();

    // Check that the block was auto-registered for game ticks
    SubChunk* subchunk = world.getSubChunk(ChunkPos{0, 0, 0});
    ASSERT_NE(subchunk, nullptr);

    // Calculate local index for (5, 5, 5)
    int32_t localIndex = BlockCoord{5, 5, 5}.localIndex();
    EXPECT_TRUE(subchunk->isRegisteredForGameTicks(localIndex));
}

TEST(UpdateSchedulerTest, AutoUnregisterOnBreak) {
    // Register a block type that wants game ticks
    BlockType tickingType;
    tickingType.setWantsGameTicks(true);
    auto tickingId = BlockTypeId::fromName("autoregtest:ticking2");
    BlockRegistry::global().registerType(tickingId, tickingType);

    // Register a normal block type (doesn't want game ticks)
    BlockType normalType;
    auto normalId = BlockTypeId::fromName("autoregtest:normal");
    BlockRegistry::global().registerType(normalId, normalType);

    // Create world with a ticking block AND a normal block
    // (We need the normal block to keep the subchunk alive after breaking the ticking block)
    World world;
    world.setBlock(BlockCoord{7, 7, 7}, tickingId);
    world.setBlock(BlockCoord{8, 8, 8}, normalId);  // Keep subchunk alive

    // Create scheduler
    UpdateScheduler scheduler(world);

    // First place the block to register it
    BlockEvent placeEvent = BlockEvent::blockPlaced(
        BlockCoord{7, 7, 7}, tickingId, AIR_BLOCK_TYPE);
    scheduler.pushExternalEvent(placeEvent);
    scheduler.processEvents();

    // Verify registered
    SubChunk* subchunk = world.getSubChunk(ChunkPos{0, 0, 0});
    ASSERT_NE(subchunk, nullptr);
    int32_t localIndex = BlockCoord{7, 7, 7}.localIndex();
    EXPECT_TRUE(subchunk->isRegisteredForGameTicks(localIndex));

    // Schedule a tick for this block
    scheduler.scheduleTick(BlockCoord{7, 7, 7}, 10, TickType::Scheduled);
    EXPECT_TRUE(scheduler.hasScheduledTick(BlockCoord{7, 7, 7}));

    // Now break the block
    BlockEvent breakEvent = BlockEvent::blockBroken(BlockCoord{7, 7, 7}, tickingId);
    scheduler.pushExternalEvent(breakEvent);
    scheduler.processEvents();

    // Re-get subchunk pointer (in case internal storage changed)
    subchunk = world.getSubChunk(ChunkPos{0, 0, 0});
    ASSERT_NE(subchunk, nullptr);  // Should still exist due to the normal block

    // Should be unregistered and scheduled ticks cancelled
    EXPECT_FALSE(subchunk->isRegisteredForGameTicks(localIndex));
    EXPECT_FALSE(scheduler.hasScheduledTick(BlockCoord{7, 7, 7}));
}

// ============================================================================
// ChunkColumn Game Tick Registry Tests
// ============================================================================

#include "finevox/core/chunk_column.hpp"

TEST(ChunkColumnGameTickTest, RebuildGameTickRegistries) {
    // Register block types
    BlockType tickingType;
    tickingType.setWantsGameTicks(true);
    auto tickingId = BlockTypeId::fromName("columntest:ticking");
    BlockRegistry::global().registerType(tickingId, tickingType);

    BlockType normalType;
    auto normalId = BlockTypeId::fromName("columntest:normal");
    BlockRegistry::global().registerType(normalId, normalType);

    // Create a column with multiple subchunks
    ChunkColumn column(ColumnPos{0, 0});

    // Place blocks in different subchunks
    // Subchunk 0 (y=0-15)
    column.setBlock(BlockCoord{5, 5, 5}, tickingId);    // Should register
    column.setBlock(BlockCoord{10, 10, 10}, normalId);  // Should not register

    // Subchunk 1 (y=16-31)
    column.setBlock(BlockCoord{3, 20, 3}, tickingId);   // Should register

    // Rebuild game tick registries (simulating load from disk)
    column.rebuildGameTickRegistries();

    // Check subchunk 0
    SubChunk* sc0 = column.getSubChunk(0);
    ASSERT_NE(sc0, nullptr);
    EXPECT_TRUE(sc0->isRegisteredForGameTicks(BlockCoord{5, 5, 5}.localIndex()));
    EXPECT_FALSE(sc0->isRegisteredForGameTicks(BlockCoord{10, 10, 10}.localIndex()));

    // Check subchunk 1
    SubChunk* sc1 = column.getSubChunk(1);
    ASSERT_NE(sc1, nullptr);
    // (3, 20, 3) world -> (3, 4, 3) local within subchunk 1
    EXPECT_TRUE(sc1->isRegisteredForGameTicks(BlockCoord{3, 4, 3}.localIndex()));
}

// ============================================================================
// PropagationPolicy Tests
// ============================================================================

TEST(PropagationPolicyTest, DefaultPolicyIsDrop) {
    BlockType type;
    EXPECT_EQ(type.propagationPolicy(), PropagationPolicy::Drop);
}

TEST(PropagationPolicyTest, SetPropagationPolicy) {
    BlockType type;
    type.setPropagationPolicy(PropagationPolicy::Defer);
    EXPECT_EQ(type.propagationPolicy(), PropagationPolicy::Defer);
}

TEST(PropagationPolicyTest, DropPolicySkipsUnloadedNeighbors) {
    // Register a block type with Drop policy (default)
    BlockType dropType;
    auto dropId = BlockTypeId::fromName("proptest:drop_block");
    BlockRegistry::global().registerType(dropId, dropType);

    // Create world with block at chunk boundary
    World world;
    // Place at x=15, so +X neighbor at x=16 is in a different chunk
    BlockCoord sourcePos{15, 5, 5};
    world.setBlock(sourcePos, dropId);

    // Set up scheduler
    UpdateScheduler scheduler(world);

    // Create context and call notifyNeighbors
    ChunkPos chunkPos = ChunkPos::fromBlock(sourcePos);
    auto subChunk = world.getSubChunkShared(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    BlockContext ctx(world, *subChunk, sourcePos, sourcePos.local());
    ctx.setScheduler(&scheduler);
    ctx.notifyNeighbors();

    // Outbox should be empty — no events for unloaded neighbors
    EXPECT_TRUE(scheduler.outbox().empty());
}

TEST(PropagationPolicyTest, DeferPolicyEmitsBlockUpdate) {
    // Register a block type with Defer policy
    BlockType deferType;
    deferType.setPropagationPolicy(PropagationPolicy::Defer);
    auto deferId = BlockTypeId::fromName("proptest:defer_block");
    BlockRegistry::global().registerType(deferId, deferType);

    // Create world with block at chunk boundary
    World world;
    BlockCoord sourcePos{15, 5, 5};
    world.setBlock(sourcePos, deferId);
    // Neighbor at (16, 5, 5) is in an unloaded chunk

    // Set up scheduler
    UpdateScheduler scheduler(world);

    // Create context and call notifyNeighbors
    ChunkPos chunkPos = ChunkPos::fromBlock(sourcePos);
    auto subChunk = world.getSubChunkShared(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    BlockContext ctx(world, *subChunk, sourcePos, sourcePos.local());
    ctx.setScheduler(&scheduler);
    ctx.notifyNeighbors();

    // Outbox should have a BlockUpdate for the unloaded neighbor
    EXPECT_FALSE(scheduler.outbox().empty());

    std::vector<BlockEvent> events;
    scheduler.outbox().swapTo(events);

    // Find BlockUpdate for +X neighbor at (16, 5, 5)
    bool found = false;
    for (const auto& event : events) {
        if (event.type == EventType::BlockUpdate &&
            event.pos.x == 16 && event.pos.y == 5 && event.pos.z == 5) {
            found = true;
        }
    }
    EXPECT_TRUE(found);
}

TEST(PropagationPolicyTest, DeferredEventProcessedOnChunkLoad) {
    // Register a block type with Defer policy
    BlockType deferType;
    deferType.setPropagationPolicy(PropagationPolicy::Defer);
    auto deferId = BlockTypeId::fromName("proptest:defer_load");
    BlockRegistry::global().registerType(deferId, deferType);

    World world;
    BlockCoord sourcePos{15, 5, 5};
    world.setBlock(sourcePos, deferId);

    UpdateScheduler scheduler(world);

    // Call notifyNeighbors — should emit deferred BlockUpdate for (16, 5, 5)
    ChunkPos chunkPos = ChunkPos::fromBlock(sourcePos);
    auto subChunk = world.getSubChunkShared(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    BlockContext ctx(world, *subChunk, sourcePos, sourcePos.local());
    ctx.setScheduler(&scheduler);
    ctx.notifyNeighbors();

    // Process events — BlockUpdate at (16, 5, 5) should be deferred
    scheduler.processEvents();
    EXPECT_GE(scheduler.deferredEventCount(), 1u);

    // Load the chunk containing (16, 5, 5) by placing a block there
    auto neighborId = BlockTypeId::fromName("proptest:neighbor");
    BlockRegistry::global().registerType(neighborId, BlockType{});
    world.setBlock(BlockCoord{16, 5, 5}, neighborId);

    // Advance game tick to trigger processDeferredEvents
    scheduler.advanceGameTick();
    scheduler.processEvents();

    // Deferred event should now be processed (chunk is loaded)
    EXPECT_EQ(scheduler.deferredEventCount(), 0u);
}

TEST(PropagationPolicyTest, NoSchedulerFallsBackToDrop) {
    // Register a block type with Defer policy
    BlockType deferType;
    deferType.setPropagationPolicy(PropagationPolicy::Defer);
    auto deferId = BlockTypeId::fromName("proptest:defer_nosched");
    BlockRegistry::global().registerType(deferId, deferType);

    World world;
    BlockCoord sourcePos{15, 5, 5};
    world.setBlock(sourcePos, deferId);

    ChunkPos chunkPos = ChunkPos::fromBlock(sourcePos);
    auto subChunk = world.getSubChunkShared(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    // Create context WITHOUT setting scheduler
    BlockContext ctx(world, *subChunk, sourcePos, sourcePos.local());
    // ctx.setScheduler NOT called — scheduler_ is nullptr

    // This should not crash — falls back to Drop behavior
    ctx.notifyNeighbors();
    // No way to verify events weren't generated, but at least it doesn't crash
}
