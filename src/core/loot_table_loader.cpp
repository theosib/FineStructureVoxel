#include "finevox/core/loot_table_loader.hpp"
#include "finevox/core/loot_conditions.hpp"
#include "finevox/core/loot_modifiers.hpp"
#include "finevox/core/loot_registry.hpp"
#include "finevox/core/config_parser.hpp"
#include "finevox/core/block_type.hpp"
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace finevox {

// ============================================================================
// Helpers
// ============================================================================

static std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

static float parseFloatSafe(std::string_view s) {
    s = trimView(s);
    if (s.empty()) return 0.0f;
    // std::from_chars for float not available on all platforms
    std::string tmp(s);
    try { return std::stof(tmp); }
    catch (...) { return 0.0f; }
}

static int32_t parseIntSafe(std::string_view s) {
    s = trimView(s);
    int32_t val = 0;
    std::from_chars(s.data(), s.data() + s.size(), val);
    return val;
}

// ============================================================================
// parseRange
// ============================================================================

std::pair<int32_t, int32_t> LootTableLoader::parseRange(std::string_view str) {
    str = trimView(str);

    // Look for '-' that separates min-max (skip leading '-' for negative numbers)
    size_t start = 0;
    if (!str.empty() && str[0] == '-') start = 1;  // skip leading negative

    auto dash = str.find('-', start);
    if (dash != std::string_view::npos) {
        int32_t min = parseIntSafe(str.substr(0, dash));
        int32_t max = parseIntSafe(str.substr(dash + 1));
        return {min, max};
    }

    int32_t val = parseIntSafe(str);
    return {val, val};
}

// ============================================================================
// parseCondition
// ============================================================================

std::unique_ptr<LootCondition> LootTableLoader::parseCondition(std::string_view str) {
    str = trimView(str);
    if (str.empty()) return nullptr;

    // precise-break
    if (str == "precise-break") {
        return std::make_unique<PreciseBreakCondition>();
    }

    // not <condition>
    if (str.substr(0, 4) == "not " && str.size() > 4) {
        auto inner = parseCondition(str.substr(4));
        if (inner) return std::make_unique<InvertedCondition>(std::move(inner));
        return nullptr;
    }

    // tool-tag <tag-name>
    if (str.substr(0, 9) == "tool-tag " && str.size() > 9) {
        auto tagName = trimView(str.substr(9));
        return std::make_unique<ToolTagCondition>(TagId::fromName(tagName));
    }

    // random-chance <chance> [bounty-bonus]
    if (str.substr(0, 14) == "random-chance " && str.size() > 14) {
        auto rest = trimView(str.substr(14));
        float chance = 0.0f, bonus = 0.0f;
        auto space = rest.find(' ');
        if (space != std::string_view::npos) {
            chance = parseFloatSafe(rest.substr(0, space));
            bonus = parseFloatSafe(rest.substr(space + 1));
        } else {
            chance = parseFloatSafe(rest);
        }
        return std::make_unique<RandomChanceCondition>(chance, bonus);
    }

    // block-type <block-name>
    if (str.substr(0, 11) == "block-type " && str.size() > 11) {
        auto blockName = trimView(str.substr(11));
        return std::make_unique<BlockTypeCondition>(BlockTypeId::fromName(blockName));
    }

    std::cerr << "[LootTableLoader] Unknown condition: " << str << "\n";
    return nullptr;
}

// ============================================================================
// parseModifier
// ============================================================================

std::unique_ptr<LootModifier> LootTableLoader::parseModifier(std::string_view str) {
    str = trimView(str);
    if (str.empty()) return nullptr;

    // bounty [multiplier]
    if (str == "bounty" || str.substr(0, 7) == "bounty ") {
        float mult = 1.0f;
        if (str.size() > 7) {
            mult = parseFloatSafe(str.substr(7));
        }
        return std::make_unique<BountyModifier>(mult);
    }

    // set-count <min>-<max> or set-count <n>
    if (str.substr(0, 10) == "set-count " && str.size() > 10) {
        auto [min, max] = parseRange(str.substr(10));
        return std::make_unique<SetCountModifier>(min, max);
    }

    // plunder-bonus [n]
    if (str == "plunder-bonus" || str.substr(0, 14) == "plunder-bonus ") {
        int32_t bonus = 1;
        if (str.size() > 14) {
            bonus = parseIntSafe(str.substr(14));
        }
        return std::make_unique<PlunderModifier>(bonus);
    }

    std::cerr << "[LootTableLoader] Unknown modifier: " << str << "\n";
    return nullptr;
}

// ============================================================================
// loadFromString — sequential state machine
// ============================================================================

std::optional<LootTable> LootTableLoader::loadFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);

    if (doc.empty()) return std::nullopt;

    LootTable table;

    enum class State { None, Pool, Entry };
    State state = State::None;
    LootPool currentPool;
    LootEntry currentEntry;

    auto finalizeEntry = [&]() {
        if (state == State::Entry) {
            currentPool.entries.push_back(std::move(currentEntry));
            currentEntry = LootEntry{};
        }
    };

    auto finalizePool = [&]() {
        finalizeEntry();
        if (state != State::None) {
            table.addPool(std::move(currentPool));
            currentPool = LootPool{};
        }
    };

    for (const auto& entry : doc.entries()) {
        // pool:NAME: — start a new pool
        if (entry.key == "pool") {
            finalizePool();
            currentPool = LootPool{};
            state = State::Pool;
            continue;
        }

        // entry:NAME: — start a new entry within current pool
        if (entry.key == "entry") {
            finalizeEntry();
            currentEntry = LootEntry{};
            state = State::Entry;
            continue;
        }

        // Pool-level keys (between pool: and first entry:)
        if (state == State::Pool) {
            if (entry.key == "rolls") {
                auto [min, max] = parseRange(entry.value.asString());
                currentPool.rollsMin = min;
                currentPool.rollsMax = max;
            } else if (entry.key == "bonus-rolls") {
                currentPool.bonusRollsPerLevel = entry.value.asFloat();
            } else if (entry.key == "condition") {
                auto cond = parseCondition(entry.value.asString());
                if (cond) currentPool.conditions.push_back(std::move(cond));
            } else if (entry.key == "modifier") {
                auto mod = parseModifier(entry.value.asString());
                if (mod) currentPool.modifiers.push_back(std::move(mod));
            }
            continue;
        }

        // Entry-level keys (after entry:)
        if (state == State::Entry) {
            if (entry.key == "type") {
                auto val = entry.value.asString();
                if (val == "item") currentEntry.type = LootEntry::Type::Item;
                else if (val == "table") currentEntry.type = LootEntry::Type::LootTableRef;
                else if (val == "empty") currentEntry.type = LootEntry::Type::Empty;
            } else if (entry.key == "item") {
                currentEntry.item = ItemTypeId::fromName(entry.value.asString());
            } else if (entry.key == "count") {
                auto [min, max] = parseRange(entry.value.asString());
                currentEntry.countMin = min;
                currentEntry.countMax = max;
            } else if (entry.key == "weight") {
                currentEntry.weight = entry.value.asFloat(1.0f);
            } else if (entry.key == "table") {
                currentEntry.referencedTable = LootTableId::fromName(entry.value.asString());
            } else if (entry.key == "condition") {
                auto cond = parseCondition(entry.value.asString());
                if (cond) currentEntry.conditions.push_back(std::move(cond));
            } else if (entry.key == "modifier") {
                auto mod = parseModifier(entry.value.asString());
                if (mod) currentEntry.modifiers.push_back(std::move(mod));
            }
            continue;
        }
    }

    // Finalize the last pool/entry
    finalizePool();

    if (table.empty()) return std::nullopt;
    return table;
}

// ============================================================================
// loadFromFile
// ============================================================================

std::optional<LootTable> LootTableLoader::loadFromFile(const std::string& path) {
    ConfigParser parser;
    auto doc = parser.parseFile(path);
    if (!doc) return std::nullopt;

    // Re-serialize to string for simplicity? No — we can iterate directly.
    // But we already have the doc, so let's parse it directly.
    // Actually, loadFromString uses parseString internally, so we need to
    // work with the ConfigDocument directly. Let's refactor to share logic.

    // For now, read the file as text and pass to loadFromString
    std::ifstream file(path);
    if (!file.is_open()) return std::nullopt;

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    return loadFromString(content);
}

// ============================================================================
// loadDirectory
// ============================================================================

size_t LootTableLoader::loadDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".loot") continue;

        auto tableName = entry.path().stem().string();
        auto table = loadFromFile(entry.path().string());
        if (table) {
            if (LootRegistry::global().registerTable(tableName, std::move(*table))) {
                ++count;
            }
        }
    }

    return count;
}

}  // namespace finevox
