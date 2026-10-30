#include "finevox/script/script_game_subsystem.hpp"
#include "finevox/script/game_script_engine.hpp"

#include <finescript/cbor.h>
#include <finescript/script_engine.h>

#include <iostream>

namespace finevox::script {

ScriptGameSubsystem::ScriptGameSubsystem(GameScriptEngine& scripts)
    : scripts_(scripts)
    , ctx_(std::make_unique<finescript::ExecutionContext>(scripts.engine()))
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

void ScriptGameSubsystem::deliverCommand(const finescript::Value& command) {
    ctx_->set("cmd", command);
    fire("command");
}

void ScriptGameSubsystem::publish(const finescript::Value& message) {
    if (!sink_) return;
    sink_(finescript::cborEncode(message, scripts_.engine().interner()));
}

void ScriptGameSubsystem::reportError(const std::string& message) {
    ++errorCount_;
    if (seenErrors_.insert(message).second) {
        std::cerr << "[game script] " << message << "\n";
        errors_.push_back(message);
    }
}

}  // namespace finevox::script
