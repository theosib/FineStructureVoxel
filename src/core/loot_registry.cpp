#include "finevox/core/loot_registry.hpp"

namespace finevox {

LootRegistry& LootRegistry::global() {
    static LootRegistry instance;
    return instance;
}

bool LootRegistry::registerTable(std::string_view name, LootTable table) {
    auto id = LootTableId::fromName(name);
    std::unique_lock lock(mutex_);
    auto [it, inserted] = tables_.emplace(id, std::move(table));
    return inserted;
}

const LootTable* LootRegistry::getTable(LootTableId id) const {
    std::shared_lock lock(mutex_);
    auto it = tables_.find(id);
    return it != tables_.end() ? &it->second : nullptr;
}

const LootTable* LootRegistry::getTable(std::string_view name) const {
    auto id = LootTableId::fromName(name);
    return getTable(id);
}

LootTableId LootRegistry::getTableId(std::string_view name) const {
    auto id = LootTableId::fromName(name);
    std::shared_lock lock(mutex_);
    return tables_.count(id) ? id : EMPTY_LOOT_TABLE;
}

bool LootRegistry::hasTable(LootTableId id) const {
    std::shared_lock lock(mutex_);
    return tables_.count(id) > 0;
}

bool LootRegistry::hasTable(std::string_view name) const {
    return hasTable(LootTableId::fromName(name));
}

std::vector<ItemStack> LootRegistry::roll(std::string_view name,
                                           const LootContext& ctx) const {
    auto* table = getTable(name);
    return table ? table->roll(ctx) : std::vector<ItemStack>{};
}

std::vector<ItemStack> LootRegistry::roll(LootTableId id,
                                           const LootContext& ctx) const {
    auto* table = getTable(id);
    return table ? table->roll(ctx) : std::vector<ItemStack>{};
}

size_t LootRegistry::size() const {
    std::shared_lock lock(mutex_);
    return tables_.size();
}

void LootRegistry::clear() {
    std::unique_lock lock(mutex_);
    tables_.clear();
}

}  // namespace finevox
