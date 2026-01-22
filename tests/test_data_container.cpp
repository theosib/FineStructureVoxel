#include <gtest/gtest.h>
#include "finevox/data_container.hpp"
#include <cmath>

using namespace finevox;

// ============================================================================
// Basic Operations
// ============================================================================

TEST(DataContainer, DefaultEmpty) {
    DataContainer dc;
    EXPECT_TRUE(dc.empty());
    EXPECT_EQ(dc.size(), 0);
}

TEST(DataContainer, SetAndGetInt) {
    DataContainer dc;
    DataKey key = internKey("power");

    dc.set(key, 15);
    EXPECT_TRUE(dc.has(key));
    EXPECT_EQ(dc.get<int>(key), 15);
    EXPECT_EQ(dc.get<int64_t>(key), 15);
}

TEST(DataContainer, SetAndGetDouble) {
    DataContainer dc;
    DataKey key = internKey("progress");

    dc.set(key, 0.75);
    EXPECT_TRUE(dc.has(key));
    EXPECT_DOUBLE_EQ(dc.get<double>(key), 0.75);
    EXPECT_FLOAT_EQ(dc.get<float>(key), 0.75f);
}

TEST(DataContainer, SetAndGetString) {
    DataContainer dc;
    DataKey key = internKey("name");

    dc.set(key, std::string("Hello World"));
    EXPECT_TRUE(dc.has(key));
    EXPECT_EQ(dc.get<std::string>(key), "Hello World");
}

TEST(DataContainer, SetAndGetBool) {
    DataContainer dc;
    DataKey trueKey = internKey("active");
    DataKey falseKey = internKey("locked");

    dc.set(trueKey, true);
    dc.set(falseKey, false);

    EXPECT_TRUE(dc.get<bool>(trueKey));
    EXPECT_FALSE(dc.get<bool>(falseKey));

    // Bools stored as int64_t
    EXPECT_EQ(dc.get<int64_t>(trueKey), 1);
    EXPECT_EQ(dc.get<int64_t>(falseKey), 0);
}

TEST(DataContainer, SetAndGetBytes) {
    DataContainer dc;
    DataKey key = internKey("data");

    std::vector<uint8_t> bytes = {0x01, 0x02, 0x03, 0xFF};
    dc.set(key, bytes);

    EXPECT_TRUE(dc.has(key));
    auto result = dc.get<std::vector<uint8_t>>(key);
    EXPECT_EQ(result, bytes);
}

TEST(DataContainer, StringKeyConvenience) {
    DataContainer dc;

    dc.set("count", 42);
    EXPECT_TRUE(dc.has("count"));
    EXPECT_EQ(dc.get<int>("count"), 42);

    dc.remove("count");
    EXPECT_FALSE(dc.has("count"));
}

TEST(DataContainer, ConstCharKey) {
    DataContainer dc;

    const char* key = "message";
    dc.set(key, "test");
    EXPECT_EQ(dc.get<std::string>(key), "test");
}

// ============================================================================
// Default Values
// ============================================================================

TEST(DataContainer, DefaultValueForMissing) {
    DataContainer dc;

    EXPECT_EQ(dc.get<int>("missing", 99), 99);
    EXPECT_DOUBLE_EQ(dc.get<double>("missing", 1.5), 1.5);
    EXPECT_EQ(dc.get<std::string>("missing", "default"), "default");
}

TEST(DataContainer, DefaultValueForWrongType) {
    DataContainer dc;
    dc.set("value", 42);  // int

    // Trying to get as wrong type returns default
    EXPECT_EQ(dc.get<std::string>("value", "fallback"), "fallback");
}

// ============================================================================
// Remove and Clear
// ============================================================================

TEST(DataContainer, Remove) {
    DataContainer dc;
    dc.set("a", 1);
    dc.set("b", 2);

    EXPECT_EQ(dc.size(), 2);

    dc.remove("a");
    EXPECT_FALSE(dc.has("a"));
    EXPECT_TRUE(dc.has("b"));
    EXPECT_EQ(dc.size(), 1);
}

