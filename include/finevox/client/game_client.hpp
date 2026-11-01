#pragma once

/**
 * @file game_client.hpp
 * @brief Window, rendering, and UI script host for a game, here or elsewhere
 *
 * Engine-side and game-agnostic. All game UI lives in the game's scripts, which run
 * on the render thread with their own script engine. A client knows only what the
 * protocol tells it, even when the game is in this same process. Scripts reach the
 * game through send_command and hear from it through `on :state`.
 */

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace finenet {
class Connection;
}

namespace finevox {
class ContentPackLoader;
}

namespace finevox::game {
class McpServer;
}

namespace finevox::client {

/// A game running in this process. The client only ever asks it for a connection: everything else
/// is so that a menu can start one and open it up. Whoever makes these decides what a game is, so
/// a client built without a server has no need of the code.
class LocalHost {
public:
    virtual ~LocalHost() = default;

    [[nodiscard]] virtual std::unique_ptr<finenet::Connection> connect() = 0;
    virtual bool openToNetwork(uint16_t port, std::string& error) = 0;
    [[nodiscard]] virtual uint16_t networkPort() const = 0;
    virtual void stop() = 0;

    /// The content this game loaded, for a client in the same process to share: registries are
    /// global, so loading it twice would be wrong. Valid once the world has arrived.
    [[nodiscard]] virtual ContentPackLoader* content() = 0;
};

struct HostRequest {
    bool newGame = false;
    bool autoLaunch = false;  ///< Straight in, with no pre-game
    bool listen = false;      ///< On the network from the start
    uint16_t port = 0;
    std::string title;
    std::string password;
};

using HostFactory = std::function<std::unique_ptr<LocalHost>(const HostRequest&, std::string& error)>;

enum class ClientStart : uint8_t {
    Menu,      ///< The game's menu, or straight into a game if it hasn't one
    NewGame,   ///< A new game for one
    Continue,  ///< The game for one that was saved
    Join,      ///< Go to `joinAddress`
};

struct ClientOptions {
    std::filesystem::path gameRoot;
    std::filesystem::path shaderDir;
    ClientStart start = ClientStart::Menu;
    std::string joinAddress;
    std::string password;
    std::string playerName = "Player";
    HostFactory hostFactory;          ///< Null means this client can only join
    game::McpServer* mcp = nullptr;   ///< This client's tools go here, if it's set

    uint32_t width = 1600;
    uint32_t height = 900;
    bool validation = false;

    /// Save a screenshot after this many frames (for automated visual checks)
    std::string screenshotPath;
    int screenshotFrame = 0;

    /// With screenshotPath: also save this many more frames, `burstInterval` apart, as
    /// PATH_1.png, PATH_2.png, ... Comparing them measures flicker, which stills can't.
    int burstCount = 0;
    int burstInterval = 10;

    /// Print render statistics once a second
    bool printStats = false;

    /// Close the window after this many frames (0 = run until closed)
    int exitAfterFrames = 0;
};

class GameClient {
public:
    explicit GameClient(ClientOptions options);
    ~GameClient();

    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    /// Run until the window closes. Returns a process exit code.
    int run();

    /// True if the player asked to start over, which the caller does by starting afresh
    [[nodiscard]] bool newGameRequested() const;

    /// True if the player asked to go back to the menu. The same way, for the same reason.
    [[nodiscard]] bool restartRequested() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox::client
