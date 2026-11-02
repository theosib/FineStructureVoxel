/**
 * @file finevox_run.cpp
 * @brief Run a game from its content directory
 *
 *   finevox_run [--game-root DIR] [--user-dir DIR] [--seed S] [--radius R] [--script FILE]
 *               [--new | --continue | --join ADDR] [--name N] [--password P]
 *               [--screenshot PATH --screenshot-frame N] [--exit-after N]
 *               [--mcp] [--mcp-port N] [--mcp-player]
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

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace finevox;

namespace {

#ifdef _WIN32
std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
    return out;
}

/// One argument as CommandLineToArgvW will read it back
void appendQuoted(std::wstring& line, const std::wstring& arg) {
    if (!line.empty()) line += L' ';
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        line += arg;
        return;
    }
    line += L'"';
    for (size_t i = 0;; ++i) {
        size_t slashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++i;
            ++slashes;
        }
        if (i == arg.size()) {
            line.append(slashes * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            line.append(slashes * 2 + 1, L'\\');
        } else {
            line.append(slashes, L'\\');
        }
        line += arg[i];
    }
    line += L'"';
}

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

/// Windows has no exec: start a new process and leave this one to end
bool restartAs(const std::vector<std::string>& args) {
    wchar_t self[MAX_PATH * 4];
    DWORD length = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
    if (length == 0 || length >= std::size(self)) return false;

    std::wstring line;
    appendQuoted(line, self);
    for (const std::string& arg : args) appendQuoted(line, widen(arg));

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(self, line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
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
        // A hosted game has a slot of its own: it's no reason to lose the one you were playing
        if (!request.autoLaunch && !config.runtime.noSaves) {
            config.runtime.savePath = config.runtime.saveRoot() / "hosted";
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

#ifdef _WIN32
    if (restartAs(kept)) return 0;
#else
    std::vector<char*> arguments;
    arguments.push_back(argv[0]);
    for (std::string& arg : kept) arguments.push_back(arg.data());
    arguments.push_back(nullptr);
    execv(argv[0], arguments.data());
#endif
    std::cerr << "error: couldn't restart\n";
    return 1;
}
