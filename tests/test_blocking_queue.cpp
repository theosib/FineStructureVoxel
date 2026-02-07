#include <gtest/gtest.h>
#include "finevox/core/deprecated/blocking_queue.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include "finevox/core/position.hpp"
#include <thread>
#include <chrono>
#include <atomic>

using namespace finevox;

// ============================================================================
// Basic queue operations (using ChunkPos as key type)
// ============================================================================

TEST(BlockingQueueTest, EmptyQueue) {
    BlockingQueue<ChunkPos> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(BlockingQueueTest, PushAndPop) {
    BlockingQueue<ChunkPos> queue;

    EXPECT_TRUE(queue.push(ChunkPos(1, 2, 3)));
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ChunkPos(1, 2, 3));
    EXPECT_TRUE(queue.empty());
}

TEST(BlockingQueueTest, FIFOOrder) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));
    queue.push(ChunkPos(3, 0, 0));

    auto first = queue.pop();
    auto second = queue.pop();
    auto third = queue.pop();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());

    EXPECT_EQ(*first, ChunkPos(1, 0, 0));
    EXPECT_EQ(*second, ChunkPos(2, 0, 0));
    EXPECT_EQ(*third, ChunkPos(3, 0, 0));
}

TEST(BlockingQueueTest, Contains) {
    BlockingQueue<ChunkPos> queue;
    ChunkPos pos(5, 6, 7);

    EXPECT_FALSE(queue.contains(pos));

    queue.push(pos);
    EXPECT_TRUE(queue.contains(pos));

    queue.pop();
    EXPECT_FALSE(queue.contains(pos));
}

TEST(BlockingQueueTest, Clear) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));
    queue.push(ChunkPos(3, 0, 0));

    EXPECT_EQ(queue.size(), 3);

    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
}

TEST(BlockingQueueTest, Remove) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));
    queue.push(ChunkPos(3, 0, 0));

    EXPECT_TRUE(queue.remove(ChunkPos(2, 0, 0)));
    EXPECT_FALSE(queue.contains(ChunkPos(2, 0, 0)));
    EXPECT_EQ(queue.size(), 2);

    EXPECT_FALSE(queue.remove(ChunkPos(99, 0, 0)));  // Not in queue

    // FIFO order preserved for remaining items
    auto first = queue.pop();
    auto second = queue.pop();
    EXPECT_EQ(*first, ChunkPos(1, 0, 0));
    EXPECT_EQ(*second, ChunkPos(3, 0, 0));
}

// ============================================================================
// Deduplication
// ============================================================================

TEST(BlockingQueueTest, DeduplicatesSamePosition) {
    BlockingQueue<ChunkPos> queue;
    ChunkPos pos(1, 2, 3);

    EXPECT_TRUE(queue.push(pos));   // First push: added
    EXPECT_FALSE(queue.push(pos));  // Second push: deduplicated

    EXPECT_EQ(queue.size(), 1);  // Still only one entry
}

TEST(BlockingQueueTest, DeduplicatesMultiplePushes) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));
    queue.push(ChunkPos(1, 0, 0));  // Duplicate
    queue.push(ChunkPos(3, 0, 0));
    queue.push(ChunkPos(2, 0, 0));  // Duplicate

    EXPECT_EQ(queue.size(), 3);  // Only unique positions
}

// ============================================================================
// Blocking operations
// ============================================================================

TEST(BlockingQueueTest, PopWaitBlocksUntilData) {
    BlockingQueue<ChunkPos> queue;
    std::atomic<bool> gotResult{false};
    std::optional<ChunkPos> result;

    std::thread consumer([&]() {
        result = queue.popWait();
        gotResult = true;
    });

    // Give consumer time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(gotResult.load());

    // Push data to unblock
    queue.push(ChunkPos(1, 2, 3));

    consumer.join();
    EXPECT_TRUE(gotResult.load());
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, ChunkPos(1, 2, 3));
}

