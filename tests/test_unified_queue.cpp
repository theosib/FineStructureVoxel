#include <gtest/gtest.h>
#include "finevox/core/queue.hpp"
#include "finevox/core/keyed_queue.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include "finevox/core/position.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>

using namespace finevox;

// ============================================================================
// Queue<T> Tests
// ============================================================================

TEST(QueueTest, EmptyOnConstruction) {
    Queue<int> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_EQ(q.capacity(), 0u);
    EXPECT_FALSE(q.isFull());
    EXPECT_FALSE(q.isShutdown());
    EXPECT_FALSE(q.hasAlarm());
    EXPECT_FALSE(q.isAttached());
}

TEST(QueueTest, PushAndTryPop) {
    Queue<int> q;

    EXPECT_TRUE(q.push(42));
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(q.size(), 1u);

    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, 42);

    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(QueueTest, TryPopEmptyReturnsNullopt) {
    Queue<int> q;
    auto val = q.tryPop();
    EXPECT_FALSE(val.has_value());
}

TEST(QueueTest, FIFOOrder) {
    Queue<int> q;
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));

    EXPECT_EQ(*q.tryPop(), 1);
    EXPECT_EQ(*q.tryPop(), 2);
    EXPECT_EQ(*q.tryPop(), 3);
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(QueueTest, MoveSemantics) {
    Queue<std::string> q;
    std::string s = "hello";
    EXPECT_TRUE(q.push(std::move(s)));

    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "hello");
}

TEST(QueueTest, PushBatchVector) {
    Queue<int> q;
    std::vector<int> items = {10, 20, 30, 40};
    EXPECT_TRUE(q.pushBatch(std::move(items)));

    EXPECT_EQ(q.size(), 4u);
    EXPECT_EQ(*q.tryPop(), 10);
    EXPECT_EQ(*q.tryPop(), 20);
    EXPECT_EQ(*q.tryPop(), 30);
    EXPECT_EQ(*q.tryPop(), 40);
}

TEST(QueueTest, PushBatchIterators) {
    Queue<int> q;
    std::vector<int> items = {5, 6, 7};
    EXPECT_TRUE(q.pushBatch(items.begin(), items.end()));

    EXPECT_EQ(q.size(), 3u);
    EXPECT_EQ(*q.tryPop(), 5);
    EXPECT_EQ(*q.tryPop(), 6);
    EXPECT_EQ(*q.tryPop(), 7);
}

TEST(QueueTest, DrainAll) {
    Queue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);

    auto items = q.drainAll();
    EXPECT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0], 1);
    EXPECT_EQ(items[1], 2);
    EXPECT_EQ(items[2], 3);
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, DrainAllEmpty) {
    Queue<int> q;
    auto items = q.drainAll();
    EXPECT_TRUE(items.empty());
}

TEST(QueueTest, DrainUpTo) {
    Queue<int> q;
    q.push(1);
    q.push(2);
    q.push(3);
    q.push(4);
    q.push(5);

    auto batch1 = q.drainUpTo(3);
    EXPECT_EQ(batch1.size(), 3u);
    EXPECT_EQ(batch1[0], 1);
    EXPECT_EQ(batch1[1], 2);
    EXPECT_EQ(batch1[2], 3);

    auto batch2 = q.drainUpTo(10);
    EXPECT_EQ(batch2.size(), 2u);
    EXPECT_EQ(batch2[0], 4);
    EXPECT_EQ(batch2[1], 5);

    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, Clear) {
    Queue<int> q;
    q.push(1);
    q.push(2);
    q.setAlarm(std::chrono::steady_clock::now() + std::chrono::hours(1));

    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.hasAlarm());
}

// --- Capacity & Backpressure tests ---

