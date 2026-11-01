/**
 * @file finevox_run.cpp
 * @brief Run a game from its content directory
 *
 *   finevox_run --game-root DIR [--seed S] [--radius R] [--script FILE]
 *               [--new | --continue | --join ADDR] [--name N] [--password P]
 *               [--screenshot PATH --screenshot-frame N] [--exit-after N]
 *               [--mcp] [--mcp-port N] [--mcp-player]
 *   finevox_run --game-root DIR --server [--config FILE] [--port N] [--new]
 *
 * With none of --new, --continue or --join it opens on the game's menu, or goes straight into a
 * game if it hasn't one.
 *
 * --server has no window and no GPU: the game runs until killed, for clients to join.
 */

#include "finevox/client/game_client.hpp"
#include "finevox/game/game_host.hpp"
#include "finevox/game/game_runtime.hpp"
#include "finevox/game/mcp_server.hpp"

#include <finevk/core/logging.hpp>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <unistd.h>
#include <vector>

using namespace finevox;

namespace {

/// What a client sees of a game in this process
class HostHandle : public client::LocalHost {
public:
    explicit HostHandle(game::HostConfig config) : host_(std::move(config)) {}

    bool start(std::string& error) { return host_.start(error); }

    std::unique_ptr<finenet::Connection> connect() override { return host_.connectLocal(); }
    bool openToNetwork(uint16_t port, std::string& error) override { return host_.openToNetwork(port, error); }
    uint16_t networkPort() const override { return host_.networkPort(); }
    void stop() override { host_.stop(); }

    ContentPackLoader* content() override {
        game::GameRuntime* runtime = host_.runtime();
        return runtime ? &runtime->content() : nullptr;
    }

private:
    game::GameHost host_;
};

std::string defaultPlayerName() {
    const char* user = std::getenv("USER");
    return user && *user ? user : "Player";
}

}  // namespace

