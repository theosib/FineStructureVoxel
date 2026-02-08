#include "finevox/core/unification.hpp"
#include "finevox/core/tag_registry.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>

namespace finevox {

// ============================================================================
// Singleton
// ============================================================================

UnificationRegistry& UnificationRegistry::global() {
    static UnificationRegistry instance;
    return instance;
}

// ============================================================================
// Helpers
// ============================================================================

std::string_view UnificationRegistry::baseName(std::string_view fullName) {
    auto pos = fullName.find(':');
    if (pos == std::string_view::npos) return fullName;
    return fullName.substr(pos + 1);
}

std::string_view UnificationRegistry::namespacePart(std::string_view fullName) {
    auto pos = fullName.find(':');
    if (pos == std::string_view::npos) return {};
    return fullName.substr(0, pos);
}

// ============================================================================
// Explicit definition
// ============================================================================

void UnificationRegistry::declareGroup(ItemTypeId canonical,
                                        const std::vector<ItemTypeId>& members,
                                        bool autoConvert) {
    std::unique_lock lock(mutex_);

    // Check if canonical is already in another group
    if (itemToGroup_.contains(canonical)) {
        std::cerr << "[Unification] Warning: '" << canonical.name()
                  << "' is already in a group, ignoring duplicate declareGroup\n";
        return;
    }

    size_t groupIdx = groups_.size();
    groups_.push_back({canonical, members, autoConvert});

    // Ensure canonical is in the members list
    auto& group = groups_.back();
    bool canonicalFound = false;
    for (auto& m : group.members) {
        if (m == canonical) { canonicalFound = true; break; }
    }
    if (!canonicalFound) {
        group.members.insert(group.members.begin(), canonical);
    }

    // Map all members to this group
    for (auto& m : group.members) {
        if (itemToGroup_.contains(m)) {
            std::cerr << "[Unification] Warning: '" << m.name()
                      << "' is already in another group, skipping\n";
            continue;
        }
        itemToGroup_[m] = groupIdx;
    }
}

void UnificationRegistry::declareSeparate(const std::vector<ItemTypeId>& items) {
    std::unique_lock lock(mutex_);
    for (auto& item : items) {
        separated_.insert(item);
    }
}

// ============================================================================
// Canonical selection
// ============================================================================

ItemTypeId UnificationRegistry::selectCanonical(
    const std::vector<ItemTypeId>& candidates,
    const TagRegistry& tags) const {

    if (candidates.empty()) return ItemTypeId{};
    if (candidates.size() == 1) return candidates[0];

    // Score each candidate: higher = better canonical
    struct Scored {
        ItemTypeId item;
        int priority;    // namespace priority
        size_t tagCount; // more tags = better
        InternedId id;   // lower = registered earlier
    };

    std::vector<Scored> scored;
    scored.reserve(candidates.size());

    for (auto& item : candidates) {
        auto ns = namespacePart(item.name());
        int priority = 0;
        if (ns.empty()) priority = 2;           // Unnamespaced = best
        else if (ns == "finevox") priority = 1;  // Engine namespace = good
        // Everything else = 0

        auto itemTags = tags.getTagsFor(item.id);
        scored.push_back({item, priority, itemTags.size(), item.id});
    }

    std::sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.tagCount != b.tagCount) return a.tagCount > b.tagCount;
        return a.id < b.id;
    });

    return scored[0].item;
}

// ============================================================================
// Auto-resolution
// ============================================================================

