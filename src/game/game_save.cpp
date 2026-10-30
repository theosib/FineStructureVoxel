/**
 * @file game_save.cpp
 * @brief Writing a running game to disk and picking it up again
 *
 * Only what can't be worked out again is kept. Paths, work claims, and shots in flight aren't,
 * so a game's `on :resume` handler sets its units to choose new work.
 *
 * Saves are written to one side and renamed into place, so a failure at any point leaves
 * the last good save alone. The save it replaces stays as a `.bak`.
 */

#include "finevox/game/game_runtime.hpp"

#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_serializer.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/game_session.hpp"
#include "finevox/core/region_file.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/script_game_subsystem.hpp"
#include "finevox/script/value_graph.hpp"

#include <finescript/cbor.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>

namespace finevox::game {

namespace fs = std::filesystem;
using finescript::Value;

namespace {

constexpr int64_t SAVE_VERSION = 1;

void writeFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.flush();
    if (!out) throw std::runtime_error("can't write " + path.string());
}

std::vector<uint8_t> readFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("can't read " + path.string());
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Value number(double v) { return Value::number(v); }
Value integer(int64_t v) { return Value::integer(v); }

/// Regions are many columns to a file, and opening one per column would be slow
class Regions {
public:
    explicit Regions(fs::path directory) : directory_(std::move(directory)) {
        fs::create_directories(directory_);
    }

    RegionFile& at(ColumnPos pos) {
        RegionPos region = RegionPos::fromColumn(pos);
        auto key = std::make_pair(region.rx, region.rz);
        auto found = open_.find(key);
        if (found == open_.end()) {
            found = open_.emplace(key, std::make_unique<RegionFile>(directory_, region)).first;
        }
        return *found->second;
    }

    void flush() {
        for (auto& [key, file] : open_) file->flush();
    }

private:
    fs::path directory_;
    std::map<std::pair<int32_t, int32_t>, std::unique_ptr<RegionFile>> open_;
};

void saveColumns(const World& world, const fs::path& directory) {
    Regions regions(directory);
    bool ok = true;
    world.forEachColumn([&](ColumnPos pos, const ChunkColumn& column) {
        ok = regions.at(pos).saveColumn(column, pos) && ok;
    });
    regions.flush();
    if (!ok) throw std::runtime_error("couldn't write a column");
}

void loadColumns(World& world, const fs::path& directory, int32_t radius) {
    Regions regions(directory);
    for (int32_t x = -radius; x < radius; ++x) {
        for (int32_t z = -radius; z < radius; ++z) {
            ColumnPos pos(x, z);
            if (auto column = regions.at(pos).loadColumn(pos)) {
                world.insertColumn(pos, std::move(column));
            }
        }
    }
}

}  // namespace

fs::path GameRuntime::savePath() const {
    if (options_.noSaves) return {};
    if (!options_.savePath.empty()) return options_.savePath;
    return manifest_.saves ? options_.gameRoot / "saves" / "autosave" : fs::path{};
}

bool GameRuntime::saveExists() const {
    fs::path path = savePath();
    return !path.empty() && fs::is_regular_file(path / "game.cbor");
}

Value GameRuntime::saveSites() {
    std::vector<Value> sites;
    for (const auto& [id, site] : sites_) {
        std::vector<Value> cells;
        cells.reserve(site.cells().size() * 5);
        for (const worldgen::SiteCell& cell : site.cells()) {
            cells.push_back(integer(cell.pos.x));
            cells.push_back(integer(cell.pos.y));
            cells.push_back(integer(cell.pos.z));
            cells.push_back(Value::string(std::string(cell.block.name())));
            cells.push_back(integer(cell.rotation.index()));
        }
        BlockCoord origin = site.origin();
        sites.push_back(Value::array(std::vector<Value>{
            integer(id), integer(origin.x), integer(origin.y), integer(origin.z),
            Value::array(std::move(cells))}));
    }
    return Value::array(std::move(sites));
}

