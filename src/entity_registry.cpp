#include "finevox/entity_registry.hpp"

namespace finevox {

EntityRegistry& EntityRegistry::global() {
    static EntityRegistry instance;
    return instance;
}

bool EntityRegistry::registerType(std::string_view name) {
    std::unique_lock lock(mutex_);

    std::string nameStr(name);
    if (types_.find(nameStr) != types_.end()) {
        return false;  // Already registered
    }

    types_[nameStr] = true;
    return true;
}

bool EntityRegistry::hasType(std::string_view name) const {
    std::shared_lock lock(mutex_);
    return types_.find(std::string(name)) != types_.end();
}

size_t EntityRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

}  // namespace finevox
