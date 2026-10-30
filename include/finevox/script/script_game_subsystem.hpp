#pragma once

/**
 * @file script_game_subsystem.hpp
 * @brief Game-wide script, ticked on the game thread
 *
 * Block and entity handlers run per-object. Rules that span the whole game (job
 * assignment, match flow, rival strategy) need one script that outlives any
 * object. It registers handlers the same way block scripts do:
 *
 *     on :start do ... end
 *     on :tick do ... end            # `dt` in scope
 *     on :command do ... end         # `cmd` in scope, from GameActions::sendGameCommand
 *
 * A broken handler reports once and the game keeps running. Tests that want
 * script errors to be fatal check errorCount().
 */

#include "finevox/core/game_subsystem.hpp"

#include <finescript/execution_context.h>
#include <finescript/value.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace finevox::script {

class GameScriptEngine;

class ScriptGameSubsystem : public GameSubsystem {
public:
    using MessageSink = std::function<void(std::vector<uint8_t>)>;

    explicit ScriptGameSubsystem(GameScriptEngine& scripts);
    ~ScriptGameSubsystem() override;

    [[nodiscard]] std::string_view name() const override { return "ScriptGame"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PostTick; }
    /// After entities, so scripts see this tick's positions and deaths
    [[nodiscard]] int32_t priority() const override { return 100; }
    void tick(float dt) override;

    /// Run a script file in the game context. Its `on` handlers stay registered.
    bool loadScript(const std::string& path);

    /// Fire an event at every handler registered for it
    void fire(std::string_view event);

    /// Deliver a client command to `on :command` handlers
    void deliverCommand(const finescript::Value& command);

    /// Where ui_publish goes. Payloads are CBOR, safe to hand across threads.
    void setMessageSink(MessageSink sink) { sink_ = std::move(sink); }
    void publish(const finescript::Value& message);

    /// Total ticks since this subsystem started
    [[nodiscard]] uint64_t tickCount() const { return tickCount_; }

    [[nodiscard]] finescript::ExecutionContext& context() { return *ctx_; }

    /// Handler failures so far (each distinct message is logged once)
    [[nodiscard]] size_t errorCount() const { return errorCount_; }
    [[nodiscard]] const std::vector<std::string>& errors() const { return errors_; }

private:
    void reportError(const std::string& message);

    GameScriptEngine& scripts_;
    std::unique_ptr<finescript::ExecutionContext> ctx_;
    MessageSink sink_;
    uint64_t tickCount_ = 0;
    uint32_t tickEvent_ = 0;
    size_t errorCount_ = 0;
    std::vector<std::string> errors_;
    std::unordered_set<std::string> seenErrors_;
};

}  // namespace finevox::script
