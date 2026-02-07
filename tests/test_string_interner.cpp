#include <gtest/gtest.h>
#include "finevox/core/string_interner.hpp"
#include <thread>
#include <vector>
#include <unordered_set>

using namespace finevox;

// ============================================================================
// StringInterner tests
// ============================================================================

TEST(StringInternerTest, GlobalSingleton) {
    auto& a = StringInterner::global();
    auto& b = StringInterner::global();
    EXPECT_EQ(&a, &b);
}

TEST(StringInternerTest, InternReturnsNonZeroForNonEmpty) {
    auto& interner = StringInterner::global();
    InternedId id = interner.intern("test:block");
    EXPECT_NE(id, INVALID_INTERNED_ID);
}

TEST(StringInternerTest, SameStringReturnsSameId) {
    auto& interner = StringInterner::global();
    InternedId id1 = interner.intern("blockgame:stone");
    InternedId id2 = interner.intern("blockgame:stone");
    EXPECT_EQ(id1, id2);
}

TEST(StringInternerTest, DifferentStringsReturnDifferentIds) {
    auto& interner = StringInterner::global();
    InternedId id1 = interner.intern("blockgame:dirt");
    InternedId id2 = interner.intern("blockgame:grass");
    EXPECT_NE(id1, id2);
}

TEST(StringInternerTest, LookupReturnsOriginalString) {
    auto& interner = StringInterner::global();
    InternedId id = interner.intern("mymod:custom_ore");
    std::string_view name = interner.lookup(id);
    EXPECT_EQ(name, "mymod:custom_ore");
}

TEST(StringInternerTest, LookupInvalidIdReturnsEmpty) {
    auto& interner = StringInterner::global();
    std::string_view name = interner.lookup(999999);
    EXPECT_TRUE(name.empty());
}

TEST(StringInternerTest, FindExistingString) {
    auto& interner = StringInterner::global();
    InternedId id = interner.intern("findtest:block");
    auto found = interner.find("findtest:block");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found.value(), id);
}

TEST(StringInternerTest, FindNonExistingString) {
    auto& interner = StringInterner::global();
    auto found = interner.find("nonexistent:block:xyz123");
    EXPECT_FALSE(found.has_value());
}

TEST(StringInternerTest, ReservedIds) {
    auto& interner = StringInterner::global();

    // Air is ID 0, has proper printable name
    std::string_view airName = interner.lookup(AIR_INTERNED_ID);
    EXPECT_EQ(airName, "finevox:air");

    // Invalid is ID 1
    std::string_view invalidName = interner.lookup(INVALID_INTERNED_ID);
    EXPECT_EQ(invalidName, "finevox:invalid");

    // Unknown is ID 2
    std::string_view unknownName = interner.lookup(UNKNOWN_INTERNED_ID);
    EXPECT_EQ(unknownName, "finevox:unknown");
}

TEST(StringInternerTest, EmptyStringIsAir) {
    auto& interner = StringInterner::global();

    // Both empty string and "finevox:air" should map to the same ID
    InternedId emptyId = interner.intern("");
    InternedId airId = interner.intern("finevox:air");

    EXPECT_EQ(emptyId, AIR_INTERNED_ID);
    EXPECT_EQ(airId, AIR_INTERNED_ID);
    EXPECT_EQ(emptyId, airId);

    // find() should also work for both
    auto foundEmpty = interner.find("");
    auto foundAir = interner.find("finevox:air");

    ASSERT_TRUE(foundEmpty.has_value());
    ASSERT_TRUE(foundAir.has_value());
    EXPECT_EQ(foundEmpty.value(), AIR_INTERNED_ID);
    EXPECT_EQ(foundAir.value(), AIR_INTERNED_ID);
}

