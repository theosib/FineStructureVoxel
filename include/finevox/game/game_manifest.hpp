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
    uint32_t randomTicks = 4;     ///< Per subchunk per tick; 0 if no block wants them
    float gravity = -14.0f;
    float viewDistance = 256.0f;
    /// Merge distant blocks into coarser meshes. Suits a first-person view of a far
    /// horizon. An overhead camera keeps most of the map in the transition zone, where
    /// meshes visibly swap as it moves, so such games turn this off.
    bool lod = true;
    int navMaxFall = 3;           ///< Largest drop path-following units will take
    float stepHeight = 0.6f;      ///< Highest ledge the player walks up without jumping
    bool saves = false;           ///< Keep the game in <gameRoot>/saves and resume it
    float autosaveMinutes = 0.0f; ///< Save during play this often; 0 only saves on exit
    std::string defaultLocale = "en";

    bool fogOfWar = true;
    uint16_t viewerFaction = 1;   ///< Whose eyes the local client sees through

    /// Parse <gameRoot>/game.conf. nullopt if the file is missing or unreadable.
    [[nodiscard]] static std::optional<GameManifest> load(const std::filesystem::path& gameRoot);
};

}  // namespace finevox::game
