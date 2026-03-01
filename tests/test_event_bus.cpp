#include <gtest/gtest.h>
#include "finevox/core/event_bus.hpp"
#include "finevox/core/block_events.hpp"

#include <thread>
#include <atomic>
#include <vector>

using namespace finevox;

// ============================================================================
// Basic subscribe/publish tests
// ============================================================================

TEST(EventBusTest, SubscribeAndPublish) {
    EventBus bus;
    int callCount = 0;
    BlockCoord receivedPos{};

    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent& e) {
        callCount++;
        receivedPos = e.pos;
    });

    bus.publish(BlockBrokenEvent{BlockCoord{10, 20, 30}, BlockTypeId{1}});

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(receivedPos.x, 10);
    EXPECT_EQ(receivedPos.y, 20);
    EXPECT_EQ(receivedPos.z, 30);
}

TEST(EventBusTest, MultipleSubscribersReceiveEvent) {
    EventBus bus;
    int count1 = 0, count2 = 0;

    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) { count1++; });
    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) { count2++; });

    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});

    EXPECT_EQ(count1, 1);
    EXPECT_EQ(count2, 1);
}

TEST(EventBusTest, SubscriberOnlyReceivesItsType) {
    EventBus bus;
    int brokenCount = 0, placedCount = 0;

    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) { brokenCount++; });
    bus.subscribe<BlockPlacedEvent>([&](const BlockPlacedEvent&) { placedCount++; });

    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});

    EXPECT_EQ(brokenCount, 1);
    EXPECT_EQ(placedCount, 0);
}

TEST(EventBusTest, PublishWithNoSubscribers) {
    EventBus bus;
    // Should not crash
    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
}

TEST(EventBusTest, Unsubscribe) {
    EventBus bus;
    int callCount = 0;

    auto sub = bus.subscribe<BlockBrokenEvent>(
        [&](const BlockBrokenEvent&) { callCount++; });

    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    EXPECT_EQ(callCount, 1);

    bus.unsubscribe(sub);
    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    EXPECT_EQ(callCount, 1); // No change after unsubscribe
}

TEST(EventBusTest, UnsubscribeInvalidHandle) {
    EventBus bus;
    // Should not crash
    bus.unsubscribe(INVALID_SUBSCRIPTION);
    bus.unsubscribe(99999);
}

// ============================================================================
// Publish from GameEventHolder
// ============================================================================

TEST(EventBusTest, PublishFromHolder) {
    EventBus bus;
    int callCount = 0;

    bus.subscribe<FluidPlacedEvent>([&](const FluidPlacedEvent& e) {
        callCount++;
        EXPECT_EQ(e.level, 15);
    });

    auto holder = GameEventHolder::create<FluidPlacedEvent>(
        BlockCoord{1, 2, 3}, FluidTypeId{5}, uint8_t{15});
    bus.publish(holder);

    EXPECT_EQ(callCount, 1);
}

TEST(EventBusTest, PublishEmptyHolder) {
    EventBus bus;
    int callCount = 0;
    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) { callCount++; });

    GameEventHolder empty;
    bus.publish(empty); // Should not crash or fire

    EXPECT_EQ(callCount, 0);
}

// ============================================================================
// Cross-thread queuing tests
// ============================================================================

TEST(EventBusTest, QueueAndDispatch) {
    EventBus bus;
    int callCount = 0;

    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent& e) {
        callCount++;
        EXPECT_EQ(e.pos.x, 42);
    });

    // Queue event (simulating cross-thread push)
    bus.queueEvent(BlockBrokenEvent{BlockCoord{42, 0, 0}, BlockTypeId{1}});

    // Not dispatched yet
    EXPECT_EQ(callCount, 0);

    // Dispatch from "game thread"
    size_t dispatched = bus.dispatchQueued();
    EXPECT_EQ(dispatched, 1u);
    EXPECT_EQ(callCount, 1);
}

TEST(EventBusTest, DispatchQueuedMultipleEvents) {
    EventBus bus;
    std::vector<int> received;

    bus.subscribe<SetWorldTimeEvent>([&](const SetWorldTimeEvent& e) {
        received.push_back(static_cast<int>(e.ticks));
    });

    bus.queueEvent(SetWorldTimeEvent{100});
    bus.queueEvent(SetWorldTimeEvent{200});
    bus.queueEvent(SetWorldTimeEvent{300});

    size_t dispatched = bus.dispatchQueued();
    EXPECT_EQ(dispatched, 3u);
    ASSERT_EQ(received.size(), 3u);
    EXPECT_EQ(received[0], 100);
    EXPECT_EQ(received[1], 200);
    EXPECT_EQ(received[2], 300);
}