void UnificationRegistry::autoResolve(const TagRegistry& tags) {
    std::unique_lock lock(mutex_);

    // ====================================================================
    // Phase 1: Group by shared community tags (c:xxx)
    // ====================================================================

    // Collect all community tags and their members
    auto allTags = tags.allTags();
    // Map: base_name → candidates from different namespaces
    std::unordered_map<std::string, std::vector<ItemTypeId>> communityGroups;

    for (auto& tagId : allTags) {
        auto tagName = tagId.name();
        // Only consider community tags (start with "c:")
        if (tagName.size() < 2 || tagName.substr(0, 2) != "c:") continue;

        auto members = tags.getMembersOf(tagId);
        if (members.size() <= 1) continue;

        // Check if members span multiple namespaces
        std::unordered_set<std::string_view> namespaces;
        std::vector<ItemTypeId> candidates;

        for (auto memberId : members) {
            auto memberName = StringInterner::global().lookup(memberId);
            auto ns = namespacePart(memberName);
            namespaces.insert(ns);
            candidates.push_back(ItemTypeId{memberId});
        }

        if (namespaces.size() <= 1) continue;

        // Group by base name within this tag
        std::unordered_map<std::string, std::vector<ItemTypeId>> byBase;
        for (auto& item : candidates) {
            std::string base{baseName(item.name())};
            byBase[base].push_back(item);
        }

        for (auto& [base, group] : byBase) {
            if (group.size() <= 1) continue;

            // Filter out separated items
            std::vector<ItemTypeId> filtered;
            for (auto& item : group) {
                if (!separated_.contains(item)) {
                    filtered.push_back(item);
                }
            }
            if (filtered.size() <= 1) continue;

            // Check if already grouped
            bool anyGrouped = false;
            for (auto& item : filtered) {
                if (itemToGroup_.contains(item)) {
                    anyGrouped = true;
                    break;
                }
            }
            if (anyGrouped) continue;

            // Create the group
            auto canonical = selectCanonical(filtered, tags);
            size_t groupIdx = groups_.size();
            groups_.push_back({canonical, filtered, true});

            for (auto& item : filtered) {
                itemToGroup_[item] = groupIdx;
            }

            // Log the unification
            std::ostringstream oss;
            oss << "[Unification] Unified '" << canonical.name() << "': {";
            bool first = true;
            for (auto& item : filtered) {
                if (item == canonical) continue;
                if (!first) oss << ", ";
                oss << item.name();
                first = false;
            }
            oss << "}";
            std::cerr << oss.str() << '\n';

            // Warn about members that lack tags other members have
            for (auto& item : filtered) {
                auto itemTags = tags.getTagsFor(item.id);
                std::unordered_set<TagId> itemTagSet(itemTags.begin(), itemTags.end());

                for (auto& other : filtered) {
                    if (other == item) continue;
                    auto otherTags = tags.getTagsFor(other.id);
                    for (auto& otherTag : otherTags) {
                        auto otherTagName = otherTag.name();
                        if (otherTagName.substr(0, 2) == "c:" &&
                            !itemTagSet.contains(otherTag)) {
                            std::cerr << "[Unification] Warning: '"
                                      << item.name() << "' lacks tag '"
                                      << otherTagName
                                      << "' that other group members have\n";
                        }
                    }
                }
            }
        }
    }

    // ====================================================================
    // Phase 2: Group by base name (for items not yet grouped)
    // ====================================================================

    // Collect all tagged items that aren't in any group yet
    std::unordered_map<std::string, std::vector<ItemTypeId>> byBaseName;

    for (auto& tagId : allTags) {
        auto members = tags.getMembersOf(tagId);
        for (auto memberId : members) {
            ItemTypeId item{memberId};
            if (itemToGroup_.contains(item)) continue;
            if (separated_.contains(item)) continue;

            std::string base{baseName(item.name())};
            byBaseName[base].push_back(item);
        }
    }

    for (auto& [base, candidates] : byBaseName) {
        // Deduplicate (same item may appear under multiple tags)
        std::unordered_set<ItemTypeId> seen;
        std::vector<ItemTypeId> unique;
        for (auto& item : candidates) {
            if (seen.insert(item).second) {
                unique.push_back(item);
            }
        }
        if (unique.size() <= 1) continue;

        // Check if they span multiple namespaces
        std::unordered_set<std::string_view> namespaces;
        for (auto& item : unique) {
            namespaces.insert(namespacePart(item.name()));
        }
        if (namespaces.size() <= 1) continue;

        // Check for conflicting tag families
        // Collect the "tag families" (c:xxx prefix before last /) for each item
        bool conflict = false;
        std::unordered_map<ItemTypeId, std::unordered_set<std::string>> itemFamilies;

        for (auto& item : unique) {
            auto itemTags = tags.getTagsFor(item.id);
            for (auto& t : itemTags) {
                auto tName = t.name();
                if (tName.size() >= 2 && tName.substr(0, 2) == "c:") {
                    // Extract family: c:ingots/iron → c:ingots
                    auto slash = tName.rfind('/');
                    std::string family;
                    if (slash != std::string_view::npos) {
                        family = std::string(tName.substr(0, slash));
                    } else {
                        family = std::string(tName);
                    }
                    itemFamilies[item].insert(family);
                }
            }
        }

        // If items have different families, that's a conflict
        if (itemFamilies.size() >= 2) {
            // Collect union of all families per item, check for disagreement
            std::unordered_set<std::string> allFamilies;
            for (auto& [_, families] : itemFamilies) {
                allFamilies.insert(families.begin(), families.end());
            }

            // Check if any item is missing a family that another has
            for (auto& [item, families] : itemFamilies) {
                for (auto& f : allFamilies) {
                    if (!families.contains(f)) {
                        // Items have non-overlapping tag families
                        // Only flag as conflict if families are truly incompatible
                        // (e.g., one is c:dusts, another is c:gems)
                        bool hasOverlap = false;
                        for (auto& myFamily : families) {
                            if (allFamilies.contains(myFamily)) {
                                hasOverlap = true;
                                break;
                            }
                        }
                        if (!hasOverlap) {
                            conflict = true;
                        }
                    }
                }
            }
        }

        if (conflict) {
            // Log warning about conflicting families
            std::ostringstream oss;
            oss << "[Unification] Warning: Not auto-unifying '" << base
                << "': conflicting tag families (";
            bool first = true;
            for (auto& [item, families] : itemFamilies) {
                if (!first) oss << ", ";
                oss << namespacePart(item.name()) << ": ";
                bool ffirst = true;
                for (auto& f : families) {
                    if (!ffirst) oss << "+";
                    oss << f;
                    ffirst = false;
                }
                first = false;
            }
            oss << ")";
            std::cerr << oss.str() << '\n';
            continue;
        }

        // Filter out any already-grouped items (may have been grouped by phase 1
        // in a different iteration)
        std::vector<ItemTypeId> filtered;
        for (auto& item : unique) {
            if (!itemToGroup_.contains(item)) {
                filtered.push_back(item);
            }
        }
        if (filtered.size() <= 1) continue;

        // Create the group (inferred)
        auto canonical = selectCanonical(filtered, tags);
        size_t groupIdx = groups_.size();
        groups_.push_back({canonical, filtered, true});

        for (auto& item : filtered) {
            itemToGroup_[item] = groupIdx;
        }

        // Warn about inference
        std::cerr << "[Unification] Warning: Inferring equivalence for '"
                  << base << "' without shared c: tags\n";

        std::ostringstream oss;
        oss << "[Unification] Unified '" << canonical.name() << "': {";
        bool first = true;
        for (auto& item : filtered) {
            if (item == canonical) continue;
            if (!first) oss << ", ";
            oss << item.name();
            first = false;
        }
        oss << "}";
        std::cerr << oss.str() << '\n';
    }
}