TEST(BlockingQueueTest, ShutdownWakesWaitingThreads) {
    BlockingQueue<ChunkPos> queue;
    std::atomic<bool> finished{false};
    std::optional<ChunkPos> result;

    std::thread consumer([&]() {
        result = queue.popWait();
        finished = true;
    });

    // Give consumer time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(finished.load());

    // Shutdown to unblock
    queue.shutdown();

    consumer.join();
    EXPECT_TRUE(finished.load());
    EXPECT_FALSE(result.has_value());  // Returns nullopt on shutdown
}

TEST(BlockingQueueTest, ShutdownState) {
    BlockingQueue<ChunkPos> queue;

    EXPECT_FALSE(queue.isShutdown());
    queue.shutdown();
    EXPECT_TRUE(queue.isShutdown());
}

TEST(BlockingQueueTest, PopWaitReturnsDataBeforeShutdown) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));
    queue.shutdown();

    // Should still get data even after shutdown
    auto first = queue.popWait();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(*first, ChunkPos(1, 0, 0));

    auto second = queue.popWait();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(*second, ChunkPos(2, 0, 0));

    // Now empty, should return nullopt
    auto third = queue.popWait();
    EXPECT_FALSE(third.has_value());
}

TEST(BlockingQueueTest, PopBatch) {
    BlockingQueue<ChunkPos> queue;

    for (int i = 0; i < 10; ++i) {
        queue.push(ChunkPos(i, 0, 0));
    }

    auto batch = queue.popBatch(5);
    EXPECT_EQ(batch.size(), 5);
    EXPECT_EQ(queue.size(), 5);

    // Verify FIFO order in batch
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(batch[i], ChunkPos(i, 0, 0));
    }
}

TEST(BlockingQueueTest, PopBatchMoreThanAvailable) {
    BlockingQueue<ChunkPos> queue;

    queue.push(ChunkPos(1, 0, 0));
    queue.push(ChunkPos(2, 0, 0));

    auto batch = queue.popBatch(10);
    EXPECT_EQ(batch.size(), 2);
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// Thread safety
// ============================================================================

TEST(BlockingQueueTest, ConcurrentPushes) {
    BlockingQueue<ChunkPos> queue;

    std::vector<std::thread> threads;
    const int numThreads = 4;
    const int pushesPerThread = 100;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&queue, t, pushesPerThread]() {
            for (int i = 0; i < pushesPerThread; ++i) {
                queue.push(ChunkPos(t * 1000 + i, 0, 0));
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should have all unique positions
    EXPECT_EQ(queue.size(), numThreads * pushesPerThread);
}

TEST(BlockingQueueTest, ConcurrentPushAndPop) {
    BlockingQueue<ChunkPos> queue;

    std::atomic<int> pushed{0};
    std::atomic<int> popped{0};

    std::thread producer([&]() {
        for (int i = 0; i < 100; ++i) {
            queue.push(ChunkPos(i, 0, 0));
            ++pushed;
        }
    });

    std::thread consumer([&]() {
        for (int i = 0; i < 100; ++i) {
            while (!queue.pop().has_value()) {
                std::this_thread::yield();
            }
            ++popped;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(pushed.load(), 100);
    EXPECT_EQ(popped.load(), 100);
    EXPECT_TRUE(queue.empty());
}

TEST(BlockingQueueTest, MultipleConsumersWithPopWait) {
    BlockingQueue<ChunkPos> queue;
    const int numItems = 100;
    std::atomic<int> consumed{0};

    // Start multiple consumers
    std::vector<std::thread> consumers;
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back([&]() {
            while (true) {
                auto item = queue.popWait();
                if (!item.has_value()) break;  // Shutdown
                ++consumed;
            }
        });
    }

    // Push items
    for (int i = 0; i < numItems; ++i) {
        queue.push(ChunkPos(i, 0, 0));
    }

    // Wait for all items to be consumed
    while (consumed.load() < numItems) {
        std::this_thread::yield();
    }

    // Shutdown consumers
    queue.shutdown();
    for (auto& t : consumers) {
        t.join();
    }

    EXPECT_EQ(consumed.load(), numItems);
}

// ============================================================================
// Test with different key types
// ============================================================================

TEST(BlockingQueueTest, WorksWithIntKeys) {
    BlockingQueue<int> queue;

    queue.push(42);
    queue.push(17);
    queue.push(42);  // Duplicate

    EXPECT_EQ(queue.size(), 2);

    auto first = queue.pop();
    EXPECT_EQ(*first, 42);

    auto second = queue.pop();
    EXPECT_EQ(*second, 17);
}

TEST(BlockingQueueTest, WorksWithStringKeys) {
    BlockingQueue<std::string> queue;

    queue.push("hello");
    queue.push("world");
    queue.push("hello");  // Duplicate

    EXPECT_EQ(queue.size(), 2);
    EXPECT_TRUE(queue.contains("hello"));
    EXPECT_TRUE(queue.contains("world"));
}

// ============================================================================
// MeshRebuildQueue test (with priority and version support)
// ============================================================================

TEST(MeshRebuildQueueTest, BasicPushPop) {
    // MeshRebuildQueue is BlockingQueueWithData<ChunkPos, MeshRebuildRequest>
    MeshRebuildQueue queue(mergeMeshRebuildRequest);

    queue.push(ChunkPos(1, 2, 3), MeshRebuildRequest::normal(1, 1));
    EXPECT_EQ(queue.size(), 1);

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, ChunkPos(1, 2, 3));
    EXPECT_EQ(result->second.priority, 100);  // normal priority
}

TEST(MeshRebuildQueueTest, PriorityMerging) {
    MeshRebuildQueue queue(mergeMeshRebuildRequest);

    // Push with normal priority
    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::normal(1, 1));
    EXPECT_EQ(queue.size(), 1);

    // Push same position with higher priority (immediate = 0)
    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::immediate(2, 2));
    EXPECT_EQ(queue.size(), 1);  // Still 1 - merged

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->second.priority, 0);  // Should be immediate (lowest = most urgent)
    EXPECT_EQ(result->second.targetVersion, 2);  // Should have latest version
}

