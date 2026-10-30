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

// ============================================================================
// Locales
// ============================================================================

class LabelLocaleTest : public ::testing::Test {
protected:
    void SetUp() override {
        labels.clear();
        labels.setLocale("en");
        labels.setFallbackLocale("en");
        labels.loadLocaleString("en",
            "ui.hello: Hello\n"
            "ui.only_en: English only\n"
            "ui.greet: Hello {name}, you have {gold} gold\n"
            "ui.units.one: {count} unit\n"
            "ui.units.other: {count} units\n");
        labels.loadLocaleString("fr",
            "ui.hello: Bonjour\n"
            "ui.units.one: {count} unité\n"
            "ui.units.other: {count} unités\n");
        labels.loadLocaleString("ru",
            "ui.units.one: {count} боец\n"
            "ui.units.few: {count} бойца\n"
            "ui.units.many: {count} бойцов\n");
        labels.loadLocaleString("ja", "ui.units.other: {count}人\n");
    }

    void TearDown() override {
        labels.clear();
        labels.setLocale("en");
    }

    std::string units(int64_t n) { return labels.formatLocalized("ui.units", {}, &n); }

    LabelRegistry& labels = LabelRegistry::global();
};

TEST_F(LabelLocaleTest, ActiveLocaleWins) {
    labels.setLocale("fr");
    EXPECT_EQ(labels.get("ui.hello"), "Bonjour");
}

TEST_F(LabelLocaleTest, MissingStringsFallBackThenShowTheKey) {
    labels.setLocale("fr");
    EXPECT_EQ(labels.get("ui.only_en"), "English only");
    EXPECT_EQ(labels.get("ui.nowhere"), "ui.nowhere");
}

TEST_F(LabelLocaleTest, NamedParametersSubstitute) {
    EXPECT_EQ(labels.formatLocalized("ui.greet", {{"name", "Ada"}, {"gold", "12"}}),
              "Hello Ada, you have 12 gold");
}

TEST_F(LabelLocaleTest, AnUnknownParameterStaysVisible) {
    EXPECT_EQ(labels.formatLocalized("ui.greet", {{"name", "Ada"}}),
              "Hello Ada, you have {gold} gold");
}

TEST_F(LabelLocaleTest, EnglishPlurals) {
    EXPECT_EQ(units(1), "1 unit");
    EXPECT_EQ(units(0), "0 units");
    EXPECT_EQ(units(2), "2 units");
}

TEST_F(LabelLocaleTest, FrenchTreatsZeroAsSingular) {
    labels.setLocale("fr");
    EXPECT_EQ(units(0), "0 unité");
    EXPECT_EQ(units(2), "2 unités");
}

TEST_F(LabelLocaleTest, RussianUsesThreeForms) {
    labels.setLocale("ru");
    EXPECT_EQ(units(1), "1 боец");
    EXPECT_EQ(units(21), "21 боец");
    EXPECT_EQ(units(3), "3 бойца");
    EXPECT_EQ(units(5), "5 бойцов");
    EXPECT_EQ(units(11), "11 бойцов") << "the teens are always 'many'";
    EXPECT_EQ(units(13), "13 бойцов");
}

TEST_F(LabelLocaleTest, JapaneseHasNoGrammaticalNumber) {
    labels.setLocale("ja");
    EXPECT_EQ(units(1), "1人");
    EXPECT_EQ(units(7), "7人");
}

TEST_F(LabelLocaleTest, MissingPluralFormFallsBackToOtherThenTheBareKey) {
    labels.loadLocaleString("en", "ui.plain: Things\n");
    int64_t n = 1;
    EXPECT_EQ(labels.formatLocalized("ui.plain", {}, &n), "Things");
}

TEST_F(LabelLocaleTest, RegionalLocalesUseTheirLanguageRules) {
    EXPECT_STREQ(LabelRegistry::pluralCategory("ru-RU", 3), "few");
    EXPECT_STREQ(LabelRegistry::pluralCategory("pt_BR", 0), "one");
}
