/**
 * @file finevox_headless.cpp
 * @brief Run a game with no window: for scenario tests and simulation
 *
 *   finevox_headless --game-root DIR [--ticks N] [--seed S] [--radius R]
 *                    [--script FILE]... [--define NAME=VALUE]...
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