void GameRuntime::loadSites(const Value& saved, int64_t nextId) {
    for (const Value& entry : saved.asArray()) {
        const auto& site = entry.asArray();
        std::vector<worldgen::SiteCell> cells;
        const auto& flat = site[4].asArray();
        cells.reserve(flat.size() / 5);
        for (size_t i = 0; i + 4 < flat.size(); i += 5) {
            worldgen::SiteCell cell;
            cell.pos = BlockCoord(static_cast<int32_t>(flat[i].asInt()), static_cast<int32_t>(flat[i + 1].asInt()),
                                  static_cast<int32_t>(flat[i + 2].asInt()));
            cell.block = BlockTypeId::fromName(flat[i + 3].asString());
            cell.rotation = Rotation::byIndex(static_cast<uint8_t>(flat[i + 4].asInt()));
            cells.push_back(cell);
        }
        BlockCoord origin(static_cast<int32_t>(site[1].asInt()), static_cast<int32_t>(site[2].asInt()),
                          static_cast<int32_t>(site[3].asInt()));
        sites_.emplace(site[0].asInt(), worldgen::BlueprintSite::restored(origin, std::move(cells)));
    }
    nextSiteId_ = nextId;
}

bool GameRuntime::saveGame(std::string& error) {
    fs::path target = savePath();
    if (target.empty()) return true;

    fs::path partial = target;
    partial += ".tmp";
    fs::path backup = target;
    backup += ".bak";

    try {
        fs::remove_all(partial);
        fs::create_directories(partial);

        // The script goes first: it's the part that can refuse, before anything is written
        savedScriptState_ = Value::nil();
        gameScript_->fire("save");
        auto& interner = scripts_->engine().interner();
        writeFile(partial / "script.cbor", script::encodeValueGraph(savedScriptState_, interner));

        saveColumns(std::as_const(session_->world()), partial / "world");
        saveColumns(std::as_const(replica_->world()), partial / "replica");

        // Each entity by its own id, which is how scripts know it
        std::vector<Value> entities;
        EntityId local = session_->entities().localPlayerId();
        EntityId highest = 0;
        for (const auto& [id, entity] : session_->entities().entities()) {
            highest = std::max(highest, id);
            if (id == local) continue;
            auto bytes = EntitySerializer::entityToData(*entity)->toCBOR();
            std::string blob(bytes.begin(), bytes.end());
            entities.push_back(Value::array(std::vector<Value>{integer(static_cast<int64_t>(id)),
                                                               Value::string(std::move(blob))}));
        }

        FactionVisionState sight = vision_->exportState();
        std::vector<Value> factions;
        for (const auto& [faction, cells] : sight.factions) {
            factions.push_back(Value::array(std::vector<Value>{
                integer(faction), Value::string(std::string(cells.begin(), cells.end()))}));
        }
        std::vector<Value> lookouts;
        for (const FactionVisionState::Lookout& lookout : sight.lookouts) {
            lookouts.push_back(Value::array(std::vector<Value>{
                integer(lookout.handle), integer(lookout.faction), number(lookout.eye.x),
                number(lookout.eye.y), number(lookout.eye.z), number(lookout.radius)}));
        }

        Value game = Value::map();
        auto& map = game.asMap();
        auto set = [&](const char* name, Value value) {
            map.set(scripts_->engine().intern(name), std::move(value));
        };
        set("version", integer(SAVE_VERSION));
        set("tick", integer(static_cast<int64_t>(gameScript_->tickCount())));
        set("world_time", integer(session_->worldTime().totalTicks()));
        set("next_entity", integer(static_cast<int64_t>(highest) + 1));
        set("next_site", integer(nextSiteId_));
        set("next_lookout", integer(sight.nextHandle));
        set("sites", saveSites());
        set("entities", Value::array(std::move(entities)));
        set("vision", Value::array(std::move(factions)));
        set("lookouts", Value::array(std::move(lookouts)));
        set("pivot", Value::array(std::vector<Value>{number(clientSave_.pivot.x), number(clientSave_.pivot.y),
                                                     number(clientSave_.pivot.z)}));
        set("steward", Value::array(std::vector<Value>{number(clientSave_.steward.x), number(clientSave_.steward.y),
                                                       number(clientSave_.steward.z)}));
        set("camera", Value::array(std::vector<Value>{number(clientSave_.yaw), number(clientSave_.pitch),
                                                      number(clientSave_.distance)}));
        // Written last: a save counts as there only if this is
        writeFile(partial / "game.cbor", finescript::cborEncode(game, interner));

        fs::create_directories(target.parent_path());
        fs::remove_all(backup);
        if (fs::exists(target)) fs::rename(target, backup);
        fs::rename(partial, target);
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        std::error_code ignored;
        fs::remove_all(partial, ignored);
        return false;
    }
}

