#include "finevox/core/string_interner.hpp"

namespace finevox {

StringInterner& StringInterner::global() {
    static StringInterner instance;
    return instance;
}

StringInterner::StringInterner() {
    // Reserve ID 0 as "" (empty/null across all ID types; BlockTypeId::name() returns "finevox:air" for ID 0)
    // Reserve IDs 1, 2 for special block types
    strings_.emplace_back("");                  // ID 0: Empty / Air
    strings_.emplace_back("finevox:invalid");   // ID 1: Invalid
    strings_.emplace_back("finevox:unknown");   // ID 2: Unknown

    // Add to lookup for reverse mapping
    // Both empty string and "finevox:air" map to AIR_INTERNED_ID (0)
    lookup_[""] = AIR_INTERNED_ID;
    lookup_["finevox:air"] = AIR_INTERNED_ID;
    lookup_["finevox:invalid"] = INVALID_INTERNED_ID;
    lookup_["finevox:unknown"] = UNKNOWN_INTERNED_ID;
}

InternedId StringInterner::intern(std::string_view str) {
    // Fast path: check if already interned (read lock, transparent string_view lookup)
    {
        std::shared_lock readLock(mutex_);
        auto it = lookup_.find(str);
        if (it != lookup_.end()) {
            return it->second;
        }
    }

    // Slow path: need to intern (write lock)
    std::unique_lock writeLock(mutex_);

    // Double-check after acquiring write lock
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        return it->second;
    }

    // Add new string
    InternedId id = static_cast<InternedId>(strings_.size());
    strings_.emplace_back(str);
    lookup_.emplace(strings_.back(), id);

    return id;
}

std::string_view StringInterner::lookup(InternedId id) const {
    std::shared_lock lock(mutex_);
    if (id >= strings_.size()) {
        return {};
    }
    return strings_[id];
}

std::optional<InternedId> StringInterner::find(std::string_view str) const {
    std::shared_lock lock(mutex_);
    auto it = lookup_.find(str);
    if (it != lookup_.end()) {
        return it->second;
    }
    return std::nullopt;
}

size_t StringInterner::size() const {
    std::shared_lock lock(mutex_);
    return strings_.size();
}

// BlockTypeId implementation

BlockTypeId BlockTypeId::fromName(std::string_view name) {
    if (name.empty() || name == "finevox:air") {
        return AIR_BLOCK_TYPE;
    }
    return BlockTypeId{StringInterner::global().intern(name)};
}

std::string_view BlockTypeId::name() const {
    if (id == AIR_INTERNED_ID) {
        return "finevox:air";
    }
    return StringInterner::global().lookup(id);
}

}  // namespace finevox
