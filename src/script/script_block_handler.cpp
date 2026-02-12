#include "finevox/script/script_block_handler.hpp"
#include "finevox/script/block_context_proxy.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/core/string_interner.hpp"
#include <iostream>

namespace finevox::script {

// Pre-interned event symbol IDs
struct EventSymbols {
    uint32_t place;
    uint32_t break_;
    uint32_t tick;
    uint32_t neighbor_changed;
    uint32_t block_update;
    uint32_t use;
    uint32_t hit;
    uint32_t repaint;

    // Extra context variable symbols
    uint32_t face;
    uint32_t tick_type;

    // Tick type symbols
    uint32_t scheduled;
    uint32_t repeat;
    uint32_t random;

    // Face symbols
    uint32_t pos_x, neg_x, pos_y, neg_y, pos_z, neg_z;

    static const EventSymbols& instance() {
        static EventSymbols syms = [] {
            auto& si = StringInterner::global();
            return EventSymbols{
                si.intern("place"),
                si.intern("break"),
                si.intern("tick"),
                si.intern("neighbor_changed"),
                si.intern("block_update"),
                si.intern("use"),
                si.intern("hit"),
                si.intern("repaint"),
                si.intern("face"),
                si.intern("tick_type"),
                si.intern("scheduled"),
                si.intern("repeat"),
                si.intern("random"),
                si.intern("pos_x"),
                si.intern("neg_x"),
                si.intern("pos_y"),
                si.intern("neg_y"),
                si.intern("pos_z"),
                si.intern("neg_z"),
            };
        }();
        return syms;
    }
};

static uint32_t faceToSymbol(Face face) {
    const auto& s = EventSymbols::instance();
    switch (face) {
        case Face::PosX: return s.pos_x;
        case Face::NegX: return s.neg_x;
        case Face::PosY: return s.pos_y;
        case Face::NegY: return s.neg_y;
        case Face::PosZ: return s.pos_z;
        case Face::NegZ: return s.neg_z;
    }
    return s.pos_y;
}

static uint32_t tickTypeToSymbol(TickType type) {
    const auto& s = EventSymbols::instance();
    if (type & TickType::Scheduled) return s.scheduled;
    if (type & TickType::Repeat) return s.repeat;
    if (type & TickType::Random) return s.random;
    return s.scheduled;
}

// ============================================================================

ScriptBlockHandler::ScriptBlockHandler(
    const std::string& name,
    finescript::ScriptEngine& engine,
    std::unique_ptr<finescript::ExecutionContext> ctx)
    : name_(name)
    , engine_(engine)
    , ctx_(std::move(ctx))
{
    // Cache handler closures from the execution context
    for (const auto& handler : ctx_->eventHandlers()) {
        handlers_[handler.eventSymbol] = handler.handlerFunction;
    }
}

std::string_view ScriptBlockHandler::name() const {
    return name_;
}

void ScriptBlockHandler::onPlace(BlockContext& ctx) {
    invokeHandler(EventSymbols::instance().place, ctx);
}

void ScriptBlockHandler::onBreak(BlockContext& ctx) {
    invokeHandler(EventSymbols::instance().break_, ctx);
}

void ScriptBlockHandler::onTick(BlockContext& ctx, TickType type) {
    const auto& s = EventSymbols::instance();
    ctx_->set("tick_type", finescript::Value::symbol(tickTypeToSymbol(type)));
    invokeHandler(s.tick, ctx);
}

void ScriptBlockHandler::onNeighborChanged(BlockContext& ctx, Face changedFace) {
    const auto& s = EventSymbols::instance();
    ctx_->set("face", finescript::Value::symbol(faceToSymbol(changedFace)));
    invokeHandler(s.neighbor_changed, ctx);
}

void ScriptBlockHandler::onBlockUpdate(BlockContext& ctx) {
    invokeHandler(EventSymbols::instance().block_update, ctx);
}

bool ScriptBlockHandler::onUse(BlockContext& ctx, Face face) {
    const auto& s = EventSymbols::instance();
    ctx_->set("face", finescript::Value::symbol(faceToSymbol(face)));
    auto result = invokeHandler(s.use, ctx);
    return result.truthy();
}

bool ScriptBlockHandler::onHit(BlockContext& ctx, Face face) {
    const auto& s = EventSymbols::instance();
    ctx_->set("face", finescript::Value::symbol(faceToSymbol(face)));
    auto result = invokeHandler(s.hit, ctx);
    return result.truthy();
}

void ScriptBlockHandler::onRepaint(BlockContext& ctx) {
    invokeHandler(EventSymbols::instance().repaint, ctx);
}

finescript::Value ScriptBlockHandler::invokeHandler(
    uint32_t eventSymbol, BlockContext& blockCtx)
{
    auto it = handlers_.find(eventSymbol);
    if (it == handlers_.end()) {
        return finescript::Value::nil();
    }

    // Set up block context proxy
    auto ctxProxy = std::make_shared<BlockContextProxy>(blockCtx);
    ctx_->set("ctx", finescript::Value::proxyMap(ctxProxy));

    // Set up per-block data proxy
    DataContainer& dataContainer = blockCtx.getOrCreateData();
    auto dataProxy = std::make_shared<DataContainerProxy>(dataContainer);
    ctx_->set("data", finescript::Value::proxyMap(dataProxy));

    // Call the handler
    finescript::Value result;
    try {
        result = engine_.callFunction(it->second, {}, *ctx_);
    } catch (const std::exception& e) {
        std::cerr << "[ScriptBlockHandler] Error in '" << name_
                  << "' handler: " << e.what() << "\n";
    }

    // Clear transient variables
    ctx_->set("ctx", finescript::Value::nil());
    ctx_->set("data", finescript::Value::nil());

    return result;
}

}  // namespace finevox::script
