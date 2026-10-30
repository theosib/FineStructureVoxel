#pragma once

/**
 * @file game_client.hpp
 * @brief Window, rendering, and UI script host for a GameRuntime
 *
 * Engine-side and game-agnostic. All game UI lives in the game's ui script, which
 * runs on the render thread with its own script engine. It reaches the game only
 * through send_command and receives state only through `on :state`.
 */

#include "finevox/game/game_runtime.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace finevox::client {

struct ClientOptions {
    std::filesystem::path shaderDir;
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
    GameClient(game::GameRuntime& runtime, ClientOptions options);
    ~GameClient();

    GameClient(const GameClient&) = delete;
    GameClient& operator=(const GameClient&) = delete;

    /// Run until the window closes. Returns a process exit code.
    int run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace finevox::client
