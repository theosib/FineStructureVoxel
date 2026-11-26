#include "finevox/game/game_manifest.hpp"
#include "finevox/core/config_parser.hpp"

#include <algorithm>
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
    auto& w = m.waterways;
    w.riverScale = doc->getFloat("river_scale", w.riverScale);
    w.riverWidth = doc->getFloat("river_width", w.riverWidth);
    w.riverDepth = doc->getInt("river_depth", w.riverDepth);
    w.riverLine = doc->getString("river_course", "noise") == "line";
    w.riverHeading = doc->getFloat("river_heading", w.riverHeading);
    w.riverMeander = doc->getFloat("river_meander", w.riverMeander);
    w.riverMarshes = doc->getFloat("river_marshes", w.riverMarshes);
    w.marshWidth = doc->getFloat("marsh_width", w.marshWidth);
    w.marshBlock = std::string(doc->getString("marsh_block", w.marshBlock));
    w.riverFords = doc->getFloat("river_fords", w.riverFords);
    w.fordBlock = std::string(doc->getString("ford_block", w.fordBlock));
    w.lakeScale = doc->getFloat("lake_scale", w.lakeScale);
    w.lakeThreshold = doc->getFloat("lake_threshold", w.lakeThreshold);
    w.lakeDepth = doc->getInt("lake_depth", w.lakeDepth);
    w.bankSlope = doc->getFloat("bank_slope", w.bankSlope);
    m.shoreHeight = doc->getInt("shore_height", m.shoreHeight);
    m.tickRate = static_cast<uint32_t>(doc->getInt("tick_rate", static_cast<int>(m.tickRate)));
    m.randomTicks = static_cast<uint32_t>(doc->getInt("random_ticks", static_cast<int>(m.randomTicks)));
    m.signalMaxLevel = static_cast<uint32_t>(std::clamp(doc->getInt("signal_max_level", 0), 0, 255));
    if (const auto* relays = doc->get("signal_relays")) m.signalOpaqueRelays = relays->value.asStringOwned() != "none";
    m.gravity = doc->getFloat("gravity", m.gravity);
    m.viewDistance = doc->getFloat("view_distance", m.viewDistance);
    m.lod = doc->getBool("lod", m.lod);
    m.nearestFilter = doc->getBool("nearest_filter", m.nearestFilter);
    m.detailPixels = doc->getFloat("detail_pixels", m.detailPixels);
    m.skySun = std::string(doc->getString("sky_sun", m.skySun));
    m.skyMoon = splitWords(doc->getString("sky_moon", ""));
    m.skyBodySize = doc->getFloat("sky_body_size", m.skyBodySize);
    m.skyClouds = std::string(doc->getString("sky_clouds", m.skyClouds));
    m.cloudHeight = doc->getFloat("cloud_height", m.cloudHeight);
    m.cloudCell = doc->getFloat("cloud_cell", m.cloudCell);
    m.cloudSpeed = doc->getFloat("cloud_speed", m.cloudSpeed);
    m.navMaxFall = doc->getInt("nav_max_fall", m.navMaxFall);
    m.navMaxWade = doc->getInt("nav_max_wade", m.navMaxWade);
    m.stepHeight = doc->getFloat("step_height", m.stepHeight);
    m.solidSlopes = doc->getBool("solid_slopes", m.solidSlopes);
    m.walkSpeed = doc->getFloat("walk_speed", m.walkSpeed);
    m.jumpVelocity = doc->getFloat("jump_velocity", m.jumpVelocity);
    m.sneakSpeed = doc->getFloat("sneak_speed", m.sneakSpeed);
    m.sneakEyeDrop = doc->getFloat("sneak_eye_drop", m.sneakEyeDrop);
    m.sneakShrink = doc->getFloat("sneak_shrink", m.sneakShrink);
    m.playerWidth = doc->getFloat("player_width", m.playerWidth);
    m.itemPictures = std::string(doc->getString("item_pictures", m.itemPictures));
    m.markerTextures = std::string(doc->getString("marker_textures", m.markerTextures));
    m.playerHeight = doc->getFloat("player_height", m.playerHeight);
    m.flySpeed = doc->getFloat("fly_speed", m.flySpeed);
    m.flyClimb = doc->getFloat("fly_climb", m.flyClimb);
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
    if (key == "river_scale") return number(waterways.riverScale);
    if (key == "river_width") return number(waterways.riverWidth);
    if (key == "river_depth") return number(waterways.riverDepth);
    if (key == "river_marshes") return number(waterways.riverMarshes);
    if (key == "river_course") {
        waterways.riverLine = value == "line";
        return true;
    }
    if (key == "river_heading") return number(waterways.riverHeading);
    if (key == "river_meander") return number(waterways.riverMeander);
    if (key == "river_fords") return number(waterways.riverFords);
    if (key == "lake_scale") return number(waterways.lakeScale);
    if (key == "lake_threshold") return number(waterways.lakeThreshold);
    if (key == "lake_depth") return number(waterways.lakeDepth);
    if (key == "bank_slope") return number(waterways.bankSlope);
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
    if (key == "solid_slopes") {
        solidSlopes = value == "true";
        return true;
    }
    if (key == "world_biomes") {
        worldBiomes = splitWords(value);
        return true;
    }
    return false;
}

}  // namespace finevox::game