// ============================================================================
// Tag propagation (Option A)
// ============================================================================

void UnificationRegistry::propagateTags(TagRegistry& tags) const {
    std::shared_lock lock(mutex_);

    for (auto& group : groups_) {
        // Collect union of all tags from all members
        std::unordered_set<TagId> allGroupTags;
        for (auto& member : group.members) {
            auto memberTags = tags.getTagsFor(member.id);
            allGroupTags.insert(memberTags.begin(), memberTags.end());
        }

        // Propagate missing tags to each member
        for (auto& member : group.members) {
            auto memberTags = tags.getTagsFor(member.id);
            std::unordered_set<TagId> existing(memberTags.begin(), memberTags.end());

            for (auto& tag : allGroupTags) {
                if (!existing.contains(tag)) {
                    tags.addMember(tag, member.id);
                }
            }
        }
    }
}

// ============================================================================
// Queries
// ============================================================================

ItemTypeId UnificationRegistry::resolve(ItemTypeId item) const {
    std::shared_lock lock(mutex_);
    auto it = itemToGroup_.find(item);
    if (it == itemToGroup_.end()) return item;
    auto& group = groups_[it->second];
    return group.autoConvert ? group.canonical : item;
}

bool UnificationRegistry::areEquivalent(ItemTypeId a, ItemTypeId b) const {
    if (a == b) return true;
    std::shared_lock lock(mutex_);
    auto itA = itemToGroup_.find(a);
    auto itB = itemToGroup_.find(b);
    if (itA == itemToGroup_.end() || itB == itemToGroup_.end()) return false;
    return itA->second == itB->second;
}

std::vector<ItemTypeId> UnificationRegistry::getGroup(ItemTypeId item) const {
    std::shared_lock lock(mutex_);
    auto it = itemToGroup_.find(item);
    if (it == itemToGroup_.end()) return {};
    return groups_[it->second].members;
}

ItemTypeId UnificationRegistry::getCanonical(ItemTypeId item) const {
    std::shared_lock lock(mutex_);
    auto it = itemToGroup_.find(item);
    if (it == itemToGroup_.end()) return item;
    return groups_[it->second].canonical;
}

bool UnificationRegistry::isAutoConvert(ItemTypeId item) const {
    std::shared_lock lock(mutex_);
    auto it = itemToGroup_.find(item);
    if (it == itemToGroup_.end()) return false;
    return groups_[it->second].autoConvert;
}

// ============================================================================
// Introspection
// ============================================================================

size_t UnificationRegistry::groupCount() const {
    std::shared_lock lock(mutex_);
    return groups_.size();
}

void UnificationRegistry::clear() {
    std::unique_lock lock(mutex_);
    groups_.clear();
    itemToGroup_.clear();
    separated_.clear();
}

}  // namespace finevox
