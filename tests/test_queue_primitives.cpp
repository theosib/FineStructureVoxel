#include <gtest/gtest.h>
#include "finevox/core/wake_signal.hpp"
#include "finevox/core/deprecated/simple_queue.hpp"
#include "finevox/core/deprecated/coalescing_queue.hpp"
#include "finevox/core/position.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace finevox;

// ============================================================================
// WakeSignal tests
// ============================================================================

TEST(WakeSignalTest, InitialState) {
    WakeSignal signal;
    EXPECT_FALSE(signal.isShutdown());
    EXPECT_FALSE(signal.hasDeadline());
}

TEST(WakeSignalTest, SignalWakesWaiter) {
    WakeSignal signal;
    std::atomic<bool> woke{false};

    std::thread waiter([&]() {
        signal.wait();
        woke = true;
    });

    // Give waiter time to block
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    // Signal should wake waiter
    signal.signal();
    waiter.join();

    EXPECT_TRUE(woke);
}

TEST(WakeSignalTest, ShutdownWakesWaiterAndReturnsFalse) {
    WakeSignal signal;
    std::atomic<bool> result{true};

    std::thread waiter([&]() {
        result = signal.wait();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    signal.requestShutdown();
    waiter.join();

    EXPECT_FALSE(result);  // wait() returns false on shutdown
    EXPECT_TRUE(signal.isShutdown());
}

TEST(WakeSignalTest, DeadlineWakesWaiter) {
    WakeSignal signal;
    std::atomic<bool> woke{false};

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    signal.setDeadline(deadline);
    EXPECT_TRUE(signal.hasDeadline());

    std::thread waiter([&]() {
        signal.wait();
        woke = true;
    });

    // Should not wake immediately
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    // Wait for deadline
    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(WakeSignalTest, ClearDeadline) {
    WakeSignal signal;

    signal.setDeadline(std::chrono::steady_clock::now() + std::chrono::hours(1));
    EXPECT_TRUE(signal.hasDeadline());

    signal.clearDeadline();
    EXPECT_FALSE(signal.hasDeadline());
}

TEST(WakeSignalTest, WaitForWithTimeout) {
    WakeSignal signal;

    auto start = std::chrono::steady_clock::now();
    bool result = signal.waitFor(std::chrono::milliseconds(50));
    auto end = std::chrono::steady_clock::now();

    EXPECT_TRUE(result);  // Not shutdown
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    EXPECT_GE(elapsed.count(), 40);  // Should have waited ~50ms
}

TEST(WakeSignalTest, Reset) {
    WakeSignal signal;

    signal.signal();
    signal.setDeadline(std::chrono::steady_clock::now() + std::chrono::hours(1));
    signal.requestShutdown();

    EXPECT_TRUE(signal.isShutdown());
    EXPECT_TRUE(signal.hasDeadline());

    signal.reset();

    EXPECT_FALSE(signal.isShutdown());
    EXPECT_FALSE(signal.hasDeadline());
}

TEST(WakeSignalTest, MultipleSignalsWakeMultipleWaiters) {
    // WakeSignal is designed for one consumer on multiple queues.
    // When multiple threads wait, each needs its own signal() call.
    WakeSignal signal;
    std::atomic<int> wokenCount{0};
    constexpr int numWaiters = 4;

    std::vector<std::thread> waiters;
    for (int i = 0; i < numWaiters; ++i) {
        waiters.emplace_back([&]() {
            signal.wait();
            ++wokenCount;
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(wokenCount.load(), 0);

    // Signal once per waiter (with small delays to let each wake up)
    for (int i = 0; i < numWaiters; ++i) {
        signal.signal();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }

    for (auto& t : waiters) {
        t.join();
    }

    EXPECT_EQ(wokenCount.load(), numWaiters);
}

// ============================================================================
// SimpleQueue tests
// ============================================================================

TEST(SimpleQueueTest, EmptyQueue) {
    SimpleQueue<int> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(SimpleQueueTest, PushAndPop) {
    SimpleQueue<int> queue;

    queue.push(42);
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
    EXPECT_TRUE(queue.empty());
}

TEST(SimpleQueueTest, FIFOOrder) {
    SimpleQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_EQ(*queue.tryPop(), 1);
    EXPECT_EQ(*queue.tryPop(), 2);
    EXPECT_EQ(*queue.tryPop(), 3);
}

TEST(SimpleQueueTest, NoDuplication) {
    // Unlike CoalescingQueue, SimpleQueue does NOT deduplicate
    SimpleQueue<int> queue;

    queue.push(42);
    queue.push(42);
    queue.push(42);

    EXPECT_EQ(queue.size(), 3);  // All three are queued
}

TEST(SimpleQueueTest, AttachAndSignal) {
    WakeSignal signal;
    SimpleQueue<int> queue;
    std::atomic<bool> woke{false};

    queue.attach(&signal);
    EXPECT_TRUE(queue.isAttached());

    std::thread waiter([&]() {
        signal.wait();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    // Push should signal
    queue.push(1);
    waiter.join();

    EXPECT_TRUE(woke);
}

TEST(SimpleQueueTest, AttachWithExistingItems) {
    WakeSignal signal;
    SimpleQueue<int> queue;

    queue.push(1);
    queue.push(2);

    // Attach should signal immediately because queue has items
    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        signal.wait();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.attach(&signal);  // Should signal

    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(SimpleQueueTest, Detach) {
    WakeSignal signal;
    SimpleQueue<int> queue;

    queue.attach(&signal);
    EXPECT_TRUE(queue.isAttached());

    queue.detach();
    EXPECT_FALSE(queue.isAttached());
}

TEST(SimpleQueueTest, Shutdown) {
    SimpleQueue<int> queue;

    EXPECT_FALSE(queue.isShutdown());

    queue.push(1);
    queue.shutdown();

    EXPECT_TRUE(queue.isShutdown());

    // Push after shutdown is silently dropped
    queue.push(2);
    EXPECT_EQ(queue.size(), 1);

    // Can still pop existing items
    auto item = queue.tryPop();
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(*item, 1);
}

TEST(SimpleQueueTest, Clear) {
    SimpleQueue<int> queue;

    queue.push(1);
    queue.push(2);
    queue.push(3);

    EXPECT_EQ(queue.size(), 3);

    queue.clear();
    EXPECT_TRUE(queue.empty());
}

TEST(SimpleQueueTest, MoveSemantics) {
    SimpleQueue<std::string> queue;

    std::string str = "hello";
    queue.push(std::move(str));

    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "hello");
}

TEST(SimpleQueueTest, ConcurrentPush) {
    SimpleQueue<int> queue;

    std::vector<std::thread> threads;
    const int numThreads = 4;
    const int pushesPerThread = 100;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&queue, t]() {
            for (int i = 0; i < pushesPerThread; ++i) {
                queue.push(t * 1000 + i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(queue.size(), numThreads * pushesPerThread);
}

// ============================================================================
// CoalescingQueue tests
// ============================================================================

TEST(CoalescingQueueTest, EmptyQueue) {
    CoalescingQueue<int, std::string> queue;
    EXPECT_TRUE(queue.empty());
    EXPECT_EQ(queue.size(), 0);
    EXPECT_FALSE(queue.tryPop().has_value());
}

TEST(CoalescingQueueTest, PushAndPop) {
    CoalescingQueue<int, std::string> queue;

    queue.push(1, "hello");
    EXPECT_FALSE(queue.empty());
    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, 1);
    EXPECT_EQ(result->second, "hello");
    EXPECT_TRUE(queue.empty());
}

TEST(CoalescingQueueTest, FIFOOrder) {
    CoalescingQueue<int, int> queue;

    queue.push(1, 100);
    queue.push(2, 200);
    queue.push(3, 300);

    auto first = queue.tryPop();
    auto second = queue.tryPop();
    auto third = queue.tryPop();

    EXPECT_EQ(first->first, 1);
    EXPECT_EQ(second->first, 2);
    EXPECT_EQ(third->first, 3);
}

TEST(CoalescingQueueTest, DeduplicationWithDefaultMerge) {
    // Default merge: replace with incoming
    CoalescingQueue<int, std::string> queue;

    EXPECT_TRUE(queue.push(1, "first"));    // New key
    EXPECT_FALSE(queue.push(1, "second"));  // Merged (replaced)

    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    EXPECT_EQ(result->second, "second");  // Should be replaced value
}

TEST(CoalescingQueueTest, CustomMergeFunction) {
    // Merge: keep maximum value
    CoalescingQueue<int, int> queue([](const int& existing, const int& incoming) {
        return std::max(existing, incoming);
    });

    queue.push(1, 10);
    queue.push(1, 5);   // Should keep 10
    queue.push(1, 20);  // Should become 20

    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    EXPECT_EQ(result->second, 20);
}

TEST(CoalescingQueueTest, Contains) {
    CoalescingQueue<int, std::string> queue;

    EXPECT_FALSE(queue.contains(1));

    queue.push(1, "value");
    EXPECT_TRUE(queue.contains(1));

    queue.tryPop();
    EXPECT_FALSE(queue.contains(1));
}

TEST(CoalescingQueueTest, GetData) {
    CoalescingQueue<int, std::string> queue;

    EXPECT_FALSE(queue.getData(1).has_value());

    queue.push(1, "hello");

    auto data = queue.getData(1);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(*data, "hello");
}

TEST(CoalescingQueueTest, AttachAndSignal) {
    WakeSignal signal;
    CoalescingQueue<int, int> queue;
    std::atomic<bool> woke{false};

    queue.attach(&signal);

    std::thread waiter([&]() {
        signal.wait();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    queue.push(1, 100);
    waiter.join();

    EXPECT_TRUE(woke);
}

TEST(CoalescingQueueTest, SignalsOnMerge) {
    WakeSignal signal;
    CoalescingQueue<int, int> queue;
    std::atomic<int> wakeCount{0};

    queue.attach(&signal);

    // First push
    queue.push(1, 100);

    // Second push (merge) should also signal
    std::thread waiter([&]() {
        signal.wait();
        ++wakeCount;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    queue.push(1, 200);  // Merge - should still signal

    waiter.join();
    EXPECT_GE(wakeCount.load(), 1);
}

TEST(CoalescingQueueTest, Shutdown) {
    CoalescingQueue<int, int> queue;

    queue.push(1, 100);
    queue.shutdown();

    EXPECT_TRUE(queue.isShutdown());

    // Push after shutdown returns false
    EXPECT_FALSE(queue.push(2, 200));
    EXPECT_EQ(queue.size(), 1);
}

TEST(CoalescingQueueTest, Clear) {
    CoalescingQueue<int, int> queue;

    queue.push(1, 100);
    queue.push(2, 200);
    queue.push(3, 300);

    queue.clear();
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.contains(1));
}

TEST(CoalescingQueueTest, WithChunkPos) {
    // Test with ChunkPos as key (common use case)
    CoalescingQueue<ChunkPos, int> queue([](const int& existing, const int& incoming) {
        return std::min(existing, incoming);  // Keep lower priority
    });

    queue.push(ChunkPos(1, 2, 3), 100);
    queue.push(ChunkPos(1, 2, 3), 50);  // Should become 50 (min)

    EXPECT_EQ(queue.size(), 1);

    auto result = queue.tryPop();
    EXPECT_EQ(result->first, ChunkPos(1, 2, 3));
    EXPECT_EQ(result->second, 50);
}

TEST(CoalescingQueueTest, ConcurrentPush) {
    CoalescingQueue<int, int> queue([](const int& a, const int& b) {
        return std::max(a, b);
    });

    std::vector<std::thread> threads;
    const int numThreads = 4;
    const int keysPerThread = 25;

    // Each thread pushes unique keys
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&queue, t]() {
            for (int i = 0; i < keysPerThread; ++i) {
                queue.push(t * 100 + i, i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(queue.size(), numThreads * keysPerThread);
}

TEST(CoalescingQueueTest, ConcurrentPushSameKey) {
    CoalescingQueue<int, int> queue([](const int& a, const int& b) {
        return std::max(a, b);
    });

    std::vector<std::thread> threads;
    const int numThreads = 4;
    const int pushesPerThread = 100;

    // All threads push to the same key
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&queue, t]() {
            for (int i = 0; i < pushesPerThread; ++i) {
                queue.push(0, t * 1000 + i);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Should have coalesced to a single entry
    EXPECT_EQ(queue.size(), 1);
}

// ============================================================================
// Integration: Multiple queues sharing one WakeSignal
// ============================================================================

TEST(MultiQueueTest, MultipleQueuesOneSignal) {
    WakeSignal signal;
    SimpleQueue<int> queue1;
    SimpleQueue<std::string> queue2;
    CoalescingQueue<int, int> queue3;

    queue1.attach(&signal);
    queue2.attach(&signal);
    queue3.attach(&signal);

    std::atomic<int> wakeCount{0};
    std::atomic<bool> done{false};

    std::thread consumer([&]() {
        while (!done) {
            if (signal.waitFor(std::chrono::milliseconds(10))) {
                ++wakeCount;
            }
        }
    });

    // Small delay to ensure consumer is waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Push to different queues
    queue1.push(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    queue2.push("hello");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    queue3.push(1, 100);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    done = true;
    signal.signal();  // Final wake to exit
    consumer.join();

    // Should have woken at least 3 times (once per push)
    EXPECT_GE(wakeCount.load(), 3);
}

TEST(MultiQueueTest, ConsumerDrainsAllQueues) {
    WakeSignal signal;
    SimpleQueue<int> intQueue;
    SimpleQueue<std::string> strQueue;

    intQueue.attach(&signal);
    strQueue.attach(&signal);

    // Push items to both queues
    intQueue.push(1);
    intQueue.push(2);
    strQueue.push("a");
    strQueue.push("b");

    // Consumer drains all after one wake
    signal.wait();

    std::vector<int> ints;
    while (auto item = intQueue.tryPop()) {
        ints.push_back(*item);
    }

    std::vector<std::string> strs;
    while (auto item = strQueue.tryPop()) {
        strs.push_back(*item);
    }

    EXPECT_EQ(ints.size(), 2);
    EXPECT_EQ(strs.size(), 2);
}
