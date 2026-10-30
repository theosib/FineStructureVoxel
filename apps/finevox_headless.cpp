/**
 * @file finevox_headless.cpp
 * @brief Run a game with no window: for scenario tests and simulation
 *
 *   finevox_headless --game-root DIR [--ticks N] [--seed S] [--radius R]
 *                    [--script FILE]... [--define NAME=VALUE]... [--save DIR | --resume DIR]
 *
 * Exits with the script's `game_exit` code. Any game script error makes the
 * exit code nonzero, so a broken handler can't pass silently.
 */

#include "finevox/game/game_runtime.hpp"

#include <chrono>
#include <iostream>
#include <string>

using namespace finevox::game;

int main(int argc, char* argv[]) {
    RuntimeOptions options;
    uint64_t ticks = 300;
    bool saveAtEnd = false;

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
        } else if (arg == "--ticks") {
            ticks = std::stoull(value());
        } else if (arg == "--save") {
            options.savePath = value();
            options.newGame = true;
            saveAtEnd = true;
        } else if (arg == "--resume") {
            options.savePath = value();
        } else if (arg == "--seed") {
            options.seedOverride = std::stoull(value());
        } else if (arg == "--radius") {
            options.radiusOverride = std::stoi(value());
        } else if (arg == "--script") {
            options.extraScripts.emplace_back(value());
        } else if (arg == "--define") {
            std::string define = value();
            auto eq = define.find('=');
            if (eq == std::string::npos) {
                std::cerr << "--define expects NAME=VALUE\n";
                return 2;
            }
            options.scriptDefines.emplace_back(define.substr(0, eq), define.substr(eq + 1));
        } else {
            std::cerr << "unknown argument: " << arg << "\n";
            return 2;
        }
    }

    // Saving is opt-in here: a scenario must never touch the player's own game
    if (options.savePath.empty()) options.noSaves = true;

    if (options.gameRoot.empty()) {
        std::cerr << "usage: finevox_headless --game-root DIR [--ticks N] [--seed S] [--radius R]"
                     " [--script FILE]... [--define NAME=VALUE]...\n";
        return 2;
    }

    try {
        GameRuntime runtime(options);
        runtime.load();
        std::cout << "[headless] " << runtime.manifest().name << ": world in "
                  << runtime.worldgenSeconds() << "s\n";

        auto started = std::chrono::steady_clock::now();
        runtime.runTicks(ticks);
        double seconds =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

        uint64_t ran = runtime.gameScript().tickCount();
        std::cout << "[headless] " << ran << " ticks in " << seconds << "s ("
                  << (ran > 0 ? seconds * 1000.0 / ran : 0.0) << " ms/tick)\n";

        if (saveAtEnd) {
            std::string error;
            auto saveStarted = std::chrono::steady_clock::now();
            if (!runtime.saveGame(error)) {
                std::cerr << "[headless] save failed: " << error << "\n";
                return 1;
            }
            double ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - saveStarted).count();
            std::cout << "[headless] saved in " << ms << "ms\n";
        }

        size_t errors = runtime.gameScript().errorCount();
        if (errors > 0) {
            std::cerr << "[headless] " << errors << " script error(s)\n";
            return runtime.exitCode() != 0 ? runtime.exitCode() : 1;
        }
        return runtime.exitCode();
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
