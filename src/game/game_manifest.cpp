#include "finevox/game/game_manifest.hpp"
#include "finevox/core/config_parser.hpp"

#include <charconv>
#include <sstream>

namespace finevox::game {

namespace {

std::vector<std::string> splitWords(std::string_view text) {
    std::istringstream in{std::string(text)};
    std::vector<std::string> out;
    for (std::string word; in >> word;) out.push_back(word);
    return out;
}

}  // namespace

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
    m.menuScript = std::string(doc->getString("menu_script", m.menuScript));
    m.lobbyScript = std::string(doc->getString("lobby_script", m.lobbyScript));
    m.clientContent = splitWords(doc->getString("client_content", ""));
    m.netPort = static_cast<uint16_t>(doc->getInt("net_port", m.netPort));
    m.worldBiomes = splitWords(doc->getString("world_biomes", ""));
    m.worldSeed = static_cast<uint64_t>(doc->getInt("world_seed", static_cast<int>(m.worldSeed)));
    m.worldRadius = doc->getInt("world_radius", m.worldRadius);
    m.seaLevel = doc->getInt("sea_level", m.seaLevel);
    m.caves = doc->getBool("caves", m.caves);
    m.terrain = std::string(doc->getString("terrain", m.terrain));
    m.tickRate = static_cast<uint32_t>(doc->getInt("tick_rate", static_cast<int>(m.tickRate)));
    m.randomTicks = static_cast<uint32_t>(doc->getInt("random_ticks", static_cast<int>(m.randomTicks)));
    m.gravity = doc->getFloat("gravity", m.gravity);
    m.viewDistance = doc->getFloat("view_distance", m.viewDistance);
    m.lod = doc->getBool("lod", m.lod);
    m.detailPixels = doc->getFloat("detail_pixels", m.detailPixels);
    m.skySun = std::string(doc->getString("sky_sun", m.skySun));
    m.skyMoon = splitWords(doc->getString("sky_moon", ""));
    m.skyBodySize = doc->getFloat("sky_body_size", m.skyBodySize);
    m.skyClouds = std::string(doc->getString("sky_clouds", m.skyClouds));
    m.cloudHeight = doc->getFloat("cloud_height", m.cloudHeight);
    m.cloudCell = doc->getFloat("cloud_cell", m.cloudCell);
    m.cloudSpeed = doc->getFloat("cloud_speed", m.cloudSpeed);
    m.navMaxFall = doc->getInt("nav_max_fall", m.navMaxFall);
    m.stepHeight = doc->getFloat("step_height", m.stepHeight);
    m.playerEntity = std::string(doc->getString("player_entity", m.playerEntity));
    m.saves = doc->getBool("saves", m.saves);
    m.autosaveMinutes = doc->getFloat("autosave_minutes", m.autosaveMinutes);
    m.defaultLocale = std::string(doc->getString("default_locale", m.defaultLocale));
    m.fogOfWar = doc->getBool("fog_of_war", m.fogOfWar);
    m.viewerFaction = static_cast<uint16_t>(doc->getInt("viewer_faction", m.viewerFaction));
    return m;
}

bool GameManifest::applyOverride(std::string_view key, std::string_view value) {
    auto number = [&](auto& out) {
        std::remove_reference_t<decltype(out)> parsed{};
        auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
        if (error != std::errc{} || end != value.data() + value.size()) return false;
        out = parsed;
        return true;
    };
    if (key == "world_seed") return number(worldSeed);
    if (key == "world_radius") return number(worldRadius);
    if (key == "sea_level") return number(seaLevel);
    if (key == "terrain") {
        terrain = std::string(value);
        return true;
    }
    if (key == "caves") {
        caves = value == "true";
        return true;
    }
    if (key == "fog_of_war") {
        fogOfWar = value == "true";
        return true;
    }
    if (key == "world_biomes") {
        worldBiomes = splitWords(value);
        return true;
    }
    return false;
}

}  // namespace finevox::game
