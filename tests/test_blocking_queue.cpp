#include <gtest/gtest.h>
#include "finevox/blocking_queue.hpp"
#include "finevox/mesh_rebuild_queue.hpp"
#include "finevox/position.hpp"
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
// MeshRebuildQueue alias test (ensure backwards compatibility)
// ============================================================================

TEST(MeshRebuildQueueTest, AliasWorksCorrectly) {
    // MeshRebuildQueue is now an alias for BlockingQueue<ChunkPos>
    MeshRebuildQueue queue;

    queue.push(ChunkPos(1, 2, 3));
    EXPECT_EQ(queue.size(), 1);

    auto pos = queue.pop();
    ASSERT_TRUE(pos.has_value());
    EXPECT_EQ(*pos, ChunkPos(1, 2, 3));
}
