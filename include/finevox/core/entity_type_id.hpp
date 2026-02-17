#pragma once

/**
 * @file entity_type_id.hpp
 * @brief Type-safe interned entity type identifier
 */

#include "finevox/core/string_interner.hpp"
#include <functional>

namespace finevox {

// ============================================================================
// EntityTypeId — type-safe interned wrapper (same pattern as LootTableId)
// ============================================================================

struct EntityTypeId {
    InternedId id = 0;

    constexpr EntityTypeId() = default;
    constexpr explicit EntityTypeId(InternedId id_) : id(id_) {}

    [[nodiscard]] static EntityTypeId fromName(std::string_view name) {
        return EntityTypeId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const EntityTypeId&) const = default;
    constexpr auto operator<=>(const EntityTypeId&) const = default;
};

constexpr EntityTypeId EMPTY_ENTITY_TYPE_ID{};

}  // namespace finevox

// Hash specialization
template<>
struct std::hash<finevox::EntityTypeId> {
    size_t operator()(const finevox::EntityTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
