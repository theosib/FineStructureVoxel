#pragma once

/**
 * @file item_type.hpp
 * @brief ItemTypeId and ItemType definitions
 *
 * Design: Phase 13 Inventory & Items
 *
 * ItemTypeId wraps InternedId (from StringInterner::global()), following the
 * same pattern as BlockTypeId. Runtime only — never written to disk.
 * For persistence, use NameRegistry to translate to/from stable PersistentIds.
 */

#include "finevox/core/string_interner.hpp"
#include <cstdint>

namespace finevox {

/// Type-safe wrapper for item type identity (runtime interned ID)
struct ItemTypeId {
    InternedId id = 0;  // 0 = empty/no item

    constexpr ItemTypeId() = default;
    constexpr explicit ItemTypeId(InternedId id_) : id(id_) {}

    /// Create from string name (interns via StringInterner::global())
    [[nodiscard]] static ItemTypeId fromName(std::string_view name) {
        return ItemTypeId{StringInterner::global().intern(name)};
    }

    /// Get the string name (looks up from StringInterner::global())
    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id > UNKNOWN_INTERNED_ID; }

    constexpr bool operator==(const ItemTypeId&) const = default;
    constexpr auto operator<=>(const ItemTypeId&) const = default;
};

/// Empty item type (no item)
constexpr ItemTypeId EMPTY_ITEM_TYPE{};

/// Properties for a registered item type
struct ItemType {
    ItemTypeId id;                      ///< Interned name ID (runtime)
    int32_t maxStackSize = 64;          ///< Maximum items per stack
    BlockTypeId placesBlock;            ///< Block this item places (empty if none)

    // Tool properties
    float miningSpeedMultiplier = 1.0f; ///< Mining speed factor (1.0 = hand speed)
    int32_t maxDurability = 0;          ///< Max durability (0 = infinite/not applicable)
    float attackDamage = 1.0f;          ///< Melee damage

    /// Get the item name from the interner
    [[nodiscard]] std::string_view name() const { return id.name(); }
};

}  // namespace finevox

// Hash specialization for ItemTypeId
template<>
struct std::hash<finevox::ItemTypeId> {
    size_t operator()(const finevox::ItemTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
