/**
 * @file finevox_bot.cpp
 * @brief A client with no window: for tests over real sockets
 *
 *   finevox_bot --game NAMESPACE [--game-root DIR] --join ADDR --name N --script FILE [--password P]
 *               [--cache DIR] [--load-delay SECONDS] [--timeout SECONDS]
 *
 * ADDR `lan` finds a server the way a player's browser does, and joins whatever it finds first.
 *
 * Joins a game as any client would and runs a script, which hears what a UI script hears and asserts
 * on what this client knows. That includes what it must not know: it holds only what was sent.
 * It links the protocol and nothing of the game, so if this builds, a client needs none of it.
 *
 * Scripts get `on :net`, `on :lobby`, `on :chat`, `on :state` and `on :tick`, and exit with `bot_exit`.
 */

#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/net/client_session.hpp"
#include "finevox/script/finevox_interner.hpp"

#include <finenet/discovery.hpp>
#include <finescript/cbor.h>
#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace finevox;
using finescript::ExecutionContext;
using finescript::Value;
using Args = std::vector<Value>;
using Clock = std::chrono::steady_clock;

namespace {

struct Bot {
    std::unique_ptr<net::ClientSession> session;
    finescript::ScriptEngine engine;
    script::FineVoxInterner interner;
    std::unique_ptr<ExecutionContext> context;
    int exitCode = -1;

    void fire(std::string_view event) {
        uint32_t symbol = engine.intern(event);
        auto& handlers = context->eventHandlers();
        for (size_t i = 0; i < handlers.size(); ++i) {
            if (handlers[i].eventSymbol != symbol) continue;
            try {
                engine.callFunction(handlers[i].handlerFunction, {}, *context);
            } catch (const std::exception& e) {
                std::cerr << "[bot script] " << e.what() << "\n";
                exitCode = 1;
            }
        }
    }

    void registerNatives() {
        engine.registerFunction("bot_exit", [this](ExecutionContext&, const Args& args) -> Value {
            exitCode = args.empty() ? 0 : static_cast<int>(args[0].asNumber());
            return Value::nil();
        });
        engine.registerFunction("log", [](ExecutionContext&, const Args& args) -> Value {
            std::string line;
            for (size_t i = 0; i < args.size(); ++i) {
                if (i) line += ' ';
                line += args[i].isSymbol() ? ":" + std::string(StringInterner::global().lookup(args[i].asSymbol()))
                                           : args[i].toString();
            }
            std::cout << line << std::endl;
            return Value::nil();
        });
        engine.registerFunction("me", [this](ExecutionContext&, const Args&) -> Value {
            return Value::integer(session->playerId());
        });
        engine.registerFunction("my_faction", [this](ExecutionContext&, const Args&) -> Value {
            auto bound = session->binding();
            return Value::integer(bound ? bound->faction : 0);
        });
        engine.registerFunction("now_ms", [](ExecutionContext&, const Args&) -> Value {
            auto now = std::chrono::system_clock::now().time_since_epoch();
            return Value::integer(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
        });
        engine.registerFunction("content_root", [this](ExecutionContext&, const Args&) -> Value {
            return Value::string(session->contentRoot().string());
        });
        engine.registerFunction("phase", [this](ExecutionContext&, const Args&) -> Value {
            return net::symbol(net::phaseName(session->phase()));
        });
        engine.registerFunction("lobby_info", [this](ExecutionContext&, const Args&) -> Value {
            return finescript::cborDecode(session->lobbyDeclaration(), interner);
        });
        engine.registerFunction("lobby_set", [this](ExecutionContext&, const Args& args) -> Value {
            if (args.size() >= 2) session->setSetting(args[0].toString(), args[1]);
            return Value::nil();
        });
        engine.registerFunction("lobby_ready", [this](ExecutionContext&, const Args& args) -> Value {
            session->setReady(args.empty() || args[0].truthy());
            return Value::nil();
        });
        engine.registerFunction("lobby_launch", [this](ExecutionContext&, const Args& args) -> Value {
            session->launch(args.empty() || args[0].truthy());
            return Value::nil();
        });
        engine.registerFunction("chat_send", [this](ExecutionContext&, const Args& args) -> Value {
            bool team = args.size() > 1 && args.back().isMap() && net::boolField(args.back(), "team");
            if (!args.empty()) session->chat(args[0].toString(), team);
            return Value::nil();
        });
        engine.registerFunction("send_command", [this](ExecutionContext&, const Args& args) -> Value {
            if (!args.empty()) session->sendCommand(args[0]);
            return Value::nil();
        });

        // block_at x y z -> the block's name, or nil if this client was never told
        engine.registerFunction("block_at", [this](ExecutionContext&, const Args& args) -> Value {
            if (args.size() < 3) return Value::nil();
            BlockCoord pos(static_cast<int32_t>(args[0].asNumber()), static_cast<int32_t>(args[1].asNumber()),
                           static_cast<int32_t>(args[2].asNumber()));
            const World& world = session->world().world();
            if (!world.getSubChunk(ChunkPos(pos.x >> 4, pos.y >> 4, pos.z >> 4))) return Value::nil();
            return Value::string(std::string(world.getBlock(pos).name()));
        });

        // columns_known -> how many columns this client holds anything of
        engine.registerFunction("columns_known", [this](ExecutionContext&, const Args&) -> Value {
            int64_t count = 0;
            const World& world = session->world().world();
            world.forEachColumn([&count](ColumnPos, const ChunkColumn&) { ++count; });
            return Value::integer(count);
        });

        engine.registerFunction("subchunks_received", [this](ExecutionContext&, const Args&) -> Value {
            return Value::integer(static_cast<int64_t>(session->world().subchunksReceived()));
        });

        // entities -> [{=id =faction =x =y =z}...]
        engine.registerFunction("entities", [this](ExecutionContext&, const Args&) -> Value {
            std::vector<Value> out;
            for (const EntityState& state : session->entities()) {
                Value entry = Value::map();
                net::setField(entry, "id", Value::integer(static_cast<int64_t>(state.id)));
                net::setField(entry, "faction", Value::integer(state.faction));
                net::setField(entry, "x", Value::number(state.position.x));
                net::setField(entry, "y", Value::number(state.position.y));
                net::setField(entry, "z", Value::number(state.position.z));
                out.push_back(std::move(entry));
            }
            return Value::array(std::move(out));
        });

        // vision_counts -> {=unknown =explored =visible}
        engine.registerFunction("vision_counts", [this](ExecutionContext&, const Args&) -> Value {
            int64_t counts[3] = {0, 0, 0};
            for (uint8_t cell : session->vision()) {
                if (cell < 3) ++counts[cell];
            }
            Value out = Value::map();
            net::setField(out, "unknown", Value::integer(counts[0]));
            net::setField(out, "explored", Value::integer(counts[1]));
            net::setField(out, "visible", Value::integer(counts[2]));
            return out;
        });
    }
};

}  // namespace

int main(int argc, char* argv[]) {
    net::ClientSession::Options options;
    std::string address, scriptPath;
    double loadDelay = 0.0, timeout = 60.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--game-root") {
            options.gameRoot = value();
        } else if (arg == "--game") {
            options.game = value();
        } else if (arg == "--join") {
            address = value();
        } else if (arg == "--name") {
            options.name = value();
        } else if (arg == "--password") {
            options.password = value();
        } else if (arg == "--cache") {
            options.cacheDir = value();
        } else if (arg == "--script") {
            scriptPath = value();
        } else if (arg == "--load-delay") {
            loadDelay = std::stod(value());
        } else if (arg == "--timeout") {
            timeout = std::stod(value());
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }
    std::string game = options.game;
    if (address.empty() || scriptPath.empty() || game.empty()) {
        std::cerr << "usage: finevox_bot --game NAMESPACE [--game-root DIR] --join ADDR --name N --script FILE\n";
        return 2;
    }

