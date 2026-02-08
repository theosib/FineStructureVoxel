#pragma once

/**
 * @file tag_registry.hpp
 * @brief Bidirectional tag<->item/block mapping with composition
 *
 * Design: Phase 14 Tags, Unification & Crafting Infrastructure
 *
 * Tags are applied to both items and blocks via raw InternedId.
 * Tag composition allows a tag to include other tags (transitive).
 * The resolved (transitive closure) state is computed by rebuild().
 *
 * Thread-safe singleton (shared_mutex).
 */

#include "finevox/core/tag.hpp"
#include "finevox/core/item_type.hpp"
#include "finevox/core/string_interner.hpp"

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox {

// Forward declaration
class UnificationRegistry;

class TagRegistry {
public:
    /// Get the global tag registry instance
    static TagRegistry& global();

    // ========================================================================
    // Tag definition (pre-resolution)
    // ========================================================================

    /// Add a direct member to a tag. Invalidates resolved cache.
    void addMember(TagId tag, InternedId member);

    /// Add an include (composition) from one tag to another.
    void addInclude(TagId tag, TagId included);

    /// Convenience: add member by ItemTypeId
    void addMember(TagId tag, ItemTypeId item) { addMember(tag, item.id); }

    /// Convenience: add member by BlockTypeId
    void addMember(TagId tag, BlockTypeId block) { addMember(tag, block.id); }

    // ========================================================================
    // Resolution
    // ========================================================================

    /// Resolve all tag composition (transitive closure).
    /// Detects and logs cycles. Must be called after all tags are loaded.
    /// @return true if no cycles detected
    bool rebuild();

    /// Check whether resolved data is current
    [[nodiscard]] bool isResolved() const;

    // ========================================================================
    // Queries (post-resolution)
    // ========================================================================

    /// Check if a member has a specific tag
    [[nodiscard]] bool hasTag(InternedId member, TagId tag) const;

    /// Get all tags for a member
    [[nodiscard]] std::vector<TagId> getTagsFor(InternedId member) const;

    /// Get all members of a tag (resolved, includes transitive)
    [[nodiscard]] std::vector<InternedId> getMembersOf(TagId tag) const;

    /// Convenience overloads
    [[nodiscard]] bool hasTag(ItemTypeId item, TagId tag) const { return hasTag(item.id, tag); }
    [[nodiscard]] bool hasTag(BlockTypeId block, TagId tag) const { return hasTag(block.id, tag); }
    [[nodiscard]] std::vector<TagId> getTagsFor(ItemTypeId item) const { return getTagsFor(item.id); }
    [[nodiscard]] std::vector<TagId> getTagsFor(BlockTypeId block) const { return getTagsFor(block.id); }

    // ========================================================================
    // Introspection
    // ========================================================================

    /// Number of defined tags
    [[nodiscard]] size_t tagCount() const;

    /// Get all defined tag IDs
    [[nodiscard]] std::vector<TagId> allTags() const;

    /// Reset all data (for testing)
    void clear();

    // Non-copyable singleton
    TagRegistry(const TagRegistry&) = delete;
    TagRegistry& operator=(const TagRegistry&) = delete;

private:
    TagRegistry() = default;

    struct RawTagData {
        std::unordered_set<InternedId> directMembers;
        std::unordered_set<TagId> includes;
    };

    struct ResolvedTagData {
        std::unordered_set<InternedId> members;
    };

    bool resolveTag(TagId tag,
                    std::unordered_set<TagId>& visiting,
                    std::unordered_set<TagId>& resolved);

    mutable std::shared_mutex mutex_;

    std::unordered_map<TagId, RawTagData> rawTags_;
    std::unordered_map<TagId, ResolvedTagData> resolvedTags_;
    std::unordered_map<InternedId, std::unordered_set<TagId>> memberToTags_;
    bool resolved_ = false;
};

// ============================================================================
// Free functions for loading .tag files
// ============================================================================

/// Load a .tag file that may contain tag definitions, unify blocks,
/// and separate directives. Dispatches to both registries.
/// @return Number of directives processed, or -1 on error
int loadTagFile(const std::string& path, TagRegistry& tags, UnificationRegistry& unify);

/// Load from string content (for testing)
int loadTagFileFromString(std::string_view content, TagRegistry& tags, UnificationRegistry& unify);

}  // namespace finevox
