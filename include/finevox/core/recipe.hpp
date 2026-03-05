#pragma once

/**
 * @file recipe.hpp
 * @brief Recipe types, IDs, and data structures for the crafting system
 *
 * Design: Phase 22 Crafting + Recipe System (Sub-Phase 22-1)
 *
 * Three recipe types:
 *   - Shaped:    Grid pattern (up to 3x3) with positional matching
 *   - Shapeless: Unordered ingredient set
 *   - Smelting:  Input + fuel + time -> output
 *
 * Ingredients use ItemMatch predicates (Exact or Tagged) for flexible matching.
 */

#include "finevox/core/string_interner.hpp"
#include "finevox/core/item_match.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace finevox {

// ============================================================================
// RecipeId — type-safe interned wrapper (same pattern as LootTableId, TagId)
// ============================================================================

struct RecipeId {
    InternedId id = 0;

    constexpr RecipeId() = default;
    constexpr explicit RecipeId(InternedId id_) : id(id_) {}

    [[nodiscard]] static RecipeId fromName(std::string_view name) {
        return RecipeId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const RecipeId&) const = default;
    constexpr auto operator<=>(const RecipeId&) const = default;
};

constexpr RecipeId EMPTY_RECIPE{};

// ============================================================================
// StationTypeId — identifies required crafting station
// ============================================================================

struct StationTypeId {
    InternedId id = 0;

    constexpr StationTypeId() = default;
    constexpr explicit StationTypeId(InternedId id_) : id(id_) {}

    [[nodiscard]] static StationTypeId fromName(std::string_view name) {
        return StationTypeId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const StationTypeId&) const = default;
    constexpr auto operator<=>(const StationTypeId&) const = default;
};

/// Empty station = hand-crafting (no station required)
constexpr StationTypeId EMPTY_STATION{};

// ============================================================================
// RecipeType
// ============================================================================

enum class RecipeType : uint8_t {
    Shaped,      ///< Grid pattern with positional matching
    Shapeless,   ///< Unordered ingredient set
    Smelting,    ///< Input + fuel + time -> output
};

// ============================================================================
// Recipe
// ============================================================================

struct Recipe {
    RecipeId id;
    RecipeType type = RecipeType::Shaped;
    StationTypeId station;           ///< Required station (EMPTY_STATION = hand-crafting)
    std::string category;            ///< UI category: "tools", "building", "materials"

    // --- Shaped recipes ---
    int32_t width = 0;               ///< Pattern width (1-3)
    int32_t height = 0;              ///< Pattern height (1-3)
    std::vector<ItemMatch> pattern;  ///< Row-major, width * height entries
    bool allowMirror = false;        ///< Allow horizontal mirror matching

    // --- Shapeless recipes ---
    std::vector<ItemMatch> ingredients;  ///< Unordered ingredient list

    // --- Smelting recipes ---
    ItemMatch smeltInput;            ///< Single input item
    int32_t smeltTicks = 200;        ///< Ticks to complete (at 30 TPS ~6.7s)
    float experience = 0.0f;         ///< XP reward

    // --- Output ---
    ItemTypeId outputItem;           ///< Result item type
    int32_t outputCount = 1;         ///< Result count

    // --- Convenience predicates ---
    [[nodiscard]] bool isShaped() const { return type == RecipeType::Shaped; }
    [[nodiscard]] bool isShapeless() const { return type == RecipeType::Shapeless; }
    [[nodiscard]] bool isSmelting() const { return type == RecipeType::Smelting; }
};

}  // namespace finevox

// Hash specializations
template<>
struct std::hash<finevox::RecipeId> {
    size_t operator()(const finevox::RecipeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};

template<>
struct std::hash<finevox::StationTypeId> {
    size_t operator()(const finevox::StationTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
