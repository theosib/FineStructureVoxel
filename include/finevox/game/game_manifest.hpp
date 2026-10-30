#pragma once

/**
 * @file game_manifest.hpp
 * @brief game.conf: what makes a content directory a game
 */

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace finevox::game {

struct GameManifest {
    std::string name = "Untitled";
    std::string contentNamespace = "game";
    std::string windowTitle;

    std::string mainScript = "scripts/main.fsc";  ///< Game thread
    std::string uiScript = "ui/main.fs";          ///< Render thread

    uint64_t worldSeed = 1;
    int32_t worldRadius = 16;   ///< Columns generated in each direction from the origin
    int32_t seaLevel = 62;
    bool caves = false;

    uint32_t tickRate = 30;
    float gravity = -14.0f;
    float viewDistance = 256.0f;
    std::string defaultLocale = "en";

    /// Parse <gameRoot>/game.conf. nullopt if the file is missing or unreadable.
    [[nodiscard]] static std::optional<GameManifest> load(const std::filesystem::path& gameRoot);
};

}  // namespace finevox::game
