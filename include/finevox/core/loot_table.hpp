#pragma once

/**
 * @file loot_table.hpp
 * @brief Generalized loot system — produces ItemStacks when "rolled"
 *
 * LootTable is context-agnostic: any game system (block breaks, mob deaths,
 * loot chests, fishing, etc.) can roll a table with a LootContext to get items.
 */

#include "finevox/core/string_interner.hpp"
#include "finevox/core/item_stack.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/entity_state.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace finevox {

class DataContainer;

// ============================================================================
// LootTableId — type-safe interned wrapper (same pattern as TagId, SoundSetId)
// ============================================================================

struct LootTableId {
    InternedId id = 0;

    constexpr LootTableId() = default;
    constexpr explicit LootTableId(InternedId id_) : id(id_) {}

    [[nodiscard]] static LootTableId fromName(std::string_view name) {
        return LootTableId{StringInterner::global().intern(name)};
    }

    [[nodiscard]] std::string_view name() const {
        return StringInterner::global().lookup(id);
    }

    [[nodiscard]] constexpr bool isEmpty() const { return id == 0; }
    [[nodiscard]] constexpr bool isValid() const { return id != 0; }

    constexpr bool operator==(const LootTableId&) const = default;
    constexpr auto operator<=>(const LootTableId&) const = default;
};

constexpr LootTableId EMPTY_LOOT_TABLE{};

// ============================================================================
// LootContext — contextual data bag for rolling loot
// ============================================================================

struct LootContext {
    BlockTypeId brokenBlock;                        // Block broken (empty if N/A)
    ItemTypeId toolUsed;                            // Tool held (empty if bare hand)
    int32_t bountyLevel = 0;                        // Bounty enchantment level
    int32_t plunderLevel = 0;                       // Plunder enchantment level
    bool preciseBreak = false;                      // Precise break on tool?
    BlockCoord position{0, 0, 0};                     // World position
    EntityId killerEntity = INVALID_ENTITY_ID;      // Who caused this
    EntityId sourceEntity = INVALID_ENTITY_ID;      // Entity dropping loot
    DataContainer* extraData = nullptr;             // Script-extensible data
    uint64_t seed = 0;                              // 0 = use thread-local RNG
};

// ============================================================================
// LootCondition — abstract predicate gating entries/pools
// ============================================================================

class LootCondition {
public:
    virtual ~LootCondition() = default;
    [[nodiscard]] virtual bool test(const LootContext& ctx) const = 0;
    [[nodiscard]] virtual std::unique_ptr<LootCondition> clone() const = 0;
};

// ============================================================================
// LootModifier — abstract post-processor for generated items
// ============================================================================

class LootModifier {
public:
    virtual ~LootModifier() = default;
    virtual void apply(std::vector<ItemStack>& items, const LootContext& ctx) const = 0;
    [[nodiscard]] virtual std::unique_ptr<LootModifier> clone() const = 0;
};

// ============================================================================
// LootEntry — single possible drop outcome
// ============================================================================

struct LootEntry {
    enum class Type { Item, LootTableRef, Empty };

    Type type = Type::Item;
    ItemTypeId item;                                // For Item type
    int32_t countMin = 1;
    int32_t countMax = 1;
    LootTableId referencedTable;                    // For LootTableRef type
    float weight = 1.0f;

    std::vector<std::unique_ptr<LootCondition>> conditions;
    std::vector<std::unique_ptr<LootModifier>> modifiers;

    /// Generate items from this entry (using ctx's RNG)
    [[nodiscard]] std::vector<ItemStack> generate(const LootContext& ctx,
                                                   std::mt19937_64& rng) const;

    /// Check if all conditions pass
    [[nodiscard]] bool isEligible(const LootContext& ctx) const;

    /// Deep copy
    [[nodiscard]] LootEntry clone() const;
};

// ============================================================================
// LootPool — collection of weighted entries, rolled N times
// ============================================================================

struct LootPool {
    int32_t rollsMin = 1;
    int32_t rollsMax = 1;
    float bonusRollsPerLevel = 0.0f;

    std::vector<LootEntry> entries;
    std::vector<std::unique_ptr<LootCondition>> conditions;     // Pool-level
    std::vector<std::unique_ptr<LootModifier>> modifiers;       // Pool-level

    /// Roll this pool, producing items
    [[nodiscard]] std::vector<ItemStack> roll(const LootContext& ctx,
                                               std::mt19937_64& rng) const;

    /// Check if pool-level conditions pass
    [[nodiscard]] bool isEligible(const LootContext& ctx) const;

    /// Deep copy
    [[nodiscard]] LootPool clone() const;
};

// ============================================================================
// LootTable — collection of independently-rolled pools
// ============================================================================

class LootTable {
public:
    LootTable() = default;

    /// Add a pool
    void addPool(LootPool pool);

    /// Roll all pools and combine results
    [[nodiscard]] std::vector<ItemStack> roll(const LootContext& ctx) const;

    /// Access pools (for introspection/testing)
    [[nodiscard]] const std::vector<LootPool>& pools() const { return pools_; }

    /// Deep copy
    [[nodiscard]] LootTable clone() const;

    /// Check if table has any pools
    [[nodiscard]] bool empty() const { return pools_.empty(); }

private:
    std::vector<LootPool> pools_;
};

// ============================================================================
// RNG utility — get or seed the thread-local loot RNG
// ============================================================================

/// Get the loot RNG, optionally seeded from the context
std::mt19937_64& getLootRng(const LootContext& ctx);

}  // namespace finevox

// Hash specialization
template<>
struct std::hash<finevox::LootTableId> {
    size_t operator()(const finevox::LootTableId& id) const noexcept {
        return std::hash<uint32_t>{}(id.id);
    }
};
