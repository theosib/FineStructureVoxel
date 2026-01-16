#include "finevox/string_interner.hpp"

namespace finevox {

StringInterner& StringInterner::global() {
    static StringInterner instance;
    return instance;
}

StringInterner::StringInterner() {
    // Reserve ID 0 for air/invalid
    strings_.emplace_back("");  // Empty string at index 0
}

InternedId StringInterner::intern(std::string_view str) {
    std::string key(str);  // Convert to string for map operations

    // Fast path: check if already interned (read lock)
    {
        std::shared_lock readLock(mutex_);
        auto it = lookup_.find(key);
        if (it != lookup_.end()) {
            return it->second;
        }
    }

    // Slow path: need to intern (write lock)
    std::unique_lock writeLock(mutex_);

    // Double-check after acquiring write lock
    auto it = lookup_.find(key);
    if (it != lookup_.end()) {
        return it->second;
    }

    // Add new string
    InternedId id = static_cast<InternedId>(strings_.size());
    strings_.emplace_back(str);
    lookup_[std::move(key)] = id;

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
    auto it = lookup_.find(std::string(str));
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
    if (name.empty()) {
        return AIR_BLOCK_TYPE;
    }
    return BlockTypeId{StringInterner::global().intern(name)};
}

std::string_view BlockTypeId::name() const {
    return StringInterner::global().lookup(id);
}

}  // namespace finevox
