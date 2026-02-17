#include "finevox/script/script_entity_handler.hpp"
#include "finevox/script/entity_context_proxy.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/string_interner.hpp"
#include <iostream>

namespace finevox::script {

// Pre-interned entity event symbol IDs
struct EntityEventSymbols {
    uint32_t spawn;
    uint32_t tick;
    uint32_t damage;
    uint32_t death;
    uint32_t interact;
    uint32_t strike;

    // Extra context variable names
    uint32_t dt;
    uint32_t amount;
    uint32_t source;
    uint32_t killer;
    uint32_t player;
    uint32_t attacker;

    static const EntityEventSymbols& instance() {
        static EntityEventSymbols syms = [] {
            auto& si = StringInterner::global();
            return EntityEventSymbols{
                si.intern("spawn"),
                si.intern("tick"),
                si.intern("damage"),
                si.intern("death"),
                si.intern("interact"),
                si.intern("strike"),
                si.intern("dt"),
                si.intern("amount"),
                si.intern("source"),
                si.intern("killer"),
                si.intern("player"),
                si.intern("attacker"),
            };
        }();
        return syms;
    }
};

ScriptEntityHandler::ScriptEntityHandler(
    const std::string& name,
    finescript::ScriptEngine& engine,
    std::unique_ptr<finescript::ExecutionContext> ctx)
    : name_(name)
    , engine_(engine)
    , ctx_(std::move(ctx))
{
    for (const auto& handler : ctx_->eventHandlers()) {
        handlers_[handler.eventSymbol] = handler.handlerFunction;
    }
}

void ScriptEntityHandler::onSpawn(MobEntity& mob) {
    invokeHandler(EntityEventSymbols::instance().spawn, mob);
}

void ScriptEntityHandler::onTick(MobEntity& mob, float dt) {
    ctx_->set("dt", finescript::Value::number(dt));
    invokeHandler(EntityEventSymbols::instance().tick, mob);
}

void ScriptEntityHandler::onDamage(MobEntity& mob, float amount, uint64_t source) {
    const auto& s = EntityEventSymbols::instance();
    ctx_->set("amount", finescript::Value::number(amount));
    ctx_->set("source", finescript::Value::integer(static_cast<int64_t>(source)));
    invokeHandler(s.damage, mob);
}

void ScriptEntityHandler::onDeath(MobEntity& mob, uint64_t killer) {
    ctx_->set("killer", finescript::Value::integer(static_cast<int64_t>(killer)));
    invokeHandler(EntityEventSymbols::instance().death, mob);
}

void ScriptEntityHandler::onInteract(MobEntity& mob, uint64_t player) {
    ctx_->set("player", finescript::Value::integer(static_cast<int64_t>(player)));
    invokeHandler(EntityEventSymbols::instance().interact, mob);
}

void ScriptEntityHandler::onStrike(MobEntity& mob, uint64_t attacker) {
    ctx_->set("attacker", finescript::Value::integer(static_cast<int64_t>(attacker)));
    invokeHandler(EntityEventSymbols::instance().strike, mob);
}

finescript::Value ScriptEntityHandler::invokeHandler(
    uint32_t eventSymbol, MobEntity& mob)
{
    auto it = handlers_.find(eventSymbol);
    if (it == handlers_.end()) {
        return finescript::Value::nil();
    }

    // Set up entity context: both proxy map variable and userData pointer
    auto entityProxy = makeEntityProxy(mob);
    ctx_->set("entity", finescript::Value::proxyMap(entityProxy));

    // Set entityCtx in userData so mob.* native functions can access this mob
    auto* ud = static_cast<ScriptUserData*>(ctx_->userData());
    MobEntity* prevEntity = nullptr;
    if (ud) {
        prevEntity = ud->entityCtx;
        ud->entityCtx = &mob;
    }

    finescript::Value result;
    try {
        result = engine_.callFunction(it->second, {}, *ctx_);
    } catch (const std::exception& e) {
        std::cerr << "[ScriptEntityHandler] Error in '" << name_
                  << "' handler: " << e.what() << "\n";
    }

    // Restore previous entity context
    if (ud) {
        ud->entityCtx = prevEntity;
    }

    // Clear transient variables
    ctx_->set("entity", finescript::Value::nil());
    ctx_->set("dt", finescript::Value::nil());
    ctx_->set("amount", finescript::Value::nil());
    ctx_->set("source", finescript::Value::nil());
    ctx_->set("killer", finescript::Value::nil());
    ctx_->set("player", finescript::Value::nil());
    ctx_->set("attacker", finescript::Value::nil());

    return result;
}

}  // namespace finevox::script
