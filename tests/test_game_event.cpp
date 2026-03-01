#include <gtest/gtest.h>
#include "finevox/core/game_event.hpp"
#include "finevox/core/block_events.hpp"

#include <string>
#include <vector>

using namespace finevox;

// ============================================================================
// EventTypeId registration tests
// ============================================================================

TEST(EventTypeIdTest, SameTypeReturnsSameId) {
    EventTypeId id1 = registerEventType<BlockPlacedEvent>();
    EventTypeId id2 = registerEventType<BlockPlacedEvent>();
    EXPECT_EQ(id1, id2);
}

TEST(EventTypeIdTest, DifferentTypesReturnDifferentIds) {
    EventTypeId id1 = registerEventType<BlockPlacedEvent>();
    EventTypeId id2 = registerEventType<BlockBrokenEvent>();
    EXPECT_NE(id1, id2);
}

TEST(EventTypeIdTest, IdsAreNonZero) {
    EventTypeId id = registerEventType<BlockTickEvent>();
    EXPECT_NE(id, INVALID_EVENT_TYPE);
}

// ============================================================================
// GameEventHolder SBO tests
// ============================================================================

TEST(GameEventHolderTest, CreateSmallEvent) {
    auto holder = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{1, 2, 3}, BlockTypeId{42});

    EXPECT_TRUE(holder.hasValue());
    EXPECT_TRUE(holder.is<BlockBrokenEvent>());
    EXPECT_FALSE(holder.is<BlockPlacedEvent>());
}

TEST(GameEventHolderTest, GetReturnsCorrectData) {
    auto holder = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{10, 20, 30}, BlockTypeId{99});

    auto& event = holder.get<BlockBrokenEvent>();
    EXPECT_EQ(event.pos.x, 10);
    EXPECT_EQ(event.pos.y, 20);
    EXPECT_EQ(event.pos.z, 30);
    EXPECT_EQ(event.oldType.id, 99u);
}

TEST(GameEventHolderTest, TryGetReturnsNullForWrongType) {
    auto holder = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{1, 2, 3}, BlockTypeId{42});

    EXPECT_NE(holder.tryGet<BlockBrokenEvent>(), nullptr);
    EXPECT_EQ(holder.tryGet<BlockPlacedEvent>(), nullptr);
}

TEST(GameEventHolderTest, DefaultConstructedIsEmpty) {
    GameEventHolder holder;
    EXPECT_FALSE(holder.hasValue());
    EXPECT_EQ(holder.typeId(), INVALID_EVENT_TYPE);
}

TEST(GameEventHolderTest, MoveConstructor) {
    auto holder1 = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{5, 6, 7}, BlockTypeId{11});

    GameEventHolder holder2(std::move(holder1));
    EXPECT_TRUE(holder2.hasValue());
    EXPECT_TRUE(holder2.is<BlockBrokenEvent>());
    EXPECT_EQ(holder2.get<BlockBrokenEvent>().pos.x, 5);

    // Moved-from should be empty
    EXPECT_FALSE(holder1.hasValue()); // NOLINT(bugprone-use-after-move)
}

TEST(GameEventHolderTest, MoveAssignment) {
    auto holder1 = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{1, 2, 3}, BlockTypeId{10});
    auto holder2 = GameEventHolder::create<BlockPlacedEvent>(
        BlockCoord{4, 5, 6}, BlockTypeId{20}, BlockTypeId{30}, Rotation{});

    holder2 = std::move(holder1);
    EXPECT_TRUE(holder2.is<BlockBrokenEvent>());
    EXPECT_EQ(holder2.get<BlockBrokenEvent>().pos.x, 1);
    EXPECT_FALSE(holder1.hasValue()); // NOLINT(bugprone-use-after-move)
}

TEST(GameEventHolderTest, SelfMoveAssignment) {
    auto holder = GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{1, 2, 3}, BlockTypeId{42});

    auto* p = &holder;
    *p = std::move(holder); // Self-move should not crash
    // Behavior after self-move is implementation-defined but should not crash
}

// Test with a large event that exceeds SBO
struct LargeEvent {
    char data[GameEventHolder::SBO_SIZE + 16] = {};
    int marker = 0;
};

TEST(GameEventHolderTest, LargeEventUsesHeap) {
    static_assert(sizeof(LargeEvent) > GameEventHolder::SBO_SIZE,
        "LargeEvent must exceed SBO size for this test");

    LargeEvent large;
    large.marker = 12345;
    auto holder = GameEventHolder::create<LargeEvent>(std::move(large));

    EXPECT_TRUE(holder.hasValue());
    EXPECT_TRUE(holder.is<LargeEvent>());
    EXPECT_EQ(holder.get<LargeEvent>().marker, 12345);
}

TEST(GameEventHolderTest, LargeEventMoveConstructor) {
    LargeEvent large;
    large.marker = 999;
    auto holder1 = GameEventHolder::create<LargeEvent>(std::move(large));

    GameEventHolder holder2(std::move(holder1));
    EXPECT_TRUE(holder2.hasValue());
    EXPECT_EQ(holder2.get<LargeEvent>().marker, 999);
    EXPECT_FALSE(holder1.hasValue()); // NOLINT(bugprone-use-after-move)
}