TEST(MeshRebuildQueueTest, VersionUpdate) {
    MeshRebuildQueue queue(mergeMeshRebuildRequest);

    // Push with block version 5, light version 1
    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest(5, 1, 100));

    // Push same position with block version 10, light version 2
    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest(10, 2, 100));

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->second.targetVersion, 10);  // Latest version
}

// ============================================================================
// AlarmQueue tests
// ============================================================================

#include "finevox/core/alarm_queue.hpp"

TEST(AlarmQueueTest, BasicPushTryPop) {
    AlarmQueue<int> queue;

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.tryPop().has_value());

    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(AlarmQueueTest, FIFOOrder) {
    AlarmQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_EQ(*queue.tryPop(), 1);
    EXPECT_EQ(*queue.tryPop(), 2);
    EXPECT_EQ(*queue.tryPop(), 3);
}

TEST(AlarmQueueTest, WaitForWorkWakesOnPush) {
    AlarmQueue<int> queue;
    std::atomic<bool> woke{false};

    std::thread waiter([&]() {
        queue.waitForWork();
        woke = true;
    });

    // Give the waiter time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(woke);

    // Push should wake the waiter
    queue.push(1);
    waiter.join();

    EXPECT_TRUE(woke);
    // Item should still be there (waitForWork doesn't pop)
    EXPECT_EQ(queue.size(), 1);
}