TEST(QueueTest, BoundedCapacityNonBlockingBackpressure) {
    Queue<int> q(3);
    EXPECT_EQ(q.capacity(), 3u);
    EXPECT_FALSE(q.isFull());

    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_FALSE(q.isFull());
    EXPECT_TRUE(q.push(30));
    EXPECT_TRUE(q.isFull());
    EXPECT_EQ(q.size(), 3u);

    // Next push must fail without enqueuing
    EXPECT_FALSE(q.push(40));
    int mv = 50;
    EXPECT_FALSE(q.push(std::move(mv)));
    EXPECT_EQ(q.size(), 3u);

    // Popping frees one slot
    EXPECT_EQ(*q.tryPop(), 10);
    EXPECT_FALSE(q.isFull());
    EXPECT_TRUE(q.push(40));
    EXPECT_TRUE(q.isFull());

    EXPECT_EQ(*q.tryPop(), 20);
    EXPECT_EQ(*q.tryPop(), 30);
    EXPECT_EQ(*q.tryPop(), 40);
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, PushBatchRespectsCapacity) {
    Queue<int> q(3);
    EXPECT_TRUE(q.pushBatch(std::vector<int>{1, 2}));
    EXPECT_EQ(q.size(), 2u);

    // Batch of 2 would exceed capacity 3 -> rejected without enqueuing
    EXPECT_FALSE(q.pushBatch(std::vector<int>{3, 4}));
    EXPECT_EQ(q.size(), 2u);

    // Batch of 1 fits
    EXPECT_TRUE(q.pushBatch(std::vector<int>{3}));
    EXPECT_TRUE(q.isFull());

    // Any further batch when full is rejected
    EXPECT_FALSE(q.pushBatch(std::vector<int>{4}));
}

