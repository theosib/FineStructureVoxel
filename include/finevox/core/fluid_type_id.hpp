#pragma once

/**
 * @file fluid_type_id.hpp
 * @brief Interned identifier for fluid types
 *
 * Follows the same pattern as EntityTypeId, SoundSetId, etc.
 * Uses StringInterner for O(1) comparison and hashing.
 * ID 0 = empty (no fluid present).
 */

#include "finevox/core/string_interner.hpp"
#include <functional>

namespace finevox {

struct FluidTypeId {
    InternedId id = 0;  // 0 = empty (no fluid)

    constexpr FluidTypeId() = default;
    constexpr explicit FluidTypeId(InternedId id_) : id(id_) {}

    /// Create from string name (interns if not already present)
    [[nodiscard]] static FluidTypeId fromName(std::string_view name) {
        return FluidTypeId{StringInterner::global().intern(name)};
    }

    /// Get the string name
    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    /// Check if this represents no fluid (empty)
    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }

    /// Check if this is a valid fluid type
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const FluidTypeId&) const = default;
    constexpr auto operator<=>(const FluidTypeId&) const = default;
};

/// Constant representing no fluid
constexpr FluidTypeId EMPTY_FLUID_TYPE{};

}  // namespace finevox

template<>
struct std::hash<finevox::FluidTypeId> {
    size_t operator()(const finevox::FluidTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
