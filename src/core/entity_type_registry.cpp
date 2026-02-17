#include "finevox/core/entity_type_registry.hpp"

namespace finevox {

EntityTypeRegistry& EntityTypeRegistry::global() {
    static EntityTypeRegistry instance;
    return instance;
}

bool EntityTypeRegistry::registerType(std::string_view name, EntityTypeDef def) {
    auto id = EntityTypeId::fromName(name);

    std::unique_lock lock(mutex_);
    if (types_.find(id) != types_.end()) {
        return false;  // Already registered
    }

    def.name = std::string(name);
    types_.emplace(id, std::move(def));
    return true;
}

const EntityTypeDef* EntityTypeRegistry::getType(EntityTypeId id) const {
    std::shared_lock lock(mutex_);
    auto it = types_.find(id);
    return it != types_.end() ? &it->second : nullptr;
}

const EntityTypeDef* EntityTypeRegistry::getType(std::string_view name) const {
    auto found = StringInterner::global().find(name);
    if (!found) return nullptr;
    return getType(EntityTypeId{*found});
}

bool EntityTypeRegistry::hasType(EntityTypeId id) const {
    std::shared_lock lock(mutex_);
    return types_.find(id) != types_.end();
}

bool EntityTypeRegistry::hasType(std::string_view name) const {
    auto found = StringInterner::global().find(name);
    if (!found) return false;
    return hasType(EntityTypeId{*found});
}

size_t EntityTypeRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

void EntityTypeRegistry::forEachType(const std::function<void(EntityTypeId, const EntityTypeDef&)>& fn) const {
    std::shared_lock lock(mutex_);
    for (const auto& [id, def] : types_) {
        fn(id, def);
    }
}

void EntityTypeRegistry::clear() {
    std::unique_lock lock(mutex_);
    types_.clear();
}

}  // namespace finevox