int main(int argc, char* argv[]) {
    game::RuntimeOptions runtimeOptions;
    client::ClientOptions clientOptions;
    clientOptions.shaderDir = FINEVOX_SHADER_DIR;
    clientOptions.playerName = defaultPlayerName();
    bool verbose = false;
    bool server = false;
    bool newGame = false;
    std::filesystem::path serverConfig;
    std::optional<int> serverPort;

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
            runtimeOptions.gameRoot = value();
        } else if (arg == "--server") {
            server = true;
        } else if (arg == "--config") {
            serverConfig = value();
        } else if (arg == "--port") {
            serverPort = std::stoi(value());
        } else if (arg == "--join") {
            clientOptions.start = client::ClientStart::Join;
            clientOptions.joinAddress = value();
        } else if (arg == "--continue") {
            clientOptions.start = client::ClientStart::Continue;
        } else if (arg == "--name") {
            clientOptions.playerName = value();
        } else if (arg == "--password") {
            clientOptions.password = value();
        } else if (arg == "--seed") {
            runtimeOptions.seedOverride = std::stoull(value());
        } else if (arg == "--radius") {
            runtimeOptions.radiusOverride = std::stoi(value());
        } else if (arg == "--script") {
            runtimeOptions.extraScripts.emplace_back(value());
        } else if (arg == "--shader-dir") {
            clientOptions.shaderDir = value();
        } else if (arg == "--size") {
            clientOptions.width = static_cast<uint32_t>(std::stoul(value()));
            clientOptions.height = static_cast<uint32_t>(std::stoul(value()));
        } else if (arg == "--stats") {
            clientOptions.printStats = true;
        } else if (arg == "--validation") {
            clientOptions.validation = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--new") {
            newGame = true;
            clientOptions.start = client::ClientStart::NewGame;
        } else if (arg == "--no-save") {
            runtimeOptions.noSaves = true;
        } else if (arg == "--mcp") {
            if (!runtimeOptions.mcpPort) runtimeOptions.mcpPort = game::MCP_DEFAULT_PORT;
        } else if (arg == "--mcp-port") {
            runtimeOptions.mcpPort = std::stoi(value());
        } else if (arg == "--mcp-player") {
            if (!runtimeOptions.mcpPort) runtimeOptions.mcpPort = game::MCP_DEFAULT_PORT;
            runtimeOptions.mcpPlayerOnly = true;
        } else if (arg == "--screenshot") {
            clientOptions.screenshotPath = value();
        } else if (arg == "--screenshot-frame") {
            clientOptions.screenshotFrame = std::stoi(value());
        } else if (arg == "--screenshot-burst") {
            clientOptions.burstCount = std::stoi(value());
        } else if (arg == "--screenshot-interval") {
            clientOptions.burstInterval = std::stoi(value());
        } else if (arg == "--exit-after") {
            clientOptions.exitAfterFrames = std::stoi(value());
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }

    // A run that ends itself is automation, and leaves the player's game alone
    if (clientOptions.exitAfterFrames > 0) runtimeOptions.noSaves = true;

    if (runtimeOptions.gameRoot.empty()) {
        std::cerr << "usage: finevox_run --game-root DIR [options]\n";
        return 2;
    }

    // Every mesh upload logs at Debug, which buries anything worth reading
    finevk::Logger::global().setMinLevel(verbose ? finevk::LogLevel::Debug : finevk::LogLevel::Info);

    if (server) {
        runtimeOptions.newGame = newGame;
        return game::serve(std::move(runtimeOptions), serverConfig, serverPort);
    }

    // This run's MCP server exists before any game does, so that a menu can be driven through it
    std::unique_ptr<game::McpServer> mcp;
    if (runtimeOptions.mcpPort) {
        mcp = std::make_unique<game::McpServer>(*runtimeOptions.mcpPort, runtimeOptions.mcpPlayerOnly);
        mcp->captureOutput();
        runtimeOptions.mcpServer = mcp.get();
        clientOptions.mcp = mcp.get();
    }

    clientOptions.gameRoot = runtimeOptions.gameRoot;
    clientOptions.hostFactory = [runtimeOptions](const client::HostRequest& request, std::string& error)
        -> std::unique_ptr<client::LocalHost> {
        game::HostConfig config;
        config.runtime = runtimeOptions;
        config.runtime.newGame = request.newGame;
        // A hosted game has a slot of its own: it's no reason to lose the one you were playing
        if (!request.autoLaunch && !config.runtime.noSaves) {
            config.runtime.savePath = config.runtime.gameRoot / "saves" / "hosted";
        }
        config.autoLaunch = request.autoLaunch;
        config.listen = request.listen;
        config.server.title = request.title;
        config.server.password = request.password;
        if (request.port != 0) config.server.port = request.port;

        auto handle = std::make_unique<HostHandle>(std::move(config));
        if (!handle->start(error)) return nullptr;
        return handle;
    };

    bool startOver = false;
    bool toMenu = false;
    try {
        if (mcp) {
            std::string error;
            if (mcp->start(error)) {
                std::cerr << "[mcp] listening on http://127.0.0.1:" << mcp->port() << "/mcp\n";
            } else {
                std::cerr << "[mcp] " << error << ": running without it\n";
            }
        }
        client::GameClient gameClient(clientOptions);
        int code = gameClient.run();
        startOver = gameClient.newGameRequested();
        toMenu = gameClient.restartRequested();
        if (mcp) mcp->stop();
        if (!startOver && !toMenu) return code;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    // Only a fresh process is sure of leaving nothing of the old game behind. The arguments that
    // chose how this one started don't apply to the next.
    std::vector<std::string> kept;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--new" || arg == "--continue") continue;
        if (arg == "--join" || arg == "--password") {
            ++i;
            continue;
        }
        kept.push_back(std::move(arg));
    }
    if (startOver) kept.emplace_back("--new");

    std::vector<char*> arguments;
    arguments.push_back(argv[0]);
    for (std::string& arg : kept) arguments.push_back(arg.data());
    arguments.push_back(nullptr);
    execv(argv[0], arguments.data());
    std::cerr << "error: couldn't restart\n";
    return 1;
}
