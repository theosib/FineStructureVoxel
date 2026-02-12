#pragma once

/**
 * @file script_block_handler.hpp
 * @brief BlockHandler subclass that delegates to finescript event handlers
 *
 * Each ScriptBlockHandler owns a persistent ExecutionContext containing
 * the closures registered via `on :place do ... end` etc.
 */

#include "finevox/core/block_handler.hpp"
#include <finescript/script_engine.h>
#include <finescript/execution_context.h>
#include <string>
#include <unordered_map>

namespace finevox::script {

class ScriptBlockHandler : public BlockHandler {
public:
    ScriptBlockHandler(const std::string& name,
                       finescript::ScriptEngine& engine,
                       std::unique_ptr<finescript::ExecutionContext> ctx);

    [[nodiscard]] std::string_view name() const override;

    void onPlace(BlockContext& ctx) override;
    void onBreak(BlockContext& ctx) override;
    void onTick(BlockContext& ctx, TickType type) override;
    void onNeighborChanged(BlockContext& ctx, Face changedFace) override;
    void onBlockUpdate(BlockContext& ctx) override;
    bool onUse(BlockContext& ctx, Face face) override;
    bool onHit(BlockContext& ctx, Face face) override;
    void onRepaint(BlockContext& ctx) override;

    /// Check if this handler has any event handlers registered
    [[nodiscard]] bool hasHandlers() const { return !handlers_.empty(); }

    /// Access the persistent execution context
    [[nodiscard]] finescript::ExecutionContext& context() { return *ctx_; }

private:
    /// Look up and invoke a handler closure, setting up ctx/data proxies
    finescript::Value invokeHandler(uint32_t eventSymbol, BlockContext& blockCtx);

    std::string name_;
    finescript::ScriptEngine& engine_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;

    // Cached handler closures keyed by event symbol ID
    std::unordered_map<uint32_t, finescript::Value> handlers_;
};

}  // namespace finevox::script