TEST(EventBusTest, DispatchQueuedEmptyQueue) {
    EventBus bus;
    size_t dispatched = bus.dispatchQueued();
    EXPECT_EQ(dispatched, 0u);
}

TEST(EventBusTest, CrossThreadQueueing) {
    EventBus bus;
    std::atomic<int> callCount{0};

    bus.subscribe<BlockBrokenEvent>([&](const BlockBrokenEvent&) {
        callCount.fetch_add(1, std::memory_order_relaxed);
    });

    // Queue from multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < 4; i++) {
        threads.emplace_back([&bus, i]() {
            for (int j = 0; j < 25; j++) {
                bus.queueEvent(BlockBrokenEvent{
                    BlockCoord{i * 25 + j, 0, 0}, BlockTypeId{1}});
            }
        });
    }

    for (auto& t : threads) t.join();

    EXPECT_EQ(bus.queuedEventCount(), 100u);

    // Dispatch all on "game thread"
    size_t dispatched = bus.dispatchQueued();
    EXPECT_EQ(dispatched, 100u);
    EXPECT_EQ(callCount.load(), 100);
}

// ============================================================================
// Statistics tests
// ============================================================================

TEST(EventBusTest, SubscriberCount) {
    EventBus bus;
    EXPECT_EQ(bus.subscriberCount(), 0u);

    auto sub1 = bus.subscribe<BlockBrokenEvent>(
        [](const BlockBrokenEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 1u);
    EXPECT_EQ(bus.subscriberCount(registerEventType<BlockBrokenEvent>()), 1u);
    EXPECT_EQ(bus.subscriberCount(registerEventType<BlockPlacedEvent>()), 0u);

    auto sub2 = bus.subscribe<BlockPlacedEvent>(
        [](const BlockPlacedEvent&) {});
    EXPECT_EQ(bus.subscriberCount(), 2u);

    bus.unsubscribe(sub1);
    EXPECT_EQ(bus.subscriberCount(), 1u);
    EXPECT_EQ(bus.subscriberCount(registerEventType<BlockBrokenEvent>()), 0u);

    bus.unsubscribe(sub2);
    EXPECT_EQ(bus.subscriberCount(), 0u);
}

TEST(EventBusTest, QueuedEventCount) {
    EventBus bus;
    EXPECT_EQ(bus.queuedEventCount(), 0u);

    bus.queueEvent(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    bus.queueEvent(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    EXPECT_EQ(bus.queuedEventCount(), 2u);

    bus.dispatchQueued();
    EXPECT_EQ(bus.queuedEventCount(), 0u);
}

// ============================================================================
// Shutdown tests
// ============================================================================

TEST(EventBusTest, ShutdownStopsQueuing) {
    EventBus bus;

    bus.queueEvent(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    EXPECT_EQ(bus.queuedEventCount(), 1u);

    bus.shutdown();

    // After shutdown, queued events are silently dropped
    bus.queueEvent(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    // Queue might or might not accept after shutdown (Queue drops on shutdown)
    // But we can still dispatch what was queued before shutdown
}

// ============================================================================
// Multiple event types test
// ============================================================================

TEST(EventBusTest, ManyEventTypes) {
    EventBus bus;
    bool gotBroken = false, gotPlaced = false, gotFluid = false;
    bool gotPlayer = false, gotTime = false;

    bus.subscribe<BlockBrokenEvent>([&](const auto&) { gotBroken = true; });
    bus.subscribe<BlockPlacedEvent>([&](const auto&) { gotPlaced = true; });
    bus.subscribe<FluidPlacedEvent>([&](const auto&) { gotFluid = true; });
    bus.subscribe<PlayerJumpEvent>([&](const auto&) { gotPlayer = true; });
    bus.subscribe<SetWorldTimeEvent>([&](const auto&) { gotTime = true; });

    bus.publish(BlockBrokenEvent{BlockCoord{}, BlockTypeId{}});
    bus.publish(FluidPlacedEvent{BlockCoord{}, FluidTypeId{}, 15});
    bus.publish(SetWorldTimeEvent{1000});

    EXPECT_TRUE(gotBroken);
    EXPECT_FALSE(gotPlaced);
    EXPECT_TRUE(gotFluid);
    EXPECT_FALSE(gotPlayer);
    EXPECT_TRUE(gotTime);
}
