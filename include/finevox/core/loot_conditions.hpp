#pragma once

/**
 * @file loot_conditions.hpp
 * @brief Built-in LootCondition implementations
 */

#include "finevox/core/loot_table.hpp"
#include "finevox/core/tag.hpp"
#include "finevox/core/tag_registry.hpp"
#include <functional>

namespace finevox {

// ============================================================================
// AlwaysCondition — always true (default)
// ============================================================================

class AlwaysCondition : public LootCondition {
public:
    [[nodiscard]] bool test(const LootContext&) const override { return true; }
    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<AlwaysCondition>();
    }
};

// ============================================================================
// SilkTouchCondition — requires silk touch on tool
// ============================================================================

class SilkTouchCondition : public LootCondition {
public:
    [[nodiscard]] bool test(const LootContext& ctx) const override {
        return ctx.silkTouch;
    }
    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<SilkTouchCondition>();
    }
};

// ============================================================================
// ToolTagCondition — requires tool to have a specific tag
// ============================================================================

class ToolTagCondition : public LootCondition {
public:
    explicit ToolTagCondition(TagId tag) : tag_(tag) {}

    [[nodiscard]] bool test(const LootContext& ctx) const override {
        if (ctx.toolUsed.isEmpty()) return false;
        return TagRegistry::global().hasTag(ctx.toolUsed.id, tag_);
    }

    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<ToolTagCondition>(tag_);
    }

    [[nodiscard]] TagId tag() const { return tag_; }

private:
    TagId tag_;
};

// ============================================================================
// RandomChanceCondition — probability with optional fortune bonus
// ============================================================================

class RandomChanceCondition : public LootCondition {
public:
    explicit RandomChanceCondition(float chance, float fortuneBonus = 0.0f)
        : chance_(chance), fortuneBonus_(fortuneBonus) {}

    [[nodiscard]] bool test(const LootContext& ctx) const override {
        float effective = chance_ + fortuneBonus_ * ctx.fortuneLevel;
        if (effective >= 1.0f) return true;
        if (effective <= 0.0f) return false;
        auto& rng = getLootRng(ctx);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(rng) < effective;
    }

    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<RandomChanceCondition>(chance_, fortuneBonus_);
    }

    [[nodiscard]] float chance() const { return chance_; }
    [[nodiscard]] float fortuneBonus() const { return fortuneBonus_; }

private:
    float chance_;
    float fortuneBonus_;
};

// ============================================================================
// BlockTypeCondition — requires a specific broken block
// ============================================================================

class BlockTypeCondition : public LootCondition {
public:
    explicit BlockTypeCondition(BlockTypeId block) : block_(block) {}

    [[nodiscard]] bool test(const LootContext& ctx) const override {
        return ctx.brokenBlock == block_;
    }

    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<BlockTypeCondition>(block_);
    }

private:
    BlockTypeId block_;
};

// ============================================================================
// InvertedCondition — negates another condition
// ============================================================================

class InvertedCondition : public LootCondition {
public:
    explicit InvertedCondition(std::unique_ptr<LootCondition> inner)
        : inner_(std::move(inner)) {}

    [[nodiscard]] bool test(const LootContext& ctx) const override {
        return !inner_->test(ctx);
    }

    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<InvertedCondition>(inner_->clone());
    }

private:
    std::unique_ptr<LootCondition> inner_;
};

// ============================================================================
// CallbackCondition — std::function-based (for C++ extensibility)
// ============================================================================

class CallbackCondition : public LootCondition {
public:
    explicit CallbackCondition(std::function<bool(const LootContext&)> cb)
        : callback_(std::move(cb)) {}

    [[nodiscard]] bool test(const LootContext& ctx) const override {
        return callback_(ctx);
    }

    [[nodiscard]] std::unique_ptr<LootCondition> clone() const override {
        return std::make_unique<CallbackCondition>(callback_);
    }

private:
    std::function<bool(const LootContext&)> callback_;
};

}  // namespace finevox
