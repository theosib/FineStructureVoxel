#pragma once

/**
 * @file tag.hpp
 * @brief TagId — type-safe wrapper for tag identity
 *
 * Design: Phase 14 Tags, Unification & Crafting Infrastructure
 *
 * TagId wraps InternedId (from StringInterner::global()), following the
 * same pattern as ItemTypeId and BlockTypeId.
 *
 * Tag naming convention:
 *   c:ingots/iron      — community tag (cross-mod interop)
 *   c:planks            — community tag (broad category)
 *   finevox:fuel        — engine-defined tag
 *   mymod:magic_metals  — mod-specific tag
 */

#include "finevox/core/string_interner.hpp"
#include <cstdint>

namespace finevox {

/// Type-safe wrapper for tag identity (runtime interned ID)
struct TagId {
    InternedId id = 0;  // 0 = empty/no tag

    constexpr TagId() = default;
    constexpr explicit TagId(InternedId id_) : id(id_) {}

    /// Create from string name (interns via StringInterner::global())
    [[nodiscard]] static TagId fromName(std::string_view name) {
        return TagId{StringInterner::global().intern(name)};
    }

    /// Get the string name (looks up from StringInterner::global())
    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }

    constexpr bool operator==(const TagId&) const = default;
    constexpr auto operator<=>(const TagId&) const = default;
};

/// Empty tag (no tag)
constexpr TagId EMPTY_TAG{};

}  // namespace finevox

// Hash specialization for TagId
template<>
struct std::hash<finevox::TagId> {
    size_t operator()(const finevox::TagId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
