#pragma once

/**
 * @file unification.hpp
 * @brief UnificationRegistry — cross-mod item equivalence groups
 *
 * Design: Phase 14 Tags, Unification & Crafting Infrastructure
 *
 * When multiple mods add the same logical resource (e.g., nickel ingot),
 * unification declares them equivalent so recipes and inventories treat
 * them interchangeably. Each group has a canonical item that inventories
 * consolidate to (when auto_convert is enabled).
 *
 * Auto-resolution detects equivalences from shared community tags and
 * base name matching. Inferred equivalences log warnings to encourage
 * mod developers to tag properly.
 *
 * Thread-safe singleton (shared_mutex).
 */

#include "finevox/core/item_type.hpp"
#include "finevox/core/tag.hpp"

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox {

// Forward declaration
class TagRegistry;

class UnificationRegistry {
public:
    /// Get the global unification registry instance
    static UnificationRegistry& global();

    // ========================================================================
    // Explicit definition (from .tag files)
    // ========================================================================

    /// Declare an equivalence group with a canonical item and members.
    /// @param canonical The preferred item for this group
    /// @param members   All members including canonical
    /// @param autoConvert If true, inventories consolidate to canonical
    void declareGroup(ItemTypeId canonical,
                      const std::vector<ItemTypeId>& members,
                      bool autoConvert = true);

    /// Declare items that must NOT be auto-unified (even if names/tags match)
    void declareSeparate(const std::vector<ItemTypeId>& items);

    // ========================================================================
    // Auto-resolution (call after TagRegistry::rebuild())
    // ========================================================================

    /// Detect equivalence groups from community tags and name matching.
    /// Logs warnings for inferred equivalences and conflicts.
    void autoResolve(const TagRegistry& tags);

    /// Propagate tags across unified groups (Option A).
    /// All members inherit all tags from any group member.
    /// Call tags.rebuild() after this.
    void propagateTags(TagRegistry& tags) const;

    // ========================================================================
    // Queries
    // ========================================================================

    /// Resolve an item to its canonical form (returns self if not unified)
    [[nodiscard]] ItemTypeId resolve(ItemTypeId item) const;

    /// Check if two items are in the same equivalence group
    [[nodiscard]] bool areEquivalent(ItemTypeId a, ItemTypeId b) const;

    /// Get all members of an item's equivalence group (empty if not unified)
    [[nodiscard]] std::vector<ItemTypeId> getGroup(ItemTypeId item) const;

    /// Get the canonical item for a group (returns self if not unified)
    [[nodiscard]] ItemTypeId getCanonical(ItemTypeId item) const;

    /// Whether this item's group has auto-convert enabled
    [[nodiscard]] bool isAutoConvert(ItemTypeId item) const;

    // ========================================================================
    // Introspection
    // ========================================================================

    /// Number of equivalence groups
    [[nodiscard]] size_t groupCount() const;

    /// Reset all data (for testing)
    void clear();

    // Non-copyable singleton
    UnificationRegistry(const UnificationRegistry&) = delete;
    UnificationRegistry& operator=(const UnificationRegistry&) = delete;

private:
    UnificationRegistry() = default;

    struct Group {
        ItemTypeId canonical;
        std::vector<ItemTypeId> members;
        bool autoConvert = true;
    };

    /// Select canonical from a set of candidates
    ItemTypeId selectCanonical(const std::vector<ItemTypeId>& candidates,
                               const TagRegistry& tags) const;

    /// Extract base name (strip namespace prefix before ':')
    static std::string_view baseName(std::string_view fullName);

    /// Extract namespace prefix (before ':'), empty if none
    static std::string_view namespacePart(std::string_view fullName);

    mutable std::shared_mutex mutex_;

    // Group storage: group index → Group
    std::vector<Group> groups_;

    // Reverse index: item → group index
    std::unordered_map<ItemTypeId, size_t> itemToGroup_;

    // Items that must not be auto-unified
    std::unordered_set<ItemTypeId> separated_;
};

}  // namespace finevox
