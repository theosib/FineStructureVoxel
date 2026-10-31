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
    m.randomTicks = static_cast<uint32_t>(doc->getInt("random_ticks", static_cast<int>(m.randomTicks)));
    m.gravity = doc->getFloat("gravity", m.gravity);
    m.viewDistance = doc->getFloat("view_distance", m.viewDistance);
    m.lod = doc->getBool("lod", m.lod);
    m.navMaxFall = doc->getInt("nav_max_fall", m.navMaxFall);
    m.stepHeight = doc->getFloat("step_height", m.stepHeight);
    m.saves = doc->getBool("saves", m.saves);
    m.autosaveMinutes = doc->getFloat("autosave_minutes", m.autosaveMinutes);
    m.defaultLocale = std::string(doc->getString("default_locale", m.defaultLocale));
    m.fogOfWar = doc->getBool("fog_of_war", m.fogOfWar);
    m.viewerFaction = static_cast<uint16_t>(doc->getInt("viewer_faction", m.viewerFaction));
    return m;
}

}  // namespace finevox::game
