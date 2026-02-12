#pragma once

/**
 * @file block_context_proxy.hpp
 * @brief ProxyMap wrapping BlockContext for script access
 *
 * Allows scripts to read block state via map field access:
 *   ctx.pos, ctx.block_type, ctx.rotation, ctx.is_air, etc.
 *
 * Action methods (notify_neighbors, schedule_tick, etc.) are registered
 * as native functions in GameScriptEngine rather than being proxy fields.
 */

#include "finevox/core/block_handler.hpp"
#include <finescript/proxy_map.h>
#include <finescript/value.h>

namespace finevox::script {

class BlockContextProxy : public finescript::ProxyMap {
public:
    explicit BlockContextProxy(BlockContext& ctx);

    finescript::Value get(uint32_t key) const override;
    void set(uint32_t key, finescript::Value value) override;
    bool has(uint32_t key) const override;
    bool remove(uint32_t key) override;
    std::vector<uint32_t> keys() const override;

private:
    BlockContext& ctx_;
};

}  // namespace finevox::script