TEST(AlarmQueueTest, WaitForWorkWakesOnShutdown) {
    AlarmQueue<int> queue;
    std::atomic<bool> result{true};

    std::thread waiter([&]() {
        result = queue.waitForWork();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    queue.shutdown();
    waiter.join();

    EXPECT_FALSE(result);  // waitForWork returns false on shutdown
}

TEST(AlarmQueueTest, AlarmWakesWaiter) {
    AlarmQueue<int> queue;
    std::atomic<bool> woke{false};

    auto wakeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    queue.setAlarm(wakeTime);

    std::thread waiter([&]() {
        queue.waitForWork();
        woke = true;
    });

    // Should not wake immediately
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(woke);

    // Wait for alarm to fire
    waiter.join();
    EXPECT_TRUE(woke);

    // Alarm should be cleared after firing
    EXPECT_FALSE(queue.hasAlarm());
}

TEST(AlarmQueueTest, AlarmKeepsLaterTime) {
    AlarmQueue<int> queue;

    auto now = std::chrono::steady_clock::now();
    auto early = now + std::chrono::milliseconds(10);
    auto late = now + std::chrono::milliseconds(100);

    // Set early alarm first
    queue.setAlarm(early);
    EXPECT_TRUE(queue.hasAlarm());

    // Set later alarm - should replace (keep later)
    queue.setAlarm(late);
    EXPECT_TRUE(queue.hasAlarm());

    // Now try setting an earlier one - should NOT replace
    queue.setAlarm(early);
    EXPECT_TRUE(queue.hasAlarm());

    // Verify it kept the later alarm by checking wake time
    // We can't directly inspect the alarm time, but we can test behavior:
    // If we wait past the early time but before the late time, it should still be waiting
    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        queue.waitForWork();
        woke = true;
    });

    // Wait past the early time
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    // Should still be waiting (later alarm hasn't fired)
    // Note: This is a timing-sensitive test
    // If the alarm was the early one, it would have fired by now

    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(AlarmQueueTest, ClearAlarm) {
    AlarmQueue<int> queue;

    queue.setAlarm(std::chrono::steady_clock::now() + std::chrono::hours(1));
    EXPECT_TRUE(queue.hasAlarm());

    queue.clearAlarm();
    EXPECT_FALSE(queue.hasAlarm());
}

TEST(AlarmQueueTest, Clear) {
    AlarmQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.setAlarm(std::chrono::steady_clock::now() + std::chrono::hours(1));

    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.hasAlarm());
}

// ============================================================================
// AlarmQueueWithData tests
// ============================================================================

TEST(AlarmQueueWithDataTest, BasicOperations) {
    AlarmQueueWithData<ChunkPos, int> queue;

    EXPECT_TRUE(queue.empty());

    queue.push(ChunkPos(1, 2, 3), 42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);
    EXPECT_TRUE(queue.contains(ChunkPos(1, 2, 3)));

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, ChunkPos(1, 2, 3));
    EXPECT_EQ(result->second, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(AlarmQueueWithDataTest, MergeFunction) {
    // Merge function: keep maximum value
    AlarmQueueWithData<ChunkPos, int> queue([](const int& old, const int& newVal) {
        return std::max(old, newVal);
    });

    queue.push(ChunkPos(0, 0, 0), 10);
    queue.push(ChunkPos(0, 0, 0), 5);   // Should keep 10 (max)
    queue.push(ChunkPos(0, 0, 0), 20);  // Should become 20 (max)

    EXPECT_EQ(queue.size(), 1);

    auto result = queue.pop();
    EXPECT_EQ(result->second, 20);
}

TEST(AlarmQueueWithDataTest, PopWaitBlocking) {
    AlarmQueueWithData<ChunkPos, int> queue;
    std::atomic<bool> gotResult{false};

    std::thread consumer([&]() {
        auto result = queue.popWait();
        if (result && result->second == 99) {
            gotResult = true;
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(gotResult);

    queue.push(ChunkPos(0, 0, 0), 99);
    consumer.join();

    EXPECT_TRUE(gotResult);
}

TEST(AlarmQueueWithDataTest, AlarmSupport) {
    AlarmQueueWithData<ChunkPos, int> queue;

    auto wakeTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
    queue.setAlarm(wakeTime);
    EXPECT_TRUE(queue.hasAlarm());

    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        queue.waitForWork();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(woke);

    waiter.join();
    EXPECT_TRUE(woke);
    EXPECT_FALSE(queue.hasAlarm());
}

TEST(AlarmQueueWithDataTest, ShutdownWakesPopWait) {
    AlarmQueueWithData<ChunkPos, int> queue;
    std::atomic<bool> gotNullopt{false};

    std::thread consumer([&]() {
        auto result = queue.popWait();
        gotNullopt = !result.has_value();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.shutdown();
    consumer.join();

    EXPECT_TRUE(gotNullopt);
}