TEST(StringInternerTest, ThreadSafety) {
    auto& interner = StringInterner::global();

    // Multiple threads interning strings concurrently
    std::vector<std::thread> threads;
    std::vector<InternedId> results(100);

    for (int i = 0; i < 100; ++i) {
        threads.emplace_back([&interner, &results, i]() {
            std::string name = "thread_test:block_" + std::to_string(i % 10);
            results[i] = interner.intern(name);
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Same strings (i % 10) should have same IDs
    for (int i = 0; i < 100; ++i) {
        for (int j = 0; j < 100; ++j) {
            if (i % 10 == j % 10) {
                EXPECT_EQ(results[i], results[j]);
            }
        }
    }
}

// ============================================================================
// BlockTypeId tests
// ============================================================================

TEST(BlockTypeIdTest, DefaultIsAir) {
    BlockTypeId id;
    EXPECT_TRUE(id.isAir());
    EXPECT_TRUE(id.isValid());  // Air is a valid block type
    EXPECT_FALSE(id.isRealBlock());  // But not a "real" block (it's a sentinel)
}

TEST(BlockTypeIdTest, AirConstant) {
    EXPECT_TRUE(AIR_BLOCK_TYPE.isAir());
    EXPECT_TRUE(AIR_BLOCK_TYPE.isValid());  // Air is valid
    EXPECT_FALSE(AIR_BLOCK_TYPE.isRealBlock());
}

TEST(BlockTypeIdTest, InvalidConstant) {
    EXPECT_FALSE(INVALID_BLOCK_TYPE.isAir());
    EXPECT_TRUE(INVALID_BLOCK_TYPE.isInvalid());
    EXPECT_FALSE(INVALID_BLOCK_TYPE.isValid());  // Invalid is NOT valid
    EXPECT_FALSE(INVALID_BLOCK_TYPE.isRealBlock());
}

TEST(BlockTypeIdTest, UnknownConstant) {
    EXPECT_FALSE(UNKNOWN_BLOCK_TYPE.isAir());
    EXPECT_TRUE(UNKNOWN_BLOCK_TYPE.isUnknown());
    EXPECT_TRUE(UNKNOWN_BLOCK_TYPE.isValid());  // Unknown is valid (just unrecognized)
    EXPECT_FALSE(UNKNOWN_BLOCK_TYPE.isRealBlock());
}

TEST(BlockTypeIdTest, FromNameCreatesValidId) {
    BlockTypeId id = BlockTypeId::fromName("test:cobblestone");
    EXPECT_TRUE(id.isValid());
    EXPECT_FALSE(id.isAir());
}

TEST(BlockTypeIdTest, FromNameRoundTrip) {
    BlockTypeId id = BlockTypeId::fromName("test:brick");
    EXPECT_EQ(id.name(), "test:brick");
}

TEST(BlockTypeIdTest, FromEmptyNameIsAir) {
    BlockTypeId id = BlockTypeId::fromName("");
    EXPECT_TRUE(id.isAir());
    EXPECT_EQ(id.name(), "finevox:air");  // name() returns proper printable name
}

TEST(BlockTypeIdTest, AirNameVariants) {
    // Both empty string and "finevox:air" create the same air block type
    BlockTypeId fromEmpty = BlockTypeId::fromName("");
    BlockTypeId fromFull = BlockTypeId::fromName("finevox:air");

    EXPECT_EQ(fromEmpty, fromFull);
    EXPECT_EQ(fromEmpty, AIR_BLOCK_TYPE);
    EXPECT_TRUE(fromFull.isAir());
}

TEST(BlockTypeIdTest, SameNameSameId) {
    BlockTypeId id1 = BlockTypeId::fromName("consistency:test");
    BlockTypeId id2 = BlockTypeId::fromName("consistency:test");
    EXPECT_EQ(id1, id2);
}

TEST(BlockTypeIdTest, HashableInUnorderedSet) {
    std::unordered_set<BlockTypeId> types;
    types.insert(BlockTypeId::fromName("hashtest:a"));
    types.insert(BlockTypeId::fromName("hashtest:b"));
    types.insert(BlockTypeId::fromName("hashtest:a"));  // Duplicate

    EXPECT_EQ(types.size(), 2);
}

TEST(BlockTypeIdTest, Comparison) {
    BlockTypeId air;
    BlockTypeId stone = BlockTypeId::fromName("compare:stone");
    BlockTypeId dirt = BlockTypeId::fromName("compare:dirt");

    EXPECT_EQ(air, AIR_BLOCK_TYPE);
    EXPECT_NE(stone, dirt);
    EXPECT_NE(stone, air);
}
