/**
 * @file finevox_run.cpp
 * @brief Run a game from its content directory
 *
 *   finevox_run [--game-root DIR] [--user-dir DIR] [--seed S] [--radius R] [--script FILE]
 *               [--new | --continue | --join ADDR] [--name N] [--password P]
 *               [--screenshot PATH --screenshot-frame N] [--exit-after N]
 *               [--mcp] [--mcp-port N] [--mcp-player] [--trace FILE]
 *   finevox_run [--game-root DIR] [--user-dir DIR] --server [--config FILE] [--port N] [--new]
 *
 * With none of --new, --continue or --join it opens on the game's menu, or goes straight into a
 * game if it hasn't one.
 *
 * With no --game-root this is an installed copy: the game is the one it shipped with, and saves,
 * screenshots and the like go in the player's own folder. With one, they go in the game root,
 * which is what suits a source tree. --user-dir chooses somewhere else for either.
 *
 * --server has no window and no GPU: the game runs until killed, for clients to join.
 */

#include "finevox/client/game_client.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/game/game_host.hpp"
#include "finevox/game/game_manifest.hpp"
#include "finevox/game/game_runtime.hpp"
#include "finevox/game/mcp_server.hpp"

#include <finevk/core/logging.hpp>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace finevox;

namespace {

std::atomic<client::GameClient*> activeClient{nullptr};

void handleStopSignal(int) {
    if (auto* c = activeClient.load(std::memory_order_acquire)) {
        c->requestStop();
    }
}

#ifdef _WIN32
/// A windowed build (FINEVOX_WINDOWS_GUI) has no console. Started from one, it prints there as
/// a console program would; started from a shortcut, there's nowhere to print and nothing to do.
void attachParentConsole() {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
    FILE* reopened = nullptr;
    freopen_s(&reopened, "CONOUT$", "w", stdout);
    freopen_s(&reopened, "CONOUT$", "w", stderr);
    std::cout.clear();
    std::cerr.clear();
}

#endif

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

    const game::LoadProgress* loadProgress() const override { return host_.loadProgress(); }

private:
    game::GameHost host_;
};

std::string defaultPlayerName() {
#ifdef _WIN32
    const char* user = std::getenv("USERNAME");
#else
    const char* user = std::getenv("USER");
#endif
    return user && *user ? user : "Player";
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    attachParentConsole();
#endif
    game::RuntimeOptions runtimeOptions;
    client::ClientOptions clientOptions;
    clientOptions.playerName = defaultPlayerName();
    bool verbose = false;
    bool server = false;
    bool newGame = false;
    std::filesystem::path serverConfig;
    std::optional<int> serverPort;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("-psn_", 0) == 0) continue;  // macOS can add this when opening an app
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << arg << "\n";
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--game-root") {
            runtimeOptions.gameRoot = value();
        } else if (arg == "--user-dir") {
            runtimeOptions.userRoot = value();
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
        } else if (arg == "--no-vsync") {
            clientOptions.vsync = false;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--new") {
            newGame = true;
            clientOptions.start = client::ClientStart::NewGame;
        } else if (arg == "--no-save") {
            runtimeOptions.noSaves = true;
        } else if (arg == "--trace") {
            runtimeOptions.tracePath = value();
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

    auto& locator = ResourceLocator::instance();
    locator.setEngineRoot(ResourceLocator::findEngineRoot(ResourceLocator::executableDir()));

    bool installed = runtimeOptions.gameRoot.empty();
    if (installed) runtimeOptions.gameRoot = locator.resolve("engine/game");
    auto manifest = game::GameManifest::load(runtimeOptions.gameRoot);
    if (installed && !manifest) {
        std::cerr << "usage: finevox_run --game-root DIR [options]\n";
        return 2;
    }

    if (runtimeOptions.userRoot.empty()) {
        runtimeOptions.userRoot = installed ? ResourceLocator::userRootFor(manifest->contentNamespace)
                                            : runtimeOptions.gameRoot;
    }
    std::error_code ignored;
    std::filesystem::create_directories(runtimeOptions.userRoot, ignored);
    locator.setUserRoot(runtimeOptions.userRoot);

    // Every mesh upload logs at Debug, which buries anything worth reading
    finevk::Logger::global().setMinLevel(verbose ? finevk::LogLevel::Debug : finevk::LogLevel::Info);

    if (server) {
        runtimeOptions.newGame = newGame;
        return game::serve(std::move(runtimeOptions), serverConfig, serverPort);
    }

    if (clientOptions.shaderDir.empty()) clientOptions.shaderDir = locator.resolve("engine/shaders");
    if (clientOptions.shaderDir.empty()) {
        std::cerr << "error: can't find the shaders: use --shader-dir\n";
        return 1;
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
    clientOptions.userRoot = runtimeOptions.userRoot;
    clientOptions.hostFactory = [runtimeOptions](const client::HostRequest& request, std::string& error)
        -> std::unique_ptr<client::LocalHost> {
        game::HostConfig config;
        config.runtime = runtimeOptions;
        config.runtime.newGame = request.newGame;
        if (!request.saveSlot.empty() && !config.runtime.noSaves) {
            config.runtime.savePath = config.runtime.saveRoot() / request.saveSlot;
        } else if (!request.autoLaunch && !config.runtime.noSaves) {
            // A hosted game has a slot of its own: it's no reason to lose the one you were playing
            config.runtime.savePath = config.runtime.saveRoot() / "hosted";
        }
        if (!request.title.empty()) {
            config.runtime.worldTitle = request.title;
        }
        if (request.seed) {
            config.runtime.seedOverride = *request.seed;
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

    try {
        if (mcp) {
            std::string error;
            if (mcp->start(error)) {
                std::cerr << "[mcp] listening on http://127.0.0.1:" << mcp->port() << "/mcp\n";
            } else {
                std::cerr << "[mcp] " << error << ": running without it\n";
            }
        }
        // Leaving a game for the menu, or for a new game, happens inside: the window stays
        client::GameClient gameClient(clientOptions);
        activeClient.store(&gameClient, std::memory_order_release);
        std::signal(SIGINT, handleStopSignal);
        std::signal(SIGTERM, handleStopSignal);
        int code = gameClient.run();
        activeClient.store(nullptr, std::memory_order_release);
        if (mcp) mcp->stop();
        return code;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