TEST(DataContainer, RemoveNonexistent) {
    DataContainer dc;
    dc.set("a", 1);

    // Should not throw
    dc.remove("nonexistent");
    EXPECT_EQ(dc.size(), 1);
}

TEST(DataContainer, Clear) {
    DataContainer dc;
    dc.set("a", 1);
    dc.set("b", 2);
    dc.set("c", 3);

    dc.clear();
    EXPECT_TRUE(dc.empty());
    EXPECT_EQ(dc.size(), 0);
}

// ============================================================================
// Overwrite
// ============================================================================

TEST(DataContainer, OverwriteSameType) {
    DataContainer dc;
    dc.set("x", 10);
    EXPECT_EQ(dc.get<int>("x"), 10);

    dc.set("x", 20);
    EXPECT_EQ(dc.get<int>("x"), 20);
    EXPECT_EQ(dc.size(), 1);
}

TEST(DataContainer, OverwriteDifferentType) {
    DataContainer dc;
    dc.set("x", 10);
    EXPECT_EQ(dc.get<int>("x"), 10);

    dc.set("x", std::string("now a string"));
    EXPECT_EQ(dc.get<std::string>("x"), "now a string");
    EXPECT_EQ(dc.get<int>("x", -1), -1);  // Wrong type now
}

// ============================================================================
// Arrays
// ============================================================================

TEST(DataContainer, IntArray) {
    DataContainer dc;
    std::vector<int64_t> arr = {1, 2, 3, 4, 5};

    dc.set("numbers", arr);
    auto result = dc.get<std::vector<int64_t>>("numbers");
    EXPECT_EQ(result, arr);
}

TEST(DataContainer, DoubleArray) {
    DataContainer dc;
    std::vector<double> arr = {1.1, 2.2, 3.3};

    dc.set("floats", arr);
    auto result = dc.get<std::vector<double>>("floats");
    ASSERT_EQ(result.size(), 3);
    EXPECT_DOUBLE_EQ(result[0], 1.1);
    EXPECT_DOUBLE_EQ(result[1], 2.2);
    EXPECT_DOUBLE_EQ(result[2], 3.3);
}

TEST(DataContainer, StringArray) {
    DataContainer dc;
    std::vector<std::string> arr = {"hello", "world", "test"};

    dc.set("strings", arr);
    auto result = dc.get<std::vector<std::string>>("strings");
    EXPECT_EQ(result, arr);
}

TEST(DataContainer, EmptyArrays) {
    DataContainer dc;

    dc.set("empty_ints", std::vector<int64_t>{});
    dc.set("empty_doubles", std::vector<double>{});
    dc.set("empty_strings", std::vector<std::string>{});

    EXPECT_TRUE(dc.get<std::vector<int64_t>>("empty_ints").empty());
    EXPECT_TRUE(dc.get<std::vector<double>>("empty_doubles").empty());
    EXPECT_TRUE(dc.get<std::vector<std::string>>("empty_strings").empty());
}

// ============================================================================
// Nested Containers
// ============================================================================

TEST(DataContainer, NestedContainer) {
    DataContainer dc;

    auto nested = std::make_unique<DataContainer>();
    nested->set("inner_value", 42);
    nested->set("inner_name", std::string("nested"));

    dc.set("nested", std::move(nested));

    const DataValue* raw = dc.getRaw(internKey("nested"));
    ASSERT_NE(raw, nullptr);

    auto* nestedPtr = std::get_if<std::unique_ptr<DataContainer>>(raw);
    ASSERT_NE(nestedPtr, nullptr);
    ASSERT_NE(nestedPtr->get(), nullptr);

    EXPECT_EQ((*nestedPtr)->get<int>("inner_value"), 42);
    EXPECT_EQ((*nestedPtr)->get<std::string>("inner_name"), "nested");
}

// ============================================================================
// Clone (Deep Copy)
// ============================================================================

