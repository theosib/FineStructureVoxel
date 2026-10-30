/**
 * @file finevox_run.cpp
 * @brief Run a game from its content directory
 *
 *   finevox_run --game-root DIR [--seed S] [--radius R] [--script FILE]
 *               [--screenshot PATH --screenshot-frame N] [--exit-after N]
 */

#include "finevox/client/game_client.hpp"
#include "finevox/game/game_runtime.hpp"

#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>
#include <string>

using namespace finevox;

int main(int argc, char* argv[]) {
    game::RuntimeOptions runtimeOptions;
    client::ClientOptions clientOptions;
    clientOptions.shaderDir = FINEVOX_SHADER_DIR;

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
        } else if (arg == "--new") {
            runtimeOptions.newGame = true;
        } else if (arg == "--no-save") {
            runtimeOptions.noSaves = true;
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

    bool startOver = false;
    try {
        game::GameRuntime runtime(runtimeOptions);
        runtime.load();
        client::GameClient gameClient(runtime, clientOptions);
        int code = gameClient.run();
        if (!gameClient.newGameRequested()) return code;
        startOver = true;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }

    if (startOver) {
        std::vector<char*> arguments(argv, argv + argc);
        std::string newGame = "--new";
        arguments.push_back(newGame.data());
        arguments.push_back(nullptr);
        execv(argv[0], arguments.data());
        std::cerr << "error: couldn't start a new game\n";
        return 1;
    }
    return 0;
}
