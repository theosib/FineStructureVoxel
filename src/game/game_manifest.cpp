#include "finevox/game/game_manifest.hpp"
#include "finevox/core/config_parser.hpp"

namespace finevox::game {

std::optional<GameManifest> GameManifest::load(const std::filesystem::path& gameRoot) {
    ConfigParser parser;
    auto doc = parser.parseFile((gameRoot / "game.conf").string());
    if (!doc) return std::nullopt;

    GameManifest m;
    m.name = std::string(doc->getString("name", m.name));
    m.contentNamespace = std::string(doc->getString("namespace", m.contentNamespace));
    m.windowTitle = std::string(doc->getString("window_title", m.name));
    m.mainScript = std::string(doc->getString("main_script", m.mainScript));
    m.uiScript = std::string(doc->getString("ui_script", m.uiScript));
    m.worldSeed = static_cast<uint64_t>(doc->getInt("world_seed", static_cast<int>(m.worldSeed)));
    m.worldRadius = doc->getInt("world_radius", m.worldRadius);
    m.seaLevel = doc->getInt("sea_level", m.seaLevel);
    m.caves = doc->getBool("caves", m.caves);
    m.tickRate = static_cast<uint32_t>(doc->getInt("tick_rate", static_cast<int>(m.tickRate)));
    m.gravity = doc->getFloat("gravity", m.gravity);
    m.viewDistance = doc->getFloat("view_distance", m.viewDistance);
    m.defaultLocale = std::string(doc->getString("default_locale", m.defaultLocale));
    return m;
}

}  // namespace finevox::game
