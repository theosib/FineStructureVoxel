#include <gtest/gtest.h>
#include "finevox/coalescing_queue.hpp"
#include "finevox/position.hpp"
#include <thread>
#include <atomic>

using namespace finevox;

// ============================================================================
// Basic CoalescingQueue tests
// ============================================================================

TEST(CoalescingQueueTest, EmptyQueue) {
    CoalescingQueue<int> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.pop().has_value());
}

TEST(CoalescingQueueTest, PushAndPop) {
    CoalescingQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_EQ(queue.size(), 3);

    auto v1 = queue.pop();
    auto v2 = queue.pop();
    auto v3 = queue.pop();
    auto v4 = queue.pop();

    EXPECT_TRUE(v1.has_value());
    EXPECT_TRUE(v2.has_value());
    EXPECT_TRUE(v3.has_value());
    EXPECT_FALSE(v4.has_value());

    EXPECT_EQ(*v1, 1);
    EXPECT_EQ(*v2, 2);
    EXPECT_EQ(*v3, 3);
}

TEST(CoalescingQueueTest, CoalescesDuplicates) {
    CoalescingQueue<int> queue;

    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_FALSE(queue.push(1));  // Duplicate, should return false
    EXPECT_TRUE(queue.push(3));
    EXPECT_FALSE(queue.push(2));  // Duplicate

    EXPECT_EQ(queue.size(), 3);  // Only 3 unique elements

    // Order should be preserved: 1, 2, 3
    EXPECT_EQ(*queue.pop(), 1);
    EXPECT_EQ(*queue.pop(), 2);
    EXPECT_EQ(*queue.pop(), 3);
}

TEST(CoalescingQueueTest, Contains) {
    CoalescingQueue<int> queue;

    queue.push(1);
    queue.push(2);

    EXPECT_TRUE(queue.contains(1));
    EXPECT_TRUE(queue.contains(2));
    EXPECT_FALSE(queue.contains(3));

    queue.pop();
    EXPECT_FALSE(queue.contains(1));
    EXPECT_TRUE(queue.contains(2));
}

TEST(CoalescingQueueTest, Clear) {
    CoalescingQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    queue.clear();

    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.contains(1));
}

TEST(CoalescingQueueTest, Remove) {
    CoalescingQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_TRUE(queue.remove(2));
    EXPECT_FALSE(queue.remove(2));  // Already removed
    EXPECT_FALSE(queue.remove(4));  // Never existed

    EXPECT_EQ(queue.size(), 2);
    EXPECT_EQ(*queue.pop(), 1);
    EXPECT_EQ(*queue.pop(), 3);
}

TEST(CoalescingQueueTest, CanRepushAfterPop) {
    CoalescingQueue<int> queue;

    queue.push(1);
    queue.pop();

    EXPECT_TRUE(queue.push(1));  // Should succeed after pop
    EXPECT_EQ(queue.size(), 1);
}

TEST(CoalescingQueueTest, WorksWithChunkPos) {
    CoalescingQueue<ChunkPos> queue;

    queue.push(ChunkPos(0, 0, 0));
    queue.push(ChunkPos(1, 2, 3));
    queue.push(ChunkPos(0, 0, 0));  // Duplicate

    EXPECT_EQ(queue.size(), 2);
    EXPECT_TRUE(queue.contains(ChunkPos(0, 0, 0)));
    EXPECT_TRUE(queue.contains(ChunkPos(1, 2, 3)));
}

// ============================================================================
// Thread-safe CoalescingQueueTS tests
// ============================================================================

TEST(CoalescingQueueTSTest, BasicOperations) {
    CoalescingQueueTS<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(1);  // Duplicate

    EXPECT_EQ(queue.size(), 2);
    EXPECT_EQ(*queue.pop(), 1);
    EXPECT_EQ(*queue.pop(), 2);
}

TEST(CoalescingQueueTSTest, PopBatch) {
    CoalescingQueueTS<int> queue;

    for (int i = 0; i < 10; ++i) {
        queue.push(i);
    }

    auto batch = queue.popBatch(5);
    EXPECT_EQ(batch.size(), 5);
    EXPECT_EQ(queue.size(), 5);

    // Verify order
    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(batch[i], i);
    }
}

TEST(CoalescingQueueTSTest, PopBatchMoreThanAvailable) {
    CoalescingQueueTS<int> queue;

    queue.push(1);
    queue.push(2);

    auto batch = queue.popBatch(10);
    EXPECT_EQ(batch.size(), 2);
    EXPECT_TRUE(queue.empty());
}

