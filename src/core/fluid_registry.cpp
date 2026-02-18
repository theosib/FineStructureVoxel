#include "finevox/core/fluid_registry.hpp"

namespace finevox {

FluidRegistry& FluidRegistry::global() {
    static FluidRegistry instance;
    return instance;
}

bool FluidRegistry::registerType(const std::string& name, FluidType type) {
    FluidTypeId id = FluidTypeId::fromName(name);
    type.id = id;
    type.name = name;

    std::unique_lock lock(mutex_);
    auto [it, inserted] = types_.emplace(id, std::move(type));
    return inserted;
}

const FluidType* FluidRegistry::getType(FluidTypeId id) const {
    std::shared_lock lock(mutex_);
    auto it = types_.find(id);
    return it != types_.end() ? &it->second : nullptr;
}

const FluidType* FluidRegistry::getType(const std::string& name) const {
    FluidTypeId id = FluidTypeId::fromName(name);
    return getType(id);
}

bool FluidRegistry::hasType(FluidTypeId id) const {
    std::shared_lock lock(mutex_);
    return types_.count(id) > 0;
}

FluidTypeId FluidRegistry::getTypeId(const std::string& name) const {
    FluidTypeId id = FluidTypeId::fromName(name);
    std::shared_lock lock(mutex_);
    return types_.count(id) > 0 ? id : EMPTY_FLUID_TYPE;
}

size_t FluidRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

void FluidRegistry::forEachType(const std::function<void(FluidTypeId, const FluidType&)>& fn) const {
    std::shared_lock lock(mutex_);
    for (const auto& [id, type] : types_) {
        fn(id, type);
    }
}

void FluidRegistry::clear() {
    std::unique_lock lock(mutex_);
    types_.clear();
}

}  // namespace finevox
