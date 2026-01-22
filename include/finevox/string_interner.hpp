#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <optional>

namespace finevox {

// Interned string ID - used for cheap comparison of block type names
using InternedId = uint32_t;
constexpr InternedId INVALID_INTERNED_ID = 0;

// Thread-safe string interner for block type names
// Strings are interned once and never removed (lifetime of engine)
//
// Usage:
//   auto& interner = StringInterner::global();
//   InternedId id = interner.intern("blockgame:stone");
//   std::string_view name = interner.lookup(id);
//
class StringInterner {
public:
    // Get the global interner instance (singleton)
    static StringInterner& global();

    // Intern a string, returning its ID
    // Thread-safe, returns same ID for duplicate strings
    // ID 0 is reserved for invalid/air
    [[nodiscard]] InternedId intern(std::string_view str);

    // Look up a string by ID
    // Returns empty string_view if ID is invalid
    [[nodiscard]] std::string_view lookup(InternedId id) const;

    // Check if a string is already interned
    // Returns nullopt if not interned
    [[nodiscard]] std::optional<InternedId> find(std::string_view str) const;

    // Get total number of interned strings (including reserved ID 0)
    [[nodiscard]] size_t size() const;

    // Non-copyable, non-movable (singleton)
    StringInterner(const StringInterner&) = delete;
    StringInterner& operator=(const StringInterner&) = delete;

private:
    StringInterner();

    mutable std::shared_mutex mutex_;
    std::vector<std::string> strings_;  // Index = ID, value = string
    std::unordered_map<std::string, InternedId> lookup_;  // Fast reverse lookup (owns strings)
};

// Convenience wrapper for block type IDs
// Provides type safety over raw InternedId
struct BlockTypeId {
    InternedId id = INVALID_INTERNED_ID;

    constexpr BlockTypeId() = default;
    constexpr explicit BlockTypeId(InternedId id_) : id(id_) {}

    // Create from string name (interns if not already)
    [[nodiscard]] static BlockTypeId fromName(std::string_view name);

    // Get the string name
    [[nodiscard]] std::string_view name() const;

    // Check if this is the invalid/air type
    [[nodiscard]] constexpr bool isValid() const { return id != INVALID_INTERNED_ID; }
    [[nodiscard]] constexpr bool isAir() const { return id == INVALID_INTERNED_ID; }

    constexpr bool operator==(const BlockTypeId&) const = default;
    constexpr auto operator<=>(const BlockTypeId&) const = default;
};

// Air/invalid block type constant
constexpr BlockTypeId AIR_BLOCK_TYPE{};

}  // namespace finevox

// Hash specialization for BlockTypeId
template<>
struct std::hash<finevox::BlockTypeId> {
    size_t operator()(const finevox::BlockTypeId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
