#include "finevox/core/loot_table.hpp"
#include "finevox/core/loot_registry.hpp"
#include <algorithm>
#include <numeric>

namespace finevox {

// ============================================================================
// RNG
// ============================================================================

static thread_local std::mt19937_64 g_lootRng{std::random_device{}()};

std::mt19937_64& getLootRng(const LootContext& ctx) {
    if (ctx.seed != 0) {
        g_lootRng.seed(ctx.seed);
    }
    return g_lootRng;
}

// ============================================================================
// LootEntry
// ============================================================================

bool LootEntry::isEligible(const LootContext& ctx) const {
    for (const auto& cond : conditions) {
        if (!cond->test(ctx)) return false;
    }
    return true;
}

std::vector<ItemStack> LootEntry::generate(const LootContext& ctx,
                                            std::mt19937_64& rng) const {
    std::vector<ItemStack> result;

    switch (type) {
        case Type::Item: {
            if (item.isEmpty()) break;
            ItemStack stack;
            stack.type = item;
            if (countMin == countMax) {
                stack.count = countMin;
            } else {
                std::uniform_int_distribution<int32_t> dist(countMin, countMax);
                stack.count = dist(rng);
            }
            if (stack.count > 0) {
                result.push_back(std::move(stack));
            }
            break;
        }
        case Type::LootTableRef: {
            if (referencedTable.isEmpty()) break;
            auto* table = LootRegistry::global().getTable(referencedTable);
            if (table) {
                result = table->roll(ctx);
            }
            break;
        }
        case Type::Empty:
            // Intentionally produces nothing
            break;
    }

    // Apply entry-level modifiers
    for (const auto& mod : modifiers) {
        mod->apply(result, ctx);
    }

    return result;
}

LootEntry LootEntry::clone() const {
    LootEntry copy;
    copy.type = type;
    copy.item = item;
    copy.countMin = countMin;
    copy.countMax = countMax;
    copy.referencedTable = referencedTable;
    copy.weight = weight;
    for (const auto& c : conditions) {
        copy.conditions.push_back(c->clone());
    }
    for (const auto& m : modifiers) {
        copy.modifiers.push_back(m->clone());
    }
    return copy;
}

// ============================================================================
// LootPool
// ============================================================================

bool LootPool::isEligible(const LootContext& ctx) const {
    for (const auto& cond : conditions) {
        if (!cond->test(ctx)) return false;
    }
    return true;
}

std::vector<ItemStack> LootPool::roll(const LootContext& ctx,
                                       std::mt19937_64& rng) const {
    if (!isEligible(ctx)) return {};

    // Compute roll count
    int32_t rolls = rollsMin;
    if (rollsMin != rollsMax) {
        std::uniform_int_distribution<int32_t> dist(rollsMin, rollsMax);
        rolls = dist(rng);
    }
    if (bonusRollsPerLevel > 0.0f) {
        rolls += static_cast<int32_t>(bonusRollsPerLevel * ctx.bountyLevel);
    }
    if (rolls <= 0) return {};

    // Filter eligible entries and compute weights
    std::vector<size_t> eligible;
    std::vector<float> weights;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].isEligible(ctx)) {
            eligible.push_back(i);
            weights.push_back(std::max(0.0f, entries[i].weight));
        }
    }

    if (eligible.empty()) return {};

    float totalWeight = std::accumulate(weights.begin(), weights.end(), 0.0f);
    if (totalWeight <= 0.0f) return {};

    std::vector<ItemStack> result;

    for (int32_t r = 0; r < rolls; ++r) {
        // Weighted random selection
        std::uniform_real_distribution<float> dist(0.0f, totalWeight);
        float pick = dist(rng);

        size_t selected = 0;
        float cumulative = 0.0f;
        for (size_t i = 0; i < eligible.size(); ++i) {
            cumulative += weights[i];
            if (pick < cumulative) {
                selected = eligible[i];
                break;
            }
            selected = eligible[i]; // fallback to last
        }

        auto items = entries[selected].generate(ctx, rng);
        result.insert(result.end(),
                      std::make_move_iterator(items.begin()),
                      std::make_move_iterator(items.end()));
    }

    // Apply pool-level modifiers
    for (const auto& mod : modifiers) {
        mod->apply(result, ctx);
    }

    return result;
}

LootPool LootPool::clone() const {
    LootPool copy;
    copy.rollsMin = rollsMin;
    copy.rollsMax = rollsMax;
    copy.bonusRollsPerLevel = bonusRollsPerLevel;
    for (const auto& e : entries) {
        copy.entries.push_back(e.clone());
    }
    for (const auto& c : conditions) {
        copy.conditions.push_back(c->clone());
    }
    for (const auto& m : modifiers) {
        copy.modifiers.push_back(m->clone());
    }
    return copy;
}

// ============================================================================
// LootTable
// ============================================================================

void LootTable::addPool(LootPool pool) {
    pools_.push_back(std::move(pool));
}

std::vector<ItemStack> LootTable::roll(const LootContext& ctx) const {
    auto& rng = getLootRng(ctx);
    std::vector<ItemStack> result;

    for (const auto& pool : pools_) {
        auto items = pool.roll(ctx, rng);
        result.insert(result.end(),
                      std::make_move_iterator(items.begin()),
                      std::make_move_iterator(items.end()));
    }

    return result;
}

LootTable LootTable::clone() const {
    LootTable copy;
    for (const auto& p : pools_) {
        copy.pools_.push_back(p.clone());
    }
    return copy;
}

}  // namespace finevox
