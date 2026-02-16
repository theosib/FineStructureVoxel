#pragma once

/**
 * @file loot_registry.hpp
 * @brief LootRegistry — global singleton for named loot tables
 */

#include "finevox/core/loot_table.hpp"
#include <shared_mutex>
#include <string_view>
#include <unordered_map>

namespace finevox {

class LootRegistry {
public:
    static LootRegistry& global();

    /// Register a loot table by name. Returns false if name already registered.
    bool registerTable(std::string_view name, LootTable table);

    /// Look up by LootTableId
    [[nodiscard]] const LootTable* getTable(LootTableId id) const;

    /// Look up by name
    [[nodiscard]] const LootTable* getTable(std::string_view name) const;

    /// Get the LootTableId for a name (returns empty if not registered)
    [[nodiscard]] LootTableId getTableId(std::string_view name) const;

    /// Check existence
    [[nodiscard]] bool hasTable(LootTableId id) const;
    [[nodiscard]] bool hasTable(std::string_view name) const;

    /// Convenience: roll a table by name
    [[nodiscard]] std::vector<ItemStack> roll(std::string_view name,
                                               const LootContext& ctx) const;

    /// Convenience: roll a table by ID
    [[nodiscard]] std::vector<ItemStack> roll(LootTableId id,
                                               const LootContext& ctx) const;

    /// Number of registered tables
    [[nodiscard]] size_t size() const;

    /// Clear all (for testing)
    void clear();

    LootRegistry(const LootRegistry&) = delete;
    LootRegistry& operator=(const LootRegistry&) = delete;

private:
    LootRegistry() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<LootTableId, LootTable> tables_;
};

}  // namespace finevox