void GameRuntime::loadSavedWorld() {
    fs::path path = savePath();
    loadColumns(session_->world(), path / "world", manifest_.worldRadius);
    lightWorld();
}

void GameRuntime::loadSavedState() {
    fs::path path = savePath();
    auto& interner = scripts_->engine().interner();
    Value game = finescript::cborDecode(readFile(path / "game.cbor"), interner);
    auto& map = game.asMap();
    auto get = [&](const char* name) { return map.get(scripts_->engine().intern(name)); };

    if (get("version").asInt() != SAVE_VERSION) {
        throw std::runtime_error("save is from a different version of the game");
    }

    savedScriptState_ = script::decodeValueGraph(readFile(path / "script.cbor"), interner);
    loadColumns(replica_->world(), path / "replica", manifest_.worldRadius);
    loadSites(get("sites"), get("next_site").asInt());

    EntityManager& entities = session_->entities();
    for (const Value& entry : get("entities").asArray()) {
        const auto& pair = entry.asArray();
        const std::string& blob = pair[1].asString();
        auto bytes = std::vector<uint8_t>(blob.begin(), blob.end());
        auto data = DataContainer::fromCBOR(bytes);
        auto entity = data ? EntitySerializer::entityFromData(*data) : nullptr;
        if (!entity) throw std::runtime_error("a saved entity won't load");
        entity->setId(static_cast<EntityId>(pair[0].asInt()));
        entities.spawnEntity(std::move(entity));
    }
    entities.reserveIdsBelow(static_cast<EntityId>(get("next_entity").asInt()));

    FactionVisionState sight;
    sight.nextHandle = get("next_lookout").asInt();
    for (const Value& entry : get("vision").asArray()) {
        const auto& pair = entry.asArray();
        const std::string& cells = pair[1].asString();
        sight.factions.emplace_back(static_cast<uint16_t>(pair[0].asInt()),
                                    std::vector<uint8_t>(cells.begin(), cells.end()));
    }
    for (const Value& entry : get("lookouts").asArray()) {
        const auto& v = entry.asArray();
        sight.lookouts.push_back({v[0].asInt(), static_cast<uint16_t>(v[1].asInt()),
                                  glm::dvec3(v[2].asNumber(), v[3].asNumber(), v[4].asNumber()),
                                  static_cast<float>(v[5].asNumber())});
    }
    vision_->importState(sight);

    gameScript_->setTickCount(static_cast<uint64_t>(get("tick").asInt()));
    session_->worldTime().setTime(get("world_time").asInt());

    const auto& pivot = get("pivot").asArray();
    const auto& steward = get("steward").asArray();
    const auto& camera = get("camera").asArray();
    clientSave_.pivot = glm::dvec3(pivot[0].asNumber(), pivot[1].asNumber(), pivot[2].asNumber());
    clientSave_.steward = glm::dvec3(steward[0].asNumber(), steward[1].asNumber(), steward[2].asNumber());
    clientSave_.yaw = static_cast<float>(camera[0].asNumber());
    clientSave_.pitch = static_cast<float>(camera[1].asNumber());
    clientSave_.distance = static_cast<float>(camera[2].asNumber());
}

}  // namespace finevox::game
