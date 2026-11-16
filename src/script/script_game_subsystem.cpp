#include "finevox/script/script_game_subsystem.hpp"
#include "finevox/script/game_script_engine.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <algorithm>
#include <iostream>

namespace finevox::script {

ScriptGameSubsystem::ScriptGameSubsystem(GameScriptEngine& scripts)
    : scripts_(scripts)
    , ctx_(std::make_unique<finescript::ExecutionContext>(scripts.engine(), scripts.gameScope()))
    , tickEvent_(scripts.engine().intern("tick"))
{
    ctx_->setUserData(&scripts.userData());
    scripts.userData().gameScript = this;
}

ScriptGameSubsystem::~ScriptGameSubsystem() {
    scripts_.userData().gameScript = nullptr;
}

bool ScriptGameSubsystem::loadScript(const std::string& path) {
    auto* script = scripts_.cache().load(path);
    if (!script) {
        reportError("failed to load script: " + path);
        return false;
    }
    auto result = scripts_.engine().execute(*script, *ctx_);
    if (!result.success) {
        reportError(path + ":" + std::to_string(result.errorLine) + ": " + result.error);
        return false;
    }
    return true;
}

void ScriptGameSubsystem::tick(float dt) {
    ++tickCount_;
    ctx_->set("dt", finescript::Value::number(dt));

    auto& handlers = ctx_->eventHandlers();
    for (size_t i = 0; i < handlers.size(); ++i) {
        if (handlers[i].eventSymbol != tickEvent_) continue;
        try {
            scripts_.engine().callFunction(handlers[i].handlerFunction, {}, *ctx_);
        } catch (const std::exception& e) {
            reportError(e.what());
        }
    }
}

void ScriptGameSubsystem::tickPaused(float dt) {
    ctx_->set("dt", finescript::Value::number(dt));
    fire("paused_tick");
}

void ScriptGameSubsystem::fire(std::string_view event) {
    uint32_t symbol = scripts_.engine().intern(event);
    auto& handlers = ctx_->eventHandlers();
    for (size_t i = 0; i < handlers.size(); ++i) {
        if (handlers[i].eventSymbol != symbol) continue;
        try {
            scripts_.engine().callFunction(handlers[i].handlerFunction, {}, *ctx_);
        } catch (const std::exception& e) {
            reportError(e.what());
        }
    }
}

bool ScriptGameSubsystem::handles(std::string_view event) const {
    uint32_t symbol = scripts_.engine().intern(event);
    for (const auto& handler : ctx_->eventHandlers()) {
        if (handler.eventSymbol == symbol) return true;
    }
    return false;
}

int ScriptGameSubsystem::deliverPickup(int64_t player, std::string_view item, int count, uint64_t entity) {
    auto& e = scripts_.engine();
    auto pickup = finescript::Value::map();
    auto& m = pickup.asMap();
    m.set(e.intern("player"), finescript::Value::integer(player));
    m.set(e.intern("item"), finescript::Value::symbol(e.intern(item)));
    m.set(e.intern("count"), finescript::Value::integer(count));
    m.set(e.intern("entity"), finescript::Value::integer(static_cast<int64_t>(entity)));
    ctx_->set("pickup", pickup);

    uint32_t symbol = e.intern("pickup");
    int taken = 0;
    auto& handlers = ctx_->eventHandlers();
    for (size_t i = 0; i < handlers.size(); ++i) {
        if (handlers[i].eventSymbol != symbol) continue;
        try {
            finescript::Value result = e.callFunction(handlers[i].handlerFunction, {}, *ctx_);
            if (result.isNumeric()) taken = static_cast<int>(result.asNumber());
        } catch (const std::exception& error) {
            reportError(error.what());
        }
    }
    return std::clamp(taken, 0, count);
}

void ScriptGameSubsystem::deliverSignal(int32_t x, int32_t y, int32_t z, int level) {
    auto& e = scripts_.engine();
    auto signal = finescript::Value::map();
    auto& m = signal.asMap();
    m.set(e.intern("x"), finescript::Value::integer(x));
    m.set(e.intern("y"), finescript::Value::integer(y));
    m.set(e.intern("z"), finescript::Value::integer(z));
    m.set(e.intern("level"), finescript::Value::integer(level));
    ctx_->set("signal", signal);
    fire("signal");
}

void ScriptGameSubsystem::deliverMoved(const finescript::Value& moved) {
    ctx_->set("moved", moved);
    fire("moved");
}

void ScriptGameSubsystem::deliverCommand(const finescript::Value& command, int64_t sender) {
    ctx_->set("cmd", command);
    ctx_->set("sender", finescript::Value::integer(sender));
    fire("command");
}

void ScriptGameSubsystem::publish(const finescript::Value& message, Audience audience) {
    if (!sink_) return;
    sink_(finescript::cborEncode(message, scripts_.engine().interner()), audience);
}

void ScriptGameSubsystem::reportError(const std::string& message) {
    ++errorCount_;
    if (seenErrors_.insert(message).second) {
        std::cerr << "[game script] " << message << "\n";
        errors_.push_back(message);
    }
}

}  // namespace finevox::script