// Test with event containing a non-trivial type
struct EventWithString {
    std::string message;
    int value = 0;
};

TEST(GameEventHolderTest, NonTrivialEvent) {
    auto holder = GameEventHolder::create<EventWithString>(
        EventWithString{"hello world", 42});

    EXPECT_TRUE(holder.is<EventWithString>());
    EXPECT_EQ(holder.get<EventWithString>().message, "hello world");
    EXPECT_EQ(holder.get<EventWithString>().value, 42);
}

TEST(GameEventHolderTest, NonTrivialEventMoveDestroysCleanly) {
    // Verify no memory leaks by moving non-trivial events
    auto holder1 = GameEventHolder::create<EventWithString>(
        EventWithString{"test string", 100});

    GameEventHolder holder2(std::move(holder1));
    holder2 = GameEventHolder(); // Assign empty, should destruct string cleanly
    EXPECT_FALSE(holder2.hasValue());
}

// ============================================================================
// Typed event struct tests
// ============================================================================

TEST(BlockEventsTest, BlockPlacedEventFields) {
    BlockPlacedEvent e{
        BlockCoord{10, 20, 30},
        BlockTypeId{1},
        BlockTypeId{2},
        Rotation{}
    };
    EXPECT_EQ(e.pos.x, 10);
    EXPECT_EQ(e.newType.id, 1u);
    EXPECT_EQ(e.oldType.id, 2u);
}

TEST(BlockEventsTest, PlayerPositionEventFields) {
    EntityState state;
    state.position = glm::dvec3(1.0, 2.0, 3.0);
    state.onGround = true;

    PlayerPositionEvent e{EntityId{42}, state};
    EXPECT_EQ(e.entityId, 42u);
    EXPECT_DOUBLE_EQ(e.state.position.x, 1.0);
    EXPECT_TRUE(e.state.onGround);
}

TEST(BlockEventsTest, FluidPlacedEventFields) {
    FluidPlacedEvent e{BlockCoord{5, 6, 7}, FluidTypeId{3}, 10};
    EXPECT_EQ(e.pos.x, 5);
    EXPECT_EQ(e.type.id, 3u);
    EXPECT_EQ(e.level, 10);
}

TEST(BlockEventsTest, SetWorldTimeEventFields) {
    SetWorldTimeEvent e{36000};
    EXPECT_EQ(e.ticks, 36000);
}

// ============================================================================
// SBO size verification
// ============================================================================

TEST(BlockEventsTest, AllEventsFitInSBO) {
    // Verify all standard event types fit within the 96-byte SBO
    EXPECT_LE(sizeof(BlockPlacedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockBrokenEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockChangedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockTickEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(NeighborUpdatedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockUpdateEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockInteractEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(BlockStrikeEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(RepaintEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(FluidPlacedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(FluidRemovedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(PlayerPositionEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(PlayerLookEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(PlayerJumpEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(PlayerSprintEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(PlayerSneakEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(ChunkLoadedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(ChunkUnloadedEvent), GameEventHolder::SBO_SIZE);
    EXPECT_LE(sizeof(SetWorldTimeEvent), GameEventHolder::SBO_SIZE);
}

// ============================================================================
// EventConsolidation tests
// ============================================================================

TEST(EventConsolidationTest, NeighborUpdatedMergesFaceMasks) {
    NeighborUpdatedEvent e1{BlockCoord{1, 2, 3}, 0b000001}; // NegX
    NeighborUpdatedEvent e2{BlockCoord{1, 2, 3}, 0b000100}; // NegY

    auto merged = EventConsolidation<NeighborUpdatedEvent>::merge(e1, e2);
    EXPECT_EQ(merged.faceMask, 0b000101); // NegX | NegY
    EXPECT_EQ(merged.pos.x, 1);
}

TEST(EventConsolidationTest, NeighborUpdatedKeyIsPosition) {
    NeighborUpdatedEvent e{BlockCoord{10, 20, 30}, 0b111111};
    auto key = EventConsolidation<NeighborUpdatedEvent>::key(e);
    EXPECT_EQ(key.x, 10);
    EXPECT_EQ(key.y, 20);
    EXPECT_EQ(key.z, 30);
}

TEST(EventConsolidationTest, DefaultTraitIsNotSupported) {
    EXPECT_FALSE(EventConsolidation<BlockPlacedEvent>::supported);
    EXPECT_FALSE(EventConsolidation<BlockBrokenEvent>::supported);
}

// ============================================================================
// GameEventHolder in containers
// ============================================================================

TEST(GameEventHolderTest, VectorOfHolders) {
    std::vector<GameEventHolder> events;
    events.push_back(GameEventHolder::create<BlockBrokenEvent>(
        BlockCoord{1, 2, 3}, BlockTypeId{10}));
    events.push_back(GameEventHolder::create<FluidPlacedEvent>(
        BlockCoord{4, 5, 6}, FluidTypeId{2}, uint8_t{15}));
    events.push_back(GameEventHolder::create<PlayerJumpEvent>(EntityId{1}));

    EXPECT_EQ(events.size(), 3u);
    EXPECT_TRUE(events[0].is<BlockBrokenEvent>());
    EXPECT_TRUE(events[1].is<FluidPlacedEvent>());
    EXPECT_TRUE(events[2].is<PlayerJumpEvent>());
}