TEST(QueueTest, PushBlockingWaitsForConsumerPop) {
    Queue<int> q(2);
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.isFull());

    std::atomic<bool> thirdPushed{false};
    std::thread producer([&]() {
        bool ok = q.pushBlocking(3);
        EXPECT_TRUE(ok);
        thirdPushed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    EXPECT_FALSE(thirdPushed.load());

    // Pop one item to unblock producer
    EXPECT_EQ(*q.tryPop(), 1);
    producer.join();
    EXPECT_TRUE(thirdPushed.load());
    EXPECT_EQ(*q.tryPop(), 2);
    EXPECT_EQ(*q.tryPop(), 3);
}

TEST(QueueTest, PushBlockingUnblocksOnShutdown) {
    Queue<int> q(1);
    EXPECT_TRUE(q.push(1));

    std::atomic<bool> pushResult{true};
    std::thread producer([&]() {
        pushResult = q.pushBlocking(2);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    q.shutdown();
    producer.join();

    EXPECT_FALSE(pushResult.load());
}

TEST(QueueTest, PushBlockingUnblocksOnSetCapacity) {
    Queue<int> q(1);
    EXPECT_TRUE(q.push(1));

    std::atomic<bool> pushResult{false};
    std::thread producer([&]() {
        pushResult = q.pushBlocking(2);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EXPECT_FALSE(pushResult.load());

    q.setCapacity(2);
    producer.join();
    EXPECT_TRUE(pushResult.load());
    EXPECT_EQ(q.size(), 2u);
}

// --- Alarm tests ---

TEST(QueueTest, AlarmSetAndClear) {
    Queue<int> q;
    EXPECT_FALSE(q.hasAlarm());

    q.setAlarm(std::chrono::steady_clock::now() + std::chrono::hours(1));
    EXPECT_TRUE(q.hasAlarm());

    q.resetAlarm();
    EXPECT_FALSE(q.hasAlarm());
}

TEST(QueueTest, AlarmWakesWaitForWork) {
    Queue<int> q;
    std::atomic<bool> woke{false};

    q.setAlarm(std::chrono::steady_clock::now() + std::chrono::milliseconds(30));

    std::thread waiter([&]() {
        q.waitForWork();
        woke = true;
    });

    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(QueueTest, EarlierAlarmUpdatesDeadlineAndLaterAlarmDoesNotOverwrite) {
    Queue<int> q;
    auto now = std::chrono::steady_clock::now();

    // Schedule a far-future alarm (500ms), then an earlier alarm (40ms), then a later one (400ms)
    q.setAlarm(now + std::chrono::milliseconds(500));
    q.setAlarm(now + std::chrono::milliseconds(40));
    q.setAlarm(now + std::chrono::milliseconds(400)); // Later — must NOT overwrite 40ms

    EXPECT_TRUE(q.hasAlarm());

    auto start = std::chrono::steady_clock::now();
    q.waitForWork();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // Should wake near 40ms, well below 200ms
    EXPECT_LT(elapsed.count(), 200);
}

TEST(QueueTest, EarlierAlarmWakesAlreadyWaitingThreadEarly) {
    Queue<int> q;
    auto start = std::chrono::steady_clock::now();
    q.setAlarm(start + std::chrono::seconds(5));

    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        q.waitForWork();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke.load());

    // Now set an earlier alarm 25ms from now; waiter should wake up promptly
    q.setAlarm(std::chrono::steady_clock::now() + std::chrono::milliseconds(25));
    waiter.join();

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    EXPECT_TRUE(woke.load());
    EXPECT_LT(elapsed.count(), 500);
}

// --- Shutdown tests ---

TEST(QueueTest, ShutdownWakesWaiter) {
    Queue<int> q;
    std::atomic<bool> result{true};

    std::thread waiter([&]() {
        result = q.waitForWork();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    q.shutdown();
    waiter.join();

    EXPECT_FALSE(result); // waitForWork returns false on shutdown
    EXPECT_TRUE(q.isShutdown());
}

TEST(QueueTest, PushAfterShutdownDrops) {
    Queue<int> q;
    q.shutdown();

    EXPECT_FALSE(q.push(42));
    EXPECT_TRUE(q.empty());
}

TEST(QueueTest, TryPopWorksAfterShutdown) {
    Queue<int> q;
    q.push(1);
    q.push(2);

    q.shutdown();

    // Can still drain remaining items
    EXPECT_EQ(*q.tryPop(), 1);
    EXPECT_EQ(*q.tryPop(), 2);
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(QueueTest, ResetShutdown) {
    Queue<int> q;
    q.shutdown();
    EXPECT_TRUE(q.isShutdown());

    q.resetShutdown();
    EXPECT_FALSE(q.isShutdown());

    // Should accept pushes again
    EXPECT_TRUE(q.push(42));
    EXPECT_EQ(*q.tryPop(), 42);
}

// --- WaitForWork & PopWait tests ---

TEST(QueueTest, WaitForWorkReturnsTrueOnPush) {
    Queue<int> q;
    std::atomic<bool> result{false};

    std::thread waiter([&]() {
        result = q.waitForWork();
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.push(42);

    waiter.join();
    EXPECT_TRUE(result);
    EXPECT_EQ(*q.tryPop(), 42);
}

TEST(QueueTest, PopWaitBlocksUntilDataAndReturnsBeforeShutdown) {
    Queue<int> q;
    q.push(1);
    q.push(2);
    q.shutdown();

    EXPECT_EQ(*q.popWait(), 1);
    EXPECT_EQ(*q.popWait(), 2);
    EXPECT_FALSE(q.popWait().has_value());
}

TEST(QueueTest, WaitForWorkWithTimeout) {
    Queue<int> q;

    auto start = std::chrono::steady_clock::now();
    bool result = q.waitForWork(std::chrono::milliseconds(50));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_TRUE(result); // Not shutdown
    EXPECT_GE(elapsed.count(), 40);
}

TEST(QueueTest, WaitForWorkWithTimeoutReturnsEarlyOnPush) {
    Queue<int> q;
    std::atomic<bool> result{false};

    std::thread waiter([&]() {
        result = q.waitForWork(std::chrono::milliseconds(5000));
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    q.push(99);

    waiter.join();
    EXPECT_TRUE(result);
    EXPECT_EQ(*q.tryPop(), 99);
}

// --- WakeSignal attachment tests ---

TEST(QueueTest, AttachDetach) {
    Queue<int> q;
    WakeSignal ws;

    EXPECT_FALSE(q.isAttached());

    q.attach(&ws);
    EXPECT_TRUE(q.isAttached());

    q.detach();
    EXPECT_FALSE(q.isAttached());
}

TEST(QueueTest, PushSignalsAttachedWakeSignal) {
    Queue<int> q;
    WakeSignal ws;
    q.attach(&ws);

    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        ws.wait();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    q.push(42);
    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(QueueTest, ShutdownSignalsAttachedWakeSignal) {
    Queue<int> q;
    WakeSignal ws;
    q.attach(&ws);

    std::atomic<bool> woke{false};
    std::thread waiter([&]() {
        ws.wait();
        woke = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    q.shutdown();
    waiter.join();
    EXPECT_TRUE(woke);
}

TEST(QueueTest, AttachToNonEmptyQueueSignalsImmediately) {
    Queue<int> q;
    WakeSignal ws;

    q.push(42);
    q.attach(&ws);

    bool result = ws.waitFor(std::chrono::milliseconds(100));
    EXPECT_TRUE(result);
}

TEST(QueueTest, ConcurrentPushes) {
    Queue<int> q;
    constexpr int numThreads = 4;
    constexpr int itemsPerThread = 100;

    std::vector<std::thread> producers;
    for (int t = 0; t < numThreads; ++t) {
        producers.emplace_back([&q, t]() {
            for (int i = 0; i < itemsPerThread; ++i) {
                q.push(t * itemsPerThread + i);
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(q.size(), static_cast<size_t>(numThreads * itemsPerThread));

    auto all = q.drainAll();
    EXPECT_EQ(all.size(), static_cast<size_t>(numThreads * itemsPerThread));
}

// ============================================================================
// KeyedQueue<K, D> Tests
// ============================================================================

TEST(KeyedQueueTest, EmptyOnConstruction) {
    KeyedQueue<int, std::string> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
    EXPECT_FALSE(q.isShutdown());
    EXPECT_FALSE(q.hasAlarm());
    EXPECT_FALSE(q.isAttached());
    EXPECT_FALSE(q.hasPriorityExtractor());
}

TEST(KeyedQueueTest, PushAndTryPop) {
    KeyedQueue<int, std::string> q;

    bool isNew = q.push(1, "hello");
    EXPECT_TRUE(isNew);
    EXPECT_EQ(q.size(), 1u);
    EXPECT_TRUE(q.contains(1));

    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->first, 1);
    EXPECT_EQ(val->second, "hello");
    EXPECT_TRUE(q.empty());
}

TEST(KeyedQueueTest, TryPopEmptyReturnsNullopt) {
    KeyedQueue<int, std::string> q;
    EXPECT_FALSE(q.tryPop().has_value());
}

TEST(KeyedQueueTest, FIFOOrder) {
    KeyedQueue<int, std::string> q;
    q.push(1, "a");
    q.push(2, "b");
    q.push(3, "c");

    auto v1 = q.tryPop();
    auto v2 = q.tryPop();
    auto v3 = q.tryPop();

    EXPECT_EQ(v1->first, 1);
    EXPECT_EQ(v2->first, 2);
    EXPECT_EQ(v3->first, 3);
}

TEST(KeyedQueueTest, DeduplicationDefaultMerge) {
    KeyedQueue<int, std::string> q;

    bool first = q.push(1, "old");
    EXPECT_TRUE(first);

    bool second = q.push(1, "new");
    EXPECT_FALSE(second); // Merged, not newly added

    EXPECT_EQ(q.size(), 1u);

    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->second, "new"); // Default merge: replace with incoming
}

TEST(KeyedQueueTest, CustomMergeFunction) {
    KeyedQueue<std::string, int> q([](const int& existing, const int& incoming) {
        return existing + incoming;
    });

    q.push("score", 10);
    q.push("score", 5);
    q.push("score", 3);

    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->first, "score");
    EXPECT_EQ(val->second, 18);
}

TEST(KeyedQueueTest, DeduplicationPreservesOrder) {
    KeyedQueue<int, std::string> q;

    q.push(1, "a");
    q.push(2, "b");
    q.push(3, "c");
    q.push(2, "b_updated");

    EXPECT_EQ(q.size(), 3u);

    EXPECT_EQ(q.tryPop()->first, 1);
    EXPECT_EQ(q.tryPop()->first, 2);
    EXPECT_EQ(q.tryPop()->first, 3);
}

TEST(KeyedQueueTest, GetDataAndContains) {
    KeyedQueue<int, std::string> q;

    q.push(1, "hello");
    q.push(2, "world");

    EXPECT_TRUE(q.contains(1));
    EXPECT_FALSE(q.contains(3));

    auto d1 = q.getData(1);
    ASSERT_TRUE(d1.has_value());
    EXPECT_EQ(*d1, "hello");

    auto d3 = q.getData(3);
    EXPECT_FALSE(d3.has_value());
}

// --- Priority ordering tests ---

namespace {
struct RebuildReq {
    int priority = 100;
    int version = 1;
};
}  // namespace

TEST(KeyedQueueTest, PriorityOrderingAndTieBreaking) {
    KeyedQueue<std::string, RebuildReq> q(
        [](const RebuildReq& existing, const RebuildReq& incoming) {
            return RebuildReq{
                std::min(existing.priority, incoming.priority),
                incoming.version
            };
        },
        [](const RebuildReq& r) { return r.priority; }
    );

    EXPECT_TRUE(q.hasPriorityExtractor());

    q.push("chunk_bg", RebuildReq{1000, 1});
    q.push("chunk_norm1", RebuildReq{100, 1});
    q.push("chunk_imm1", RebuildReq{0, 1});
    q.push("chunk_norm2", RebuildReq{100, 1});
    q.push("chunk_imm2", RebuildReq{0, 1});

    EXPECT_EQ(q.tryPop()->first, "chunk_imm1");
    EXPECT_EQ(q.tryPop()->first, "chunk_imm2");
    EXPECT_EQ(q.tryPop()->first, "chunk_norm1");
    EXPECT_EQ(q.tryPop()->first, "chunk_norm2");
    EXPECT_EQ(q.tryPop()->first, "chunk_bg");
    EXPECT_TRUE(q.empty());
}

TEST(KeyedQueueTest, PriorityUpgradeViaMergeRespected) {
    KeyedQueue<std::string, RebuildReq> q(
        [](const RebuildReq& existing, const RebuildReq& incoming) {
            return RebuildReq{
                std::min(existing.priority, incoming.priority),
                incoming.version
            };
        },
        [](const RebuildReq& r) { return r.priority; }
    );

    q.push("chunk_A", RebuildReq{1000, 1});
    q.push("chunk_B", RebuildReq{100, 1});

    // Upgrade chunk_A to immediate (0)
    EXPECT_FALSE(q.push("chunk_A", RebuildReq{0, 2}));
    EXPECT_EQ(q.size(), 2u);

    auto first = q.tryPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->first, "chunk_A");
    EXPECT_EQ(first->second.priority, 0);
    EXPECT_EQ(first->second.version, 2);

    auto second = q.tryPop();
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(second->first, "chunk_B");
}

TEST(KeyedQueueTest, DrainAllAndDrainUpToRespectPriorityOrder) {
    KeyedQueue<int, RebuildReq> q(
        [](const RebuildReq& a, const RebuildReq& b) {
            return RebuildReq{std::min(a.priority, b.priority), b.version};
        },
        [](const RebuildReq& r) { return r.priority; }
    );

    q.push(1, RebuildReq{1000, 1});
    q.push(2, RebuildReq{100, 1});
    q.push(3, RebuildReq{0, 1});
    q.push(4, RebuildReq{100, 1});

    auto top2 = q.drainUpTo(2);
    ASSERT_EQ(top2.size(), 2u);
    EXPECT_EQ(top2[0].first, 3);
    EXPECT_EQ(top2[1].first, 2);

    auto rest = q.drainAll();
    ASSERT_EQ(rest.size(), 2u);
    EXPECT_EQ(rest[0].first, 4);
    EXPECT_EQ(rest[1].first, 1);
}

TEST(KeyedQueueTest, BoundedCapacityAndPushBlocking) {
    KeyedQueue<int, std::string> q(2);
    EXPECT_EQ(q.capacity(), 2u);
    EXPECT_TRUE(q.push(1, "a"));
    EXPECT_TRUE(q.push(2, "b"));
    EXPECT_TRUE(q.isFull());

    // New key rejected when full
    EXPECT_FALSE(q.push(3, "c"));
    // Existing key still merges!
    EXPECT_FALSE(q.push(1, "a_updated"));
    EXPECT_EQ(*q.getData(1), "a_updated");

    // pushBlocking on existing key merges without blocking
    EXPECT_TRUE(q.pushBlocking(2, "b_updated"));
    EXPECT_EQ(*q.getData(2), "b_updated");

    // pushBlocking on new key blocks until pop
    std::atomic<bool> thirdPushed{false};
    std::thread producer([&]() {
        EXPECT_TRUE(q.pushBlocking(3, "c"));
        thirdPushed = true;
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    EXPECT_FALSE(thirdPushed.load());

    auto first = q.tryPop();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->first, 1);

    producer.join();
    EXPECT_TRUE(thirdPushed.load());
    EXPECT_EQ(q.size(), 2u);
}

TEST(KeyedQueueTest, EarlierAlarmUpdatesDeadlineAndWakesWaiter) {
    KeyedQueue<int, std::string> q;
    auto now = std::chrono::steady_clock::now();

    q.setAlarm(now + std::chrono::milliseconds(500));
    q.setAlarm(now + std::chrono::milliseconds(35));
    q.setAlarm(now + std::chrono::milliseconds(400));

    auto start = std::chrono::steady_clock::now();
    q.waitForWork();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    EXPECT_LT(elapsed.count(), 200);
}

TEST(KeyedQueueTest, ConcurrentPushesWithMerge) {
    KeyedQueue<int, int> q([](const int& existing, const int& incoming) {
        return existing + incoming;
    });

    constexpr int numThreads = 4;
    constexpr int pushesPerThread = 100;

    std::vector<std::thread> producers;
    for (int t = 0; t < numThreads; ++t) {
        producers.emplace_back([&q]() {
            for (int i = 0; i < pushesPerThread; ++i) {
                q.push(1, 1);
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    EXPECT_EQ(q.size(), 1u);
    auto val = q.tryPop();
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val->second, numThreads * pushesPerThread);
}

// ============================================================================
// MeshRebuildQueue Tests (with priority and version support)
// ============================================================================

TEST(MeshRebuildQueueTest, BasicPushPop) {
    MeshRebuildQueue queue = createMeshRebuildQueue();

    queue.push(ChunkPos(1, 2, 3), MeshRebuildRequest::normal());
    EXPECT_EQ(queue.size(), 1u);

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first, ChunkPos(1, 2, 3));
    EXPECT_EQ(result->second.priority, 100u);
}

TEST(MeshRebuildQueueTest, PriorityMerging) {
    MeshRebuildQueue queue = createMeshRebuildQueue();

    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::normal());
    EXPECT_EQ(queue.size(), 1u);

    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::immediate());
    EXPECT_EQ(queue.size(), 1u);

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->second.priority, 0u);
}

TEST(MeshRebuildQueueTest, LODMerging) {
    MeshRebuildQueue queue = createMeshRebuildQueue();

    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::normal(LODLevel::LOD0));
    queue.push(ChunkPos(0, 0, 0), MeshRebuildRequest::background(LODLevel::LOD2));

    auto result = queue.pop();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->second.priority, 100u);
    EXPECT_EQ(result->second.lodRequest.buildLevel(), LODLevel::LOD2);
}

TEST(MeshRebuildQueueTest, PriorityOrderingPopsImmediateBeforeBackground) {
    MeshRebuildQueue queue = createMeshRebuildQueue();

    queue.push(ChunkPos(10, 0, 0), MeshRebuildRequest::background());
    queue.push(ChunkPos(5, 0, 0), MeshRebuildRequest::normal());
    queue.push(ChunkPos(1, 0, 0), MeshRebuildRequest::immediate());

    auto first = queue.pop();
    auto second = queue.pop();
    auto third = queue.pop();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    ASSERT_TRUE(third.has_value());

    EXPECT_EQ(first->first, ChunkPos(1, 0, 0));
    EXPECT_EQ(second->first, ChunkPos(5, 0, 0));
    EXPECT_EQ(third->first, ChunkPos(10, 0, 0));
}