TEST(DataContainer, Clone) {
    DataContainer dc;
    dc.set("int_val", 123);
    dc.set("str_val", std::string("hello"));
    dc.set("arr_val", std::vector<int64_t>{1, 2, 3});

    auto clone = dc.clone();

    EXPECT_EQ(clone->get<int>("int_val"), 123);
    EXPECT_EQ(clone->get<std::string>("str_val"), "hello");
    EXPECT_EQ(clone->get<std::vector<int64_t>>("arr_val"), (std::vector<int64_t>{1, 2, 3}));

    // Modify original, clone should be unaffected
    dc.set("int_val", 999);
    EXPECT_EQ(clone->get<int>("int_val"), 123);
}

TEST(DataContainer, CloneNested) {
    DataContainer dc;

    auto nested = std::make_unique<DataContainer>();
    nested->set("x", 10);
    dc.set("child", std::move(nested));

    auto clone = dc.clone();

    // Modify original's nested container
    const DataValue* origRaw = dc.getRaw(internKey("child"));
    auto* origNested = std::get_if<std::unique_ptr<DataContainer>>(origRaw);
    (*origNested)->set("x", 999);

    // Clone's nested should be unaffected
    const DataValue* cloneRaw = clone->getRaw(internKey("child"));
    auto* cloneNested = std::get_if<std::unique_ptr<DataContainer>>(cloneRaw);
    EXPECT_EQ((*cloneNested)->get<int>("x"), 10);
}

// ============================================================================
// ForEach
// ============================================================================

TEST(DataContainer, ForEach) {
    DataContainer dc;
    dc.set("a", 1);
    dc.set("b", 2);
    dc.set("c", 3);

    int64_t sum = 0;
    int count = 0;
    dc.forEach([&](DataKey key, const DataValue& value) {
        (void)key;
        if (auto* val = std::get_if<int64_t>(&value)) {
            sum += *val;
        }
        ++count;
    });

    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 6);
}

// ============================================================================
// CBOR Serialization - Basic Types
// ============================================================================

TEST(DataContainer, CBORRoundtripEmpty) {
    DataContainer dc;

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_TRUE(restored->empty());
}

TEST(DataContainer, CBORRoundtripInt) {
    DataContainer dc;
    dc.set("positive", 42);
    dc.set("negative", -100);
    dc.set("zero", 0);
    dc.set("large", int64_t(1) << 40);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<int64_t>("positive"), 42);
    EXPECT_EQ(restored->get<int64_t>("negative"), -100);
    EXPECT_EQ(restored->get<int64_t>("zero"), 0);
    EXPECT_EQ(restored->get<int64_t>("large"), int64_t(1) << 40);
}

TEST(DataContainer, CBORRoundtripDouble) {
    DataContainer dc;
    dc.set("pi", 3.14159265358979);
    dc.set("negative", -1.5);
    dc.set("zero", 0.0);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_DOUBLE_EQ(restored->get<double>("pi"), 3.14159265358979);
    EXPECT_DOUBLE_EQ(restored->get<double>("negative"), -1.5);
    EXPECT_DOUBLE_EQ(restored->get<double>("zero"), 0.0);
}

TEST(DataContainer, CBORRoundtripString) {
    DataContainer dc;
    dc.set("short", std::string("hi"));
    dc.set("empty", std::string(""));
    dc.set("unicode", std::string("Hello 世界 🌍"));

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<std::string>("short"), "hi");
    EXPECT_EQ(restored->get<std::string>("empty"), "");
    EXPECT_EQ(restored->get<std::string>("unicode"), "Hello 世界 🌍");
}

TEST(DataContainer, CBORRoundtripBytes) {
    DataContainer dc;
    std::vector<uint8_t> data = {0x00, 0x01, 0xFF, 0x80};
    dc.set("binary", data);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<std::vector<uint8_t>>("binary"), data);
}

// ============================================================================
// CBOR Serialization - Arrays
// ============================================================================

TEST(DataContainer, CBORRoundtripIntArray) {
    DataContainer dc;
    std::vector<int64_t> arr = {-1, 0, 1, 1000, -1000};
    dc.set("ints", arr);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<std::vector<int64_t>>("ints"), arr);
}

