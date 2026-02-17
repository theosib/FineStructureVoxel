#pragma once

/**
 * @file loot_table_loader.hpp
 * @brief Loads loot tables from .loot config files
 *
 * File format (uses ConfigParser):
 *
 *   pool:default:
 *   rolls: 1
 *   entry:silk:
 *   type: item
 *   item: stone
 *   count: 1
 *   condition: precise-break
 *   entry:normal:
 *   type: item
 *   item: cobble
 *   count: 1-3
 *   condition: not precise-break
 *   modifier: bounty 1.0
 *
 * Parsing is sequential: pool:NAME: starts a pool, entry:NAME: starts
 * an entry within the current pool. Keys between pool and first entry
 * apply to the pool; keys after entry apply to the entry.
 */

#include "finevox/core/loot_table.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace finevox {

class LootTableLoader {
public:
    /// Load a loot table from a .loot file
    [[nodiscard]] static std::optional<LootTable> loadFromFile(const std::string& path);

    /// Load a loot table from a string (for testing)
    [[nodiscard]] static std::optional<LootTable> loadFromString(std::string_view content);

    /// Load all .loot files from a directory, registering each in LootRegistry.
    /// Returns number of tables successfully loaded.
    static size_t loadDirectory(const std::string& dirPath);

    /// Parse a condition from a config string (e.g., "precise-break", "random-chance 0.1 0.03")
    [[nodiscard]] static std::unique_ptr<LootCondition> parseCondition(std::string_view str);

    /// Parse a modifier from a config string (e.g., "bounty 1.0", "plunder-bonus 1")
    [[nodiscard]] static std::unique_ptr<LootModifier> parseModifier(std::string_view str);

private:
    /// Parse a range string like "1-3" into {min, max}; "5" becomes {5, 5}
    static std::pair<int32_t, int32_t> parseRange(std::string_view str);
};

}  // namespace finevox