TEST(CoalescingQueueTSTest, ConcurrentPush) {
    CoalescingQueueTS<int> queue;
    std::atomic<int> successCount{0};

    // Multiple threads try to push overlapping values
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&queue, &successCount, t]() {
            for (int i = 0; i < 100; ++i) {
                if (queue.push(i)) {
                    ++successCount;
                }
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    // Should have exactly 100 unique items
    EXPECT_EQ(queue.size(), 100);
    // Exactly 100 pushes should have succeeded (one per unique value)
    EXPECT_EQ(successCount.load(), 100);
}

TEST(CoalescingQueueTSTest, ConcurrentPushPop) {
    CoalescingQueueTS<int> queue;
    std::atomic<int> totalPopped{0};
    std::atomic<bool> done{false};

    // Producer thread
    std::thread producer([&queue, &done]() {
        for (int i = 0; i < 1000; ++i) {
            queue.push(i % 100);  // Push with duplicates
        }
        done = true;
    });

    // Consumer thread
    std::thread consumer([&queue, &done, &totalPopped]() {
        while (!done || !queue.empty()) {
            if (queue.pop().has_value()) {
                ++totalPopped;
            }
        }
    });

    producer.join();
    consumer.join();

    // All items should be popped and queue should be empty
    EXPECT_TRUE(queue.empty());
}

// ============================================================================
// CoalescingQueueWithData tests
// ============================================================================

TEST(CoalescingQueueWithDataTest, DefaultMergeReplacesData) {
    CoalescingQueueWithData<int, std::string> queue;

    queue.push(1, "first");
    queue.push(1, "second");  // Should replace

    auto data = queue.getData(1);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(*data, "second");
}

TEST(CoalescingQueueWithDataTest, CustomMergeFunction) {
    // Merge by keeping the maximum value
    CoalescingQueueWithData<int, int> queue(
        [](const int& existing, const int& newVal) { return std::max(existing, newVal); }
    );

    queue.push(1, 5);
    queue.push(1, 3);  // Should keep 5
    queue.push(1, 10); // Should update to 10

    auto data = queue.getData(1);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(*data, 10);
}

TEST(CoalescingQueueWithDataTest, PopReturnsKeyAndData) {
    CoalescingQueueWithData<int, std::string> queue;

    queue.push(1, "one");
    queue.push(2, "two");

    auto item1 = queue.pop();
    ASSERT_TRUE(item1.has_value());
    EXPECT_EQ(item1->first, 1);
    EXPECT_EQ(item1->second, "one");

    auto item2 = queue.pop();
    ASSERT_TRUE(item2.has_value());
    EXPECT_EQ(item2->first, 2);
    EXPECT_EQ(item2->second, "two");
}

TEST(CoalescingQueueWithDataTest, AccumulatingMerge) {
    // Merge by adding values
    CoalescingQueueWithData<int, int> queue(
        [](const int& existing, const int& newVal) { return existing + newVal; }
    );

    queue.push(1, 10);
    queue.push(1, 20);
    queue.push(1, 30);

    EXPECT_EQ(queue.size(), 1);

    auto item = queue.pop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(item->first, 1);
    EXPECT_EQ(item->second, 60);  // 10 + 20 + 30
}

TEST(CoalescingQueueWithDataTest, GetDataForNonexistent) {
    CoalescingQueueWithData<int, std::string> queue;

    auto data = queue.getData(999);
    EXPECT_FALSE(data.has_value());
}

TEST(CoalescingQueueWithDataTest, ClearRemovesAllData) {
    CoalescingQueueWithData<int, std::string> queue;

    queue.push(1, "one");
    queue.push(2, "two");

    queue.clear();

    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.getData(1).has_value());
    EXPECT_FALSE(queue.getData(2).has_value());
}

TEST(CoalescingQueueWithDataTest, WorksWithPositionTypes) {
    // Priority-based dirty chunk queue
    CoalescingQueueWithData<ChunkPos, int> dirtyChunks(
        [](const int& existing, const int& newPriority) { return std::max(existing, newPriority); }
    );

    ChunkPos pos(1, 2, 3);
    dirtyChunks.push(pos, 1);  // Low priority
    dirtyChunks.push(pos, 5);  // Higher priority
    dirtyChunks.push(pos, 3);  // Lower, should keep 5

    auto data = dirtyChunks.getData(pos);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(*data, 5);
}