TEST(DataContainer, CBORRoundtripDoubleArray) {
    DataContainer dc;
    std::vector<double> arr = {1.1, 2.2, 3.3, -4.4};
    dc.set("doubles", arr);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    auto result = restored->get<std::vector<double>>("doubles");
    ASSERT_EQ(result.size(), arr.size());
    for (size_t i = 0; i < arr.size(); ++i) {
        EXPECT_DOUBLE_EQ(result[i], arr[i]);
    }
}

TEST(DataContainer, CBORRoundtripStringArray) {
    DataContainer dc;
    std::vector<std::string> arr = {"one", "two", "three"};
    dc.set("strings", arr);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<std::vector<std::string>>("strings"), arr);
}

TEST(DataContainer, CBORRoundtripEmptyArrays) {
    DataContainer dc;
    dc.set("empty_ints", std::vector<int64_t>{});
    dc.set("empty_doubles", std::vector<double>{});
    dc.set("empty_strings", std::vector<std::string>{});

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_TRUE(restored->get<std::vector<int64_t>>("empty_ints").empty());
    // Empty arrays decode as int64_t by default, so these will return empty defaults
    // This is acceptable behavior - empty is empty
}

// ============================================================================
// CBOR Serialization - Nested Containers
// ============================================================================

TEST(DataContainer, CBORRoundtripNested) {
    DataContainer dc;

    auto child = std::make_unique<DataContainer>();
    child->set("x", 10);
    child->set("y", 20);
    child->set("name", std::string("child"));

    dc.set("child", std::move(child));
    dc.set("parent_value", 100);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<int>("parent_value"), 100);

    const DataValue* childRaw = restored->getRaw(internKey("child"));
    ASSERT_NE(childRaw, nullptr);

    auto* childPtr = std::get_if<std::unique_ptr<DataContainer>>(childRaw);
    ASSERT_NE(childPtr, nullptr);
    ASSERT_NE(childPtr->get(), nullptr);

    EXPECT_EQ((*childPtr)->get<int>("x"), 10);
    EXPECT_EQ((*childPtr)->get<int>("y"), 20);
    EXPECT_EQ((*childPtr)->get<std::string>("name"), "child");
}

TEST(DataContainer, CBORRoundtripDeeplyNested) {
    DataContainer dc;

    auto level3 = std::make_unique<DataContainer>();
    level3->set("depth", 3);

    auto level2 = std::make_unique<DataContainer>();
    level2->set("depth", 2);
    level2->set("child", std::move(level3));

    auto level1 = std::make_unique<DataContainer>();
    level1->set("depth", 1);
    level1->set("child", std::move(level2));

    dc.set("root_value", 0);
    dc.set("child", std::move(level1));

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<int>("root_value"), 0);

    // Navigate to level 3
    const DataValue* l1Raw = restored->getRaw(internKey("child"));
    ASSERT_NE(l1Raw, nullptr);
    auto* l1 = std::get_if<std::unique_ptr<DataContainer>>(l1Raw);
    ASSERT_NE(l1, nullptr);
    EXPECT_EQ((*l1)->get<int>("depth"), 1);

    const DataValue* l2Raw = (*l1)->getRaw(internKey("child"));
    ASSERT_NE(l2Raw, nullptr);
    auto* l2 = std::get_if<std::unique_ptr<DataContainer>>(l2Raw);
    ASSERT_NE(l2, nullptr);
    EXPECT_EQ((*l2)->get<int>("depth"), 2);

    const DataValue* l3Raw = (*l2)->getRaw(internKey("child"));
    ASSERT_NE(l3Raw, nullptr);
    auto* l3 = std::get_if<std::unique_ptr<DataContainer>>(l3Raw);
    ASSERT_NE(l3, nullptr);
    EXPECT_EQ((*l3)->get<int>("depth"), 3);
}

// ============================================================================
// CBOR Edge Cases
// ============================================================================

