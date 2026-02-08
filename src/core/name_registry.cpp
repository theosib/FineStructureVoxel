#include "finevox/core/name_registry.hpp"
#include "finevox/core/data_container.hpp"

namespace finevox {

NameRegistry::NameRegistry() {
    // Reserve ID 0 as empty/none
    names_.emplace_back("");  // ID 0 = empty string
}

NameRegistry::NameRegistry(NameRegistry&& other) {
    std::unique_lock lock(other.mutex_);
    names_ = std::move(other.names_);
    lookup_ = std::move(other.lookup_);
}

NameRegistry& NameRegistry::operator=(NameRegistry&& other) {
    if (this != &other) {
        std::unique_lock lockThis(mutex_, std::defer_lock);
        std::unique_lock lockOther(other.mutex_, std::defer_lock);
        std::lock(lockThis, lockOther);
        names_ = std::move(other.names_);
        lookup_ = std::move(other.lookup_);
    }
    return *this;
}

NameRegistry::PersistentId NameRegistry::getOrAssign(std::string_view name) {
    // Fast path: read lock
    {
        std::shared_lock lock(mutex_);
        auto it = lookup_.find(std::string(name));
        if (it != lookup_.end()) {
            return it->second;
        }
    }

    // Slow path: write lock, assign new ID
    std::unique_lock lock(mutex_);
    // Double-check after acquiring write lock
    auto it = lookup_.find(std::string(name));
    if (it != lookup_.end()) {
        return it->second;
    }

    PersistentId newId = static_cast<PersistentId>(names_.size());
    names_.emplace_back(name);
    lookup_.emplace(std::string(name), newId);
    return newId;
}

std::string_view NameRegistry::getName(PersistentId id) const {
    std::shared_lock lock(mutex_);
    if (id >= names_.size()) {
        return {};
    }
    return names_[id];
}

std::optional<NameRegistry::PersistentId> NameRegistry::find(std::string_view name) const {
    std::shared_lock lock(mutex_);
    auto it = lookup_.find(std::string(name));
    if (it != lookup_.end()) {
        return it->second;
    }
    return std::nullopt;
}

size_t NameRegistry::size() const {
    std::shared_lock lock(mutex_);
    return names_.size();
}

void NameRegistry::saveTo(DataContainer& dc, std::string_view key) const {
    std::shared_lock lock(mutex_);

    // Save as array of strings (index = PersistentId)
    // Skip ID 0 (empty) — it's always implied
    std::vector<std::string> nameList;
    nameList.reserve(names_.size() > 1 ? names_.size() - 1 : 0);
    for (size_t i = 1; i < names_.size(); ++i) {
        nameList.push_back(names_[i]);
    }

    dc.set(key, std::move(nameList));
}

NameRegistry NameRegistry::loadFrom(const DataContainer& dc, std::string_view key) {
    NameRegistry registry;

    auto nameList = dc.get<std::vector<std::string>>(key);
    if (nameList.empty()) {
        return registry;
    }

    // Rebuild from saved array (index+1 = PersistentId, since we skip ID 0)
    registry.names_.reserve(1 + nameList.size());
    for (auto& name : nameList) {
        PersistentId id = static_cast<PersistentId>(registry.names_.size());
        registry.lookup_.emplace(name, id);
        registry.names_.push_back(std::move(name));
    }

    return registry;
}

}  // namespace finevox
