#include <gtest/gtest.h>
#include "finevox/lru_cache.hpp"
#include <string>

using namespace finevox;

// ============================================================================
// Basic LRUCache tests
// ============================================================================

TEST(LRUCacheTest, EmptyCache) {
    LRUCache<int, std::string> cache(10);
    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.size(), 0);
    EXPECT_EQ(cache.capacity(), 10);
}

TEST(LRUCacheTest, PutAndGet) {
    LRUCache<int, std::string> cache(10);

    cache.put(1, "one");
    cache.put(2, "two");

    auto val1 = cache.get(1);
    auto val2 = cache.get(2);

    ASSERT_TRUE(val1.has_value());
    ASSERT_TRUE(val2.has_value());
    EXPECT_EQ(*val1, "one");
    EXPECT_EQ(*val2, "two");
}

TEST(LRUCacheTest, GetNonexistent) {
    LRUCache<int, std::string> cache(10);

    auto val = cache.get(999);
    EXPECT_FALSE(val.has_value());
}

TEST(LRUCacheTest, UpdateExisting) {
    LRUCache<int, std::string> cache(10);

    cache.put(1, "one");
    cache.put(1, "ONE");

    auto val = cache.get(1);
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(*val, "ONE");
    EXPECT_EQ(cache.size(), 1);
}

TEST(LRUCacheTest, Contains) {
    LRUCache<int, std::string> cache(10);

    cache.put(1, "one");

    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
}

TEST(LRUCacheTest, Remove) {
    LRUCache<int, std::string> cache(10);

    cache.put(1, "one");
    cache.put(2, "two");

    auto removed = cache.remove(1);
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(*removed, "one");

    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_EQ(cache.size(), 1);
}

TEST(LRUCacheTest, RemoveNonexistent) {
    LRUCache<int, std::string> cache(10);

    auto removed = cache.remove(999);
    EXPECT_FALSE(removed.has_value());
}

// ============================================================================
// Eviction tests
// ============================================================================

TEST(LRUCacheTest, EvictsLRU) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    EXPECT_TRUE(cache.full());

    // Add fourth item - should evict "one" (LRU)
    auto evicted = cache.put(4, "four");

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->first, 1);
    EXPECT_EQ(evicted->second, "one");

    EXPECT_FALSE(cache.contains(1));
    EXPECT_TRUE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
    EXPECT_TRUE(cache.contains(4));
}

TEST(LRUCacheTest, GetMovesToFront) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    // Access "one" - moves it to front
    cache.get(1);

    // Add fourth item - should evict "two" now (LRU after "one" was accessed)
    auto evicted = cache.put(4, "four");

    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(evicted->first, 2);

    EXPECT_TRUE(cache.contains(1));
    EXPECT_FALSE(cache.contains(2));
    EXPECT_TRUE(cache.contains(3));
    EXPECT_TRUE(cache.contains(4));
}

TEST(LRUCacheTest, TouchMovesToFront) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    // Touch "one" without getting value
    cache.touch(1);

    auto evicted = cache.put(4, "four");

    EXPECT_EQ(evicted->first, 2);  // "two" evicted, not "one"
}

TEST(LRUCacheTest, LeastAndMostRecentKey) {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    EXPECT_EQ(cache.mostRecentKey(), 3);
    EXPECT_EQ(cache.leastRecentKey(), 1);

    cache.get(1);

    EXPECT_EQ(cache.mostRecentKey(), 1);
    EXPECT_EQ(cache.leastRecentKey(), 2);
}

// ============================================================================
// Capacity change tests
// ============================================================================

TEST(LRUCacheTest, SetCapacitySmaller) {
    LRUCache<int, std::string> cache(5);

    for (int i = 0; i < 5; ++i) {
        cache.put(i, std::to_string(i));
    }

    auto evicted = cache.setCapacity(2);

    EXPECT_EQ(evicted.size(), 3);
    EXPECT_EQ(cache.size(), 2);
    EXPECT_EQ(cache.capacity(), 2);

    // Should have kept the 2 most recently added (3 and 4)
    EXPECT_TRUE(cache.contains(4));
    EXPECT_TRUE(cache.contains(3));
    EXPECT_FALSE(cache.contains(0));
}

TEST(LRUCacheTest, SetCapacityLarger) {
    LRUCache<int, std::string> cache(2);

    cache.put(1, "one");
    cache.put(2, "two");

    auto evicted = cache.setCapacity(10);

    EXPECT_TRUE(evicted.empty());
    EXPECT_EQ(cache.capacity(), 10);
    EXPECT_EQ(cache.size(), 2);
}

// ============================================================================
// Iteration tests
// ============================================================================

TEST(LRUCacheTest, ForEach) {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    std::vector<int> order;
    cache.forEach([&order](int key, const std::string&) {
        order.push_back(key);
    });

    // Should be in MRU order: 3, 2, 1
    ASSERT_EQ(order.size(), 3);
    EXPECT_EQ(order[0], 3);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 1);
}

// ============================================================================
// Clear tests
// ============================================================================

TEST(LRUCacheTest, Clear) {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    cache.clear();

    EXPECT_TRUE(cache.empty());
    EXPECT_EQ(cache.size(), 0);
    EXPECT_FALSE(cache.contains(1));
}

// ============================================================================
// Peek tests
// ============================================================================

TEST(LRUCacheTest, PeekDoesNotMoveToFront) {
    LRUCache<int, std::string> cache(3);

    cache.put(1, "one");
    cache.put(2, "two");
    cache.put(3, "three");

    // Peek at "one" - should NOT move to front
    std::string* val = cache.peek(1);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(*val, "one");

    // Add fourth - should evict "one" since peek didn't move it
    auto evicted = cache.put(4, "four");
    EXPECT_EQ(evicted->first, 1);
}

TEST(LRUCacheTest, ModifyThroughPeek) {
    LRUCache<int, std::string> cache(5);

    cache.put(1, "one");

    std::string* val = cache.peek(1);
    *val = "ONE_MODIFIED";

    auto result = cache.get(1);
    EXPECT_EQ(*result, "ONE_MODIFIED");
}