    Bot bot;
    bot.engine.setInterner(&bot.interner);
    bot.session = std::make_unique<net::ClientSession>(std::move(options));
    bot.context = std::make_unique<ExecutionContext>(bot.engine);
    bot.registerNatives();

    auto* script = bot.engine.loadScript(scriptPath);
    auto result = bot.engine.execute(*script, *bot.context);
    if (!result.success) {
        std::cerr << scriptPath << ":" << result.errorLine << ": " << result.error << "\n";
        return 1;
    }

    if (address == "lan") {
        finenet::DiscoveryBrowser::Config config;
        config.app = game;
        finenet::DiscoveryBrowser browser(config);
        auto deadline = Clock::now() + std::chrono::seconds(10);
        std::vector<finenet::DiscoveredServer> found;
        while (found.empty() && Clock::now() < deadline) {
            // Anyone can see a game that's running, but only some can be joined
            for (auto& server : browser.servers()) {
                if (net::boolField(net::decodeBody(server.listing), "joinable")) found.push_back(std::move(server));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (found.empty()) {
            std::cerr << "[bot] no server found, blocked=" << browser.blocked() << "\n";
            return 4;
        }
        std::vector<std::string> addresses;
        for (const std::string& host : found[0].addresses) {
            addresses.push_back(host.find(':') == std::string::npos
                                    ? host + ":" + std::to_string(found[0].port)
                                    : "[" + host + "]:" + std::to_string(found[0].port));
        }
        std::cout << "[bot] found " << addresses[0] << std::endl;
        bot.session->connect(std::move(addresses));
    } else {
        bot.session->connect({address});
    }

    auto started = Clock::now();
    auto limit = std::chrono::duration<double>(timeout);
    std::optional<Clock::time_point> loadAt;
    while (bot.exitCode < 0) {
        for (auto& event : bot.session->takeEvents()) {
            using Kind = net::SessionEvent::Kind;
            switch (event.kind) {
                case Kind::Phase:
                case Kind::Failed: {
                    Value message = Value::map();
                    net::setField(message, "phase", net::symbol(event.kind == Kind::Failed ? "failed" : event.text));
                    if (event.kind == Kind::Failed) net::setField(message, "failure", Value::string(event.text));
                    bot.context->set("msg", std::move(message));
                    bot.fire("net");
                    break;
                }
                case Kind::Lobby:
                case Kind::Chat:
                case Kind::Ui:
                    bot.context->set("msg", finescript::cborDecode(event.body, bot.interner));
                    bot.fire(event.kind == Kind::Lobby ? "lobby" : event.kind == Kind::Chat ? "chat" : "state");
                    break;
                case Kind::WorldReady:
                    loadAt = Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                std::chrono::duration<double>(loadDelay));
                    break;
            }
        }
        if (loadAt && Clock::now() >= *loadAt) {
            loadAt.reset();
            bot.session->sendLoaded();
        }
        bot.fire("tick");
        if (Clock::now() - started > limit) {
            std::cerr << "[bot] timed out in phase " << net::phaseName(bot.session->phase()) << "\n";
            bot.exitCode = 3;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    bot.session->leave();
    return bot.exitCode;
}
