#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>

namespace finevox {

// ============================================================================
// Singleton
// ============================================================================

TagRegistry& TagRegistry::global() {
    static TagRegistry instance;
    return instance;
}

// ============================================================================
// Tag definition (pre-resolution)
// ============================================================================

void TagRegistry::addMember(TagId tag, InternedId member) {
    std::unique_lock lock(mutex_);
    rawTags_[tag].directMembers.insert(member);
    resolved_ = false;
}

void TagRegistry::addInclude(TagId tag, TagId included) {
    std::unique_lock lock(mutex_);
    rawTags_[tag].includes.insert(included);
    resolved_ = false;
}

// ============================================================================
// Resolution — transitive closure with cycle detection
// ============================================================================

bool TagRegistry::rebuild() {
    std::unique_lock lock(mutex_);

    resolvedTags_.clear();
    memberToTags_.clear();

    std::unordered_set<TagId> visiting;
    std::unordered_set<TagId> resolved;
    bool noCycles = true;

    for (auto& [tag, _] : rawTags_) {
        if (!resolved.contains(tag)) {
            if (!resolveTag(tag, visiting, resolved)) {
                noCycles = false;
            }
        }
    }

    // Build reverse index: member → set of tags
    for (auto& [tag, data] : resolvedTags_) {
        for (auto member : data.members) {
            memberToTags_[member].insert(tag);
        }
    }

    resolved_ = true;
    return noCycles;
}

bool TagRegistry::resolveTag(TagId tag,
                              std::unordered_set<TagId>& visiting,
                              std::unordered_set<TagId>& resolved) {
    if (resolved.contains(tag)) return true;

    if (visiting.contains(tag)) {
        std::cerr << "[TagRegistry] Cycle detected involving tag '"
                  << tag.name() << "'\n";
        return false;
    }

    visiting.insert(tag);
    bool noCycles = true;

    auto it = rawTags_.find(tag);
    if (it == rawTags_.end()) {
        // Tag referenced but never defined — create empty resolved entry
        resolvedTags_[tag] = {};
        visiting.erase(tag);
        resolved.insert(tag);
        return true;
    }

    auto& raw = it->second;
    auto& resolvedData = resolvedTags_[tag];

    // Start with direct members
    resolvedData.members = raw.directMembers;

    // Resolve each included tag and merge its members
    for (auto included : raw.includes) {
        if (!resolveTag(included, visiting, resolved)) {
            noCycles = false;
            continue;  // Skip cyclic include
        }
        auto incIt = resolvedTags_.find(included);
        if (incIt != resolvedTags_.end()) {
            resolvedData.members.insert(incIt->second.members.begin(),
                                         incIt->second.members.end());
        }
    }

    visiting.erase(tag);
    resolved.insert(tag);
    return noCycles;
}

bool TagRegistry::isResolved() const {
    std::shared_lock lock(mutex_);
    return resolved_;
}

// ============================================================================
// Queries (post-resolution)
// ============================================================================

bool TagRegistry::hasTag(InternedId member, TagId tag) const {
    std::shared_lock lock(mutex_);
    auto it = memberToTags_.find(member);
    if (it == memberToTags_.end()) return false;
    return it->second.contains(tag);
}

std::vector<TagId> TagRegistry::getTagsFor(InternedId member) const {
    std::shared_lock lock(mutex_);
    auto it = memberToTags_.find(member);
    if (it == memberToTags_.end()) return {};
    return {it->second.begin(), it->second.end()};
}

std::vector<InternedId> TagRegistry::getMembersOf(TagId tag) const {
    std::shared_lock lock(mutex_);
    auto it = resolvedTags_.find(tag);
    if (it == resolvedTags_.end()) return {};
    return {it->second.members.begin(), it->second.members.end()};
}

// ============================================================================
// Introspection
// ============================================================================

size_t TagRegistry::tagCount() const {
    std::shared_lock lock(mutex_);
    return rawTags_.size();
}

std::vector<TagId> TagRegistry::allTags() const {
    std::shared_lock lock(mutex_);
    std::vector<TagId> result;
    result.reserve(rawTags_.size());
    for (auto& [tag, _] : rawTags_) {
        result.push_back(tag);
    }
    return result;
}

void TagRegistry::clear() {
    std::unique_lock lock(mutex_);
    rawTags_.clear();
    resolvedTags_.clear();
    memberToTags_.clear();
    resolved_ = false;
}

// ============================================================================
// .tag file parser — helper utilities
// ============================================================================

namespace {

/// Trim leading/trailing whitespace
std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

/// Split a string by a delimiter, trimming each part
std::vector<std::string_view> splitAndTrim(std::string_view s, char delim) {
    std::vector<std::string_view> parts;
    while (!s.empty()) {
        auto pos = s.find(delim);
        auto part = trim(s.substr(0, pos));
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (pos == std::string_view::npos) break;
        s.remove_prefix(pos + 1);
    }
    return parts;
}

/// Check if a line starts with a keyword (followed by space or end)
bool startsWith(std::string_view line, std::string_view prefix) {
    if (line.size() < prefix.size()) return false;
    if (line.substr(0, prefix.size()) != prefix) return false;
    return line.size() == prefix.size() || line[prefix.size()] == ' '
           || line[prefix.size()] == '\t';
}

/// Extract the rest of a line after a keyword
std::string_view afterKeyword(std::string_view line, std::string_view keyword) {
    auto rest = line.substr(keyword.size());
    return trim(rest);
}

}  // anonymous namespace

// ============================================================================
// .tag file parser
// ============================================================================

int loadTagFileFromString(std::string_view content,
                           TagRegistry& tags,
                           UnificationRegistry& unify) {
    enum class State { None, TagBlock, UnifyBlock };
    State state = State::None;

    TagId currentTag;
    ItemTypeId unifyCanonical;
    std::vector<ItemTypeId> unifyMembers;
    bool unifyAutoConvert = true;

    int directives = 0;

    std::istringstream stream{std::string(content)};
    std::string lineStr;
    int lineNum = 0;

    while (std::getline(stream, lineStr)) {
        ++lineNum;
        auto line = trim(std::string_view{lineStr});

        // Skip empty lines and comments
        if (line.empty() || line.front() == '#') continue;

        if (state == State::None) {
            // Look for block openers or single-line directives

            if (startsWith(line, "tag")) {
                auto rest = afterKeyword(line, "tag");
                // Strip trailing '{'
                if (!rest.empty() && rest.back() == '{') {
                    rest = trim(rest.substr(0, rest.size() - 1));
                }
                if (rest.empty()) {
                    std::cerr << "[TagLoader] Line " << lineNum
                              << ": missing tag name\n";
                    return -1;
                }
                currentTag = TagId::fromName(rest);
                state = State::TagBlock;
                ++directives;
            }
            else if (startsWith(line, "unify")) {
                auto rest = afterKeyword(line, "unify");
                if (!rest.empty() && rest.back() == '{') {
                    rest = trim(rest.substr(0, rest.size() - 1));
                }
                if (rest.empty()) {
                    std::cerr << "[TagLoader] Line " << lineNum
                              << ": missing unify group name\n";
                    return -1;
                }
                // The name after "unify" is descriptive; canonical comes from inside
                unifyCanonical = ItemTypeId{};
                unifyMembers.clear();
                unifyAutoConvert = true;
                state = State::UnifyBlock;
                ++directives;
            }
            else if (startsWith(line, "separate")) {
                auto rest = afterKeyword(line, "separate");
                auto items = splitAndTrim(rest, ',');
                if (items.empty()) {
                    std::cerr << "[TagLoader] Line " << lineNum
                              << ": separate directive with no items\n";
                    return -1;
                }
                std::vector<ItemTypeId> separateItems;
                separateItems.reserve(items.size());
                for (auto name : items) {
                    separateItems.push_back(ItemTypeId::fromName(name));
                }
                unify.declareSeparate(separateItems);
                ++directives;
            }
            else {
                std::cerr << "[TagLoader] Line " << lineNum
                          << ": unexpected line: " << line << '\n';
                return -1;
            }
        }
        else if (state == State::TagBlock) {
            if (line == "}") {
                state = State::None;
            }
            else if (startsWith(line, "include")) {
                auto tagName = afterKeyword(line, "include");
                if (tagName.empty()) {
                    std::cerr << "[TagLoader] Line " << lineNum
                              << ": include with no tag name\n";
                    return -1;
                }
                tags.addInclude(currentTag, TagId::fromName(tagName));
            }
            else {
                // Plain member name
                auto& interner = StringInterner::global();
                InternedId memberId = interner.intern(line);
                tags.addMember(currentTag, memberId);
            }
        }
        else if (state == State::UnifyBlock) {
            if (line == "}") {
                // Finalize the unify group
                if (!unifyCanonical.isEmpty() && !unifyMembers.empty()) {
                    unify.declareGroup(unifyCanonical, unifyMembers,
                                       unifyAutoConvert);
                } else if (!unifyMembers.empty()) {
                    // No explicit canonical — use first member
                    unify.declareGroup(unifyMembers.front(), unifyMembers,
                                       unifyAutoConvert);
                }
                state = State::None;
            }
            else if (startsWith(line, "canonical:")) {
                auto val = trim(line.substr(10));  // len("canonical:") == 10
                if (!val.empty()) {
                    unifyCanonical = ItemTypeId::fromName(val);
                }
            }
            else if (startsWith(line, "members:")) {
                auto val = trim(line.substr(8));  // len("members:") == 8
                auto items = splitAndTrim(val, ',');
                for (auto name : items) {
                    unifyMembers.push_back(ItemTypeId::fromName(name));
                }
            }
            else if (startsWith(line, "auto_convert:")) {
                auto val = trim(line.substr(13));  // len("auto_convert:") == 13
                unifyAutoConvert = (val == "true" || val == "1" || val == "yes");
            }
            else {
                std::cerr << "[TagLoader] Line " << lineNum
                          << ": unexpected line in unify block: " << line << '\n';
                return -1;
            }
        }
    }

    if (state != State::None) {
        std::cerr << "[TagLoader] Unexpected end of file inside block\n";
        return -1;
    }

    return directives;
}

int loadTagFile(const std::string& path,
                TagRegistry& tags,
                UnificationRegistry& unify) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "[TagLoader] Cannot open file: " << path << '\n';
        return -1;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    return loadTagFileFromString(ss.str(), tags, unify);
}

}  // namespace finevox
