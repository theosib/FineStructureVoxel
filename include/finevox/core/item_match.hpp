#pragma once

/**
 * @file item_match.hpp
 * @brief ItemMatch — predicate for recipe ingredient matching
 *
 * Design: Phase 14 Tags, Unification & Crafting Infrastructure
 *
 * Three match modes:
 *   - Empty:  matches empty slots (candidate.isEmpty())
 *   - Exact:  matches a specific item (resolves through unification)
 *   - Tagged: matches any item with a given tag
 *
 * Used by the recipe system to express flexible ingredient requirements.
 * Header-only — no .cpp needed.
 */

#include "finevox/core/item_type.hpp"
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"

#include <variant>

namespace finevox {

struct ItemMatch {
    struct Empty {};
    struct Exact { ItemTypeId item; };
    struct Tagged { TagId tag; };

    std::variant<Empty, Exact, Tagged> match;

    /// Match empty slot
    [[nodiscard]] static ItemMatch empty() {
        return ItemMatch{Empty{}};
    }

    /// Match a specific item (resolved through unification)
    [[nodiscard]] static ItemMatch exact(ItemTypeId item) {
        return ItemMatch{Exact{item}};
    }

    /// Match any item with the given tag
    [[nodiscard]] static ItemMatch tagged(TagId tag) {
        return ItemMatch{Tagged{tag}};
    }

    /// Test whether a candidate item matches this predicate
    [[nodiscard]] bool matches(ItemTypeId candidate) const {
        return std::visit([&](const auto& m) -> bool {
            using T = std::decay_t<decltype(m)>;

            if constexpr (std::is_same_v<T, Empty>) {
                return candidate.isEmpty();
            }
            else if constexpr (std::is_same_v<T, Exact>) {
                // Resolve both through unification before comparing
                auto& unify = UnificationRegistry::global();
                return unify.resolve(candidate) == unify.resolve(m.item);
            }
            else if constexpr (std::is_same_v<T, Tagged>) {
                return TagRegistry::global().hasTag(candidate.id, m.tag);
            }
        }, match);
    }

    /// Check if this is an empty match
    [[nodiscard]] bool isEmpty() const {
        return std::holds_alternative<Empty>(match);
    }

    /// Check if this is an exact match
    [[nodiscard]] bool isExact() const {
        return std::holds_alternative<Exact>(match);
    }

    /// Check if this is a tagged match
    [[nodiscard]] bool isTagged() const {
        return std::holds_alternative<Tagged>(match);
    }
};

}  // namespace finevox