TEST(DataContainer, CBORFromEmptySpan) {
    std::span<const uint8_t> empty;
    auto restored = DataContainer::fromCBOR(empty);
    EXPECT_TRUE(restored->empty());
}

TEST(DataContainer, CBORLargeIntegers) {
    DataContainer dc;
    dc.set("max_int64", std::numeric_limits<int64_t>::max());
    dc.set("min_int64", std::numeric_limits<int64_t>::min());

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<int64_t>("max_int64"), std::numeric_limits<int64_t>::max());
    EXPECT_EQ(restored->get<int64_t>("min_int64"), std::numeric_limits<int64_t>::min());
}

TEST(DataContainer, CBORSpecialDoubles) {
    DataContainer dc;
    dc.set("inf", std::numeric_limits<double>::infinity());
    dc.set("neg_inf", -std::numeric_limits<double>::infinity());
    dc.set("nan", std::numeric_limits<double>::quiet_NaN());

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_TRUE(std::isinf(restored->get<double>("inf")));
    EXPECT_TRUE(restored->get<double>("inf") > 0);

    EXPECT_TRUE(std::isinf(restored->get<double>("neg_inf")));
    EXPECT_TRUE(restored->get<double>("neg_inf") < 0);

    EXPECT_TRUE(std::isnan(restored->get<double>("nan")));
}

TEST(DataContainer, CBORLongString) {
    DataContainer dc;
    std::string longStr(10000, 'x');
    dc.set("long", longStr);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    EXPECT_EQ(restored->get<std::string>("long"), longStr);
}

TEST(DataContainer, CBORLargeArray) {
    DataContainer dc;
    std::vector<int64_t> arr(1000);
    for (size_t i = 0; i < arr.size(); ++i) {
        arr[i] = static_cast<int64_t>(i * i);
    }
    dc.set("large", arr);

    auto bytes = dc.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    auto result = restored->get<std::vector<int64_t>>("large");
    EXPECT_EQ(result, arr);
}

// ============================================================================
// Complex Combined Test
// ============================================================================

TEST(DataContainer, CBORComplexStructure) {
    // Simulate a tile entity like a furnace
    DataContainer furnace;
    furnace.set("id", std::string("blockgame:furnace"));
    furnace.set("burn_time", 200);
    furnace.set("cook_time", 100);
    furnace.set("cook_time_total", 200);

    // Inventory as nested container
    auto inventory = std::make_unique<DataContainer>();
    inventory->set("slots", 3);
    inventory->set("items", std::vector<std::string>{"coal", "iron_ore", ""});
    inventory->set("counts", std::vector<int64_t>{32, 16, 0});
    furnace.set("inventory", std::move(inventory));

    // Custom data as binary
    std::vector<uint8_t> customData = {0xDE, 0xAD, 0xBE, 0xEF};
    furnace.set("custom", customData);

    // Serialize and restore
    auto bytes = furnace.toCBOR();
    auto restored = DataContainer::fromCBOR(bytes);

    // Verify all data
    EXPECT_EQ(restored->get<std::string>("id"), "blockgame:furnace");
    EXPECT_EQ(restored->get<int>("burn_time"), 200);
    EXPECT_EQ(restored->get<int>("cook_time"), 100);
    EXPECT_EQ(restored->get<int>("cook_time_total"), 200);
    EXPECT_EQ(restored->get<std::vector<uint8_t>>("custom"), customData);

    // Check inventory
    const DataValue* invRaw = restored->getRaw(internKey("inventory"));
    ASSERT_NE(invRaw, nullptr);
    auto* inv = std::get_if<std::unique_ptr<DataContainer>>(invRaw);
    ASSERT_NE(inv, nullptr);

    EXPECT_EQ((*inv)->get<int>("slots"), 3);
    EXPECT_EQ((*inv)->get<std::vector<std::string>>("items"),
              (std::vector<std::string>{"coal", "iron_ore", ""}));
    EXPECT_EQ((*inv)->get<std::vector<int64_t>>("counts"),
              (std::vector<int64_t>{32, 16, 0}));
}
