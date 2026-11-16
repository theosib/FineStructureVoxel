#include "finevox/core/entity_registry.hpp"
#include "finevox/core/global_registries.hpp"
#include <mutex>

namespace finevox {

EntityRegistry& EntityRegistry::global() {
    static EntityRegistry instance;
    static const bool joined = (GlobalRegistries::join(
        "EntityRegistry", [] { instance.clear(); },
        [] { return instance.size(); }), true);
    (void)joined;
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

void EntityRegistry::clear() {
    std::unique_lock lock(mutex_);
    types_.clear();
}

}  // namespace finevox
