#include <gtest/gtest.h>
#include <filesystem>

#include "finevox/core/label_registry.hpp"

using namespace finevox;

// ============================================================================
// Basic loading and lookup
// ============================================================================

TEST(LabelRegistryTest, LoadFromString) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString(R"(
# Comment line
inventory.title: Inventory
hotbar.slot: Slot {0}
block.stone.name: Stone
)");

    EXPECT_EQ(reg.get("inventory.title"), "Inventory");
    EXPECT_EQ(reg.get("block.stone.name"), "Stone");
}

TEST(LabelRegistryTest, MissingKeyReturnsKey) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    EXPECT_EQ(reg.get("nonexistent.key"), "nonexistent.key");
}

TEST(LabelRegistryTest, HasKey) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("foo.bar: Hello");
    EXPECT_TRUE(reg.has("foo.bar"));
    EXPECT_FALSE(reg.has("foo.baz"));
}

TEST(LabelRegistryTest, FormatWithArgs) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("hotbar.slot: Slot {0}");
    EXPECT_EQ(reg.format("hotbar.slot", {"3"}), "Slot 3");
}

TEST(LabelRegistryTest, FormatMultipleArgs) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("debug.chunks: Chunks: {0}/{1}");
    EXPECT_EQ(reg.format("debug.chunks", {"42", "100"}), "Chunks: 42/100");
}

TEST(LabelRegistryTest, FormatMissingArgKeepsPlaceholder) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("test: Hello {0} and {1}");
    EXPECT_EQ(reg.format("test", {"World"}), "Hello World and {1}");
}

TEST(LabelRegistryTest, FormatMissingKeyReturnsKey) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    EXPECT_EQ(reg.format("missing.key", {"arg0"}), "missing.key");
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(LabelRegistryTest, SkipsEmptyAndCommentLines) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString(R"(
# This is a comment

key1: value1

# Another comment
key2: value2
)");

    EXPECT_EQ(reg.get("key1"), "value1");
    EXPECT_EQ(reg.get("key2"), "value2");
}

TEST(LabelRegistryTest, ColonInValue) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("time.format: Time: 12:30");
    EXPECT_EQ(reg.get("time.format"), "Time: 12:30");
}

TEST(LabelRegistryTest, OverwriteExisting) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("key: old value");
    reg.loadFromString("key: new value");
    EXPECT_EQ(reg.get("key"), "new value");
}

TEST(LabelRegistryTest, Clear) {
    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFromString("key: value");
    EXPECT_TRUE(reg.has("key"));

    reg.clear();
    EXPECT_FALSE(reg.has("key"));
}

// ============================================================================
// File loading
// ============================================================================

TEST(LabelRegistryTest, LoadFromFile) {
    std::filesystem::path testDir = std::filesystem::path(__FILE__).parent_path();
    std::filesystem::path projectRoot = testDir.parent_path();
    std::filesystem::path langPath = projectRoot / "resources" / "lang" / "en.lang";

    LabelRegistry& reg = LabelRegistry::global();
    reg.clear();

    reg.loadFile(langPath.string());

    EXPECT_EQ(reg.get("inventory.title"), "Inventory");
    EXPECT_EQ(reg.get("block.stone.name"), "Stone");
    EXPECT_EQ(reg.format("hotbar.slot", {"5"}), "Slot 5");
}
