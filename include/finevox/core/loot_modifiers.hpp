#pragma once

/**
 * @file loot_modifiers.hpp
 * @brief Built-in LootModifier implementations
 */

#include "finevox/core/loot_table.hpp"
#include <functional>

namespace finevox {

// ============================================================================
// FortuneCountModifier — multiply count by fortune level
// Formula: count * (1 + random(0, fortune * multiplier))
// ============================================================================

class FortuneCountModifier : public LootModifier {
public:
    explicit FortuneCountModifier(float multiplier = 1.0f)
        : multiplier_(multiplier) {}

    void apply(std::vector<ItemStack>& items, const LootContext& ctx) const override {
        if (ctx.fortuneLevel <= 0) return;
        auto& rng = getLootRng(ctx);
        int32_t maxBonus = static_cast<int32_t>(ctx.fortuneLevel * multiplier_);
        if (maxBonus <= 0) return;
        std::uniform_int_distribution<int32_t> dist(0, maxBonus);
        for (auto& item : items) {
            item.count *= (1 + dist(rng));
        }
    }

    [[nodiscard]] std::unique_ptr<LootModifier> clone() const override {
        return std::make_unique<FortuneCountModifier>(multiplier_);
    }

    [[nodiscard]] float multiplier() const { return multiplier_; }

private:
    float multiplier_;
};

// ============================================================================
// SetCountModifier — override count to a random range
// ============================================================================

class SetCountModifier : public LootModifier {
public:
    SetCountModifier(int32_t min, int32_t max) : min_(min), max_(max) {}

    void apply(std::vector<ItemStack>& items, const LootContext& ctx) const override {
        auto& rng = getLootRng(ctx);
        for (auto& item : items) {
            if (min_ == max_) {
                item.count = min_;
            } else {
                std::uniform_int_distribution<int32_t> dist(min_, max_);
                item.count = dist(rng);
            }
        }
    }

    [[nodiscard]] std::unique_ptr<LootModifier> clone() const override {
        return std::make_unique<SetCountModifier>(min_, max_);
    }

private:
    int32_t min_, max_;
};

// ============================================================================
// LootingBonusModifier — add count per looting level (mob drops)
// count += random(0, lootingLevel * bonusPerLevel)
// ============================================================================

class LootingBonusModifier : public LootModifier {
public:
    explicit LootingBonusModifier(int32_t bonusPerLevel = 1)
        : bonusPerLevel_(bonusPerLevel) {}

    void apply(std::vector<ItemStack>& items, const LootContext& ctx) const override {
        if (ctx.lootingLevel <= 0) return;
        auto& rng = getLootRng(ctx);
        int32_t maxBonus = ctx.lootingLevel * bonusPerLevel_;
        if (maxBonus <= 0) return;
        std::uniform_int_distribution<int32_t> dist(0, maxBonus);
        for (auto& item : items) {
            item.count += dist(rng);
        }
    }

    [[nodiscard]] std::unique_ptr<LootModifier> clone() const override {
        return std::make_unique<LootingBonusModifier>(bonusPerLevel_);
    }

private:
    int32_t bonusPerLevel_;
};

// ============================================================================
// CallbackModifier — std::function-based (for C++ extensibility)
// ============================================================================

class CallbackModifier : public LootModifier {
public:
    explicit CallbackModifier(
        std::function<void(std::vector<ItemStack>&, const LootContext&)> cb)
        : callback_(std::move(cb)) {}

    void apply(std::vector<ItemStack>& items, const LootContext& ctx) const override {
        callback_(items, ctx);
    }

    [[nodiscard]] std::unique_ptr<LootModifier> clone() const override {
        return std::make_unique<CallbackModifier>(callback_);
    }

private:
    std::function<void(std::vector<ItemStack>&, const LootContext&)> callback_;
};

}  // namespace finevox
