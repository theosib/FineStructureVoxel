#include "finevox/core/item_registry.hpp"

namespace finevox {

ItemRegistry& ItemRegistry::global() {
    static ItemRegistry instance;
    return instance;
}

bool ItemRegistry::registerType(const ItemType& type) {
    std::unique_lock lock(mutex_);
    if (types_.contains(type.id)) {
        return false;
    }
    types_.emplace(type.id, type);
    return true;
}

bool ItemRegistry::registerType(std::string_view name) {
    ItemType type;
    type.id = ItemTypeId::fromName(name);
    return registerType(type);
}

const ItemType* ItemRegistry::getType(ItemTypeId id) const {
    std::shared_lock lock(mutex_);
    auto it = types_.find(id);
    if (it != types_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ItemType* ItemRegistry::getType(std::string_view name) const {
    return getType(ItemTypeId::fromName(name));
}

bool ItemRegistry::hasType(ItemTypeId id) const {
    std::shared_lock lock(mutex_);
    return types_.contains(id);
}

bool ItemRegistry::hasType(std::string_view name) const {
    return hasType(ItemTypeId::fromName(name));
}

size_t ItemRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

void ItemRegistry::registerBlockItems() {
    // TODO: Iterate BlockRegistry and create items for each block.
    // Requires adding a forEach method to BlockRegistry.
    // For now, block items are registered manually by game modules.
}

}  // namespace finevox
