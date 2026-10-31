#include "finevox/game/mcp_tools.hpp"

#include "finevox/core/entity_manager.hpp"
#include "finevox/core/game_actions.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/world_time.hpp"
#include "finevox/game/game_runtime.hpp"

#include <finescript/execution_context.h>
#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

using json = nlohmann::json;
using finescript::Value;

namespace finevox::game {

namespace {

constexpr int64_t MAX_BLOCK_CELLS = 32768;
constexpr std::string_view PALETTE_LETTERS =
    "#ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

json valueToJson(const Value& value, finescript::ScriptEngine& engine, const McpValueLimits& limits,
                 int depth, int& nodes, bool& truncated) {
    if (++nodes > limits.maxNodes || depth > limits.maxDepth) {
        truncated = true;
        return "...";
    }
    if (value.isNil()) return nullptr;
    if (value.isBool()) return value.asBool();
    if (value.isInt()) return value.asInt();
    if (value.isFloat()) return value.asFloat();
    if (value.isString()) return value.asString();
    if (value.isSymbol()) return ":" + std::string(engine.lookupSymbol(value.asSymbol()));
    if (value.isCallable()) return "<function>";
    if (value.isArray()) {
        json out = json::array();
        for (const Value& element : value.asArray()) {
            out.push_back(valueToJson(element, engine, limits, depth + 1, nodes, truncated));
        }
        return out;
    }
    if (value.isMap()) {
        json out = json::object();
        const auto& map = value.asMap();
        for (uint32_t key : map.keys()) {
            out[std::string(engine.lookupSymbol(key))] =
                valueToJson(map.get(key), engine, limits, depth + 1, nodes, truncated);
        }
        return out;
    }
    return value.toString();
}

}  // namespace

json mcpValueToJson(const Value& value, finescript::ScriptEngine& engine, const McpValueLimits& limits,
                    bool& truncated) {
    int nodes = 0;
    return valueToJson(value, engine, limits, 0, nodes, truncated);
}

Value mcpJsonToValue(const json& value, finescript::ScriptEngine& engine) {
    if (value.is_boolean()) return Value::boolean(value.get<bool>());
    if (value.is_number_integer()) return Value::integer(value.get<int64_t>());
    if (value.is_number()) return Value::number(value.get<double>());
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() > 1 && text[0] == ':') {
            return Value::symbol(engine.intern(std::string_view(text).substr(1)));
        }
        return Value::string(text);
    }
    if (value.is_array()) {
        std::vector<Value> elements;
        elements.reserve(value.size());
        for (const json& element : value) elements.push_back(mcpJsonToValue(element, engine));
        return Value::array(std::move(elements));
    }
    if (value.is_object()) {
        auto map = std::make_shared<finescript::MapData>();
        for (const auto& [key, element] : value.items()) {
            map->set(engine.intern(key), mcpJsonToValue(element, engine));
        }
        return Value::map(std::move(map));
    }
    return Value::nil();
}

Value mcpCommandValue(const json& args, finescript::ScriptEngine& engine) {
    Value command = mcpJsonToValue(args.value("args", json::object()), engine);
    std::string name = args.at("name").get<std::string>();
    if (!name.empty() && name[0] == ':') name.erase(0, 1);
    command.asMap().set(engine.intern("name"), Value::symbol(engine.intern(name)));
    return command;
}

json mcpCommandSchema() {
    return {{"type", "object"},
            {"properties",
             {{"name", {{"type", "string"}, {"description", "The command's name"}}},
              {"args",
               {{"type", "object"},
                {"description", "Its other fields. Strings like \":name\" are sent as symbols."}}}}},
            {"required", {"name"}}};
}

json mcpEvalSchema() {
    return {{"type", "object"},
            {"properties",
             {{"code", {{"type", "string"}, {"description", "finescript. The value of the last statement comes back."}}},
              {"max_depth", {{"type", "integer"}, {"description", "How deep to show maps and arrays (default 6)"}}},
              {"max_nodes", {{"type", "integer"}, {"description", "How many values to show (default 2000)"}}}}},
            {"required", {"code"}}};
}

McpResult mcpEval(const json& args, finescript::ScriptEngine& engine, finescript::ExecutionContext& context) {
    McpValueLimits limits;
    limits.maxDepth = args.value("max_depth", limits.maxDepth);
    limits.maxNodes = args.value("max_nodes", limits.maxNodes);

    auto result = engine.executeCommand(args.at("code").get<std::string>(), context);
    if (!result.success) {
        return McpResult::error("line " + std::to_string(result.errorLine) + ": " + result.error);
    }
    bool truncated = false;
    json value = mcpValueToJson(result.returnValue, engine, limits, truncated);
    return McpResult::value({{"value", std::move(value)}, {"truncated", truncated}});
}

json mcpEntityFilterSchema() {
    return {{"type", "object"},
            {"properties",
             {{"type", {{"type", "string"}, {"description", "Only this entity type"}}},
              {"faction", {{"type", "integer"}, {"description", "Only this faction"}}},
              {"near", {{"type", "array"}, {"description", "[x, y, z]: only within `radius` of here"}}},
              {"radius", {{"type", "number"}, {"description", "With `near` (default 16)"}}},
              {"limit", {{"type", "integer"}, {"description", "At most this many (default 100)"}}}}}};
}

bool mcpEntityMatches(const json& args, std::string_view type, int faction, double x, double y, double z) {
    if (args.contains("type") && args["type"].get<std::string>() != type) return false;
    if (args.contains("faction") && args["faction"].get<int>() != faction) return false;
    if (args.contains("near")) {
        const json& near = args["near"];
        if (near.size() != 3) throw std::runtime_error("near must be [x, y, z]");
        double dx = near[0].get<double>() - x;
        double dy = near[1].get<double>() - y;
        double dz = near[2].get<double>() - z;
        double radius = args.value("radius", 16.0);
        if (dx * dx + dy * dy + dz * dz > radius * radius) return false;
    }
    return true;
}

json mcpBlocksSchema() {
    return {{"type", "object"},
            {"properties",
             {{"min", {{"type", "array"}, {"description", "[x, y, z], one corner"}}},
              {"max", {{"type", "array"}, {"description", "[x, y, z], the other, included"}}}}},
            {"required", {"min", "max"}}};
}

McpResult mcpBlocks(const json& args, const World& world) {
    const json& a = args.at("min");
    const json& b = args.at("max");
    if (a.size() != 3 || b.size() != 3) return McpResult::error("min and max must be [x, y, z]");
    int32_t x0 = std::min(a[0].get<int32_t>(), b[0].get<int32_t>());
    int32_t y0 = std::min(a[1].get<int32_t>(), b[1].get<int32_t>());
    int32_t z0 = std::min(a[2].get<int32_t>(), b[2].get<int32_t>());
    int32_t x1 = std::max(a[0].get<int32_t>(), b[0].get<int32_t>());
    int32_t y1 = std::max(a[1].get<int32_t>(), b[1].get<int32_t>());
    int32_t z1 = std::max(a[2].get<int32_t>(), b[2].get<int32_t>());
    int64_t cells = int64_t(x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1);
    if (cells > MAX_BLOCK_CELLS) {
        return McpResult::error("that's " + std::to_string(cells) + " cells, and the limit is " +
                                std::to_string(MAX_BLOCK_CELLS));
    }

    std::unordered_map<uint32_t, char> letters;
    json palette = {{".", "air"}};
    json layers = json::array();
    for (int32_t y = y0; y <= y1; ++y) {
        json rows = json::array();
        for (int32_t z = z0; z <= z1; ++z) {
            std::string row;
            row.reserve(static_cast<size_t>(x1 - x0 + 1));
            for (int32_t x = x0; x <= x1; ++x) {
                BlockTypeId block = world.getBlock(x, y, z);
                if (block.isAir()) {
                    row += '.';
                    continue;
                }
                auto [found, added] = letters.try_emplace(block.id, '?');
                if (added && letters.size() <= PALETTE_LETTERS.size()) {
                    found->second = PALETTE_LETTERS[letters.size() - 1];
                    palette[std::string(1, found->second)] = std::string(block.name());
                }
                row += found->second;
            }
            rows.push_back(std::move(row));
        }
        layers.push_back({{"y", y}, {"rows", std::move(rows)}});
    }
    return McpResult::value({{"palette", std::move(palette)}, {"x0", x0}, {"z0", z0}, {"layers", std::move(layers)}});
}

namespace {

json entityJson(const Entity& entity) {
    Vec3 pos = entity.position();
    Vec3 vel = entity.velocity();
    json out = {{"id", entity.id()},
                {"type", entity.typeName()},
                {"faction", entity.faction()},
                {"position", {pos.x, pos.y, pos.z}},
                {"velocity", {vel.x, vel.y, vel.z}},
                {"yaw", entity.yaw()},
                {"on_ground", entity.isOnGround()},
                {"alive", entity.isAlive()}};
    if (const auto* mob = dynamic_cast<const MobEntity*>(&entity)) {
        out["health"] = mob->health();
        out["max_health"] = mob->maxHealth();
        out["player"] = mob->isPlayerEntity();
    }
    return out;
}

}  // namespace

void addServerMcpTools(McpServer& server, GameRuntime& runtime) {
    McpTool state;
    state.name = "server_state";
    state.description =
        "The simulation at a glance: tick, time of day, how many entities and building sites "
        "there are, and every distinct script error so far.";
    state.inputSchema = {{"type", "object"}, {"properties", json::object()}};
    state.side = McpSide::Game;
    state.run = [&runtime](const json&) {
        auto& script = runtime.gameScript();
        return McpResult::value({{"game", runtime.manifest().name},
                                 {"tick", script.tickCount()},
                                 {"time_of_day", runtime.session().worldTime().timeOfDay()},
                                 {"entities", runtime.session().entities().entities().size()},
                                 {"sites", runtime.sites().size()},
                                 {"script_errors", script.errors()},
                                 {"script_error_count", script.errorCount()}});
    };
    server.addTool(std::move(state));

    McpTool eval;
    eval.name = "server_eval";
    eval.description =
        "Run finescript on the game thread, between ticks, with the game script's globals and "
        "natives. This reaches all of the game's state, and can change it. An `on` handler it "
        "registers stays registered, which makes a probe that runs every tick. Calls need "
        "braces: {ent_pos id}. print output goes to `logs`.";
    eval.inputSchema = mcpEvalSchema();
    eval.side = McpSide::Game;
    eval.run = [&runtime](const json& args) {
        return mcpEval(args, runtime.scripts().engine(), runtime.gameScript().context());
    };
    server.addTool(std::move(eval));

    McpTool entities;
    entities.name = "server_entities";
    entities.description =
        "Entities as the simulation has them, whatever the viewer can see: id, type, faction, "
        "position, velocity, health. Filter with type, faction, near and radius.";
    entities.inputSchema = mcpEntityFilterSchema();
    entities.side = McpSide::Game;
    entities.run = [&runtime](const json& args) {
        size_t limit = static_cast<size_t>(std::max(0, args.value("limit", 100)));
        json out = json::array();
        size_t matched = 0;
        for (const auto& [id, entity] : runtime.session().entities().entities()) {
            Vec3 pos = entity->position();
            if (!mcpEntityMatches(args, entity->typeName(), entity->faction(), pos.x, pos.y, pos.z)) {
                continue;
            }
            if (++matched <= limit) out.push_back(entityJson(*entity));
        }
        return McpResult::value({{"entities", std::move(out)}, {"matched", matched}});
    };
    server.addTool(std::move(entities));

    McpTool blocks;
    blocks.name = "server_blocks";
    blocks.description =
        "A region of the real world as text: a palette, and for each y, a row of characters per "
        "z, one per x, starting at x0 and z0. Compare with client_blocks to see what the client "
        "has wrong or hasn't heard about yet. Limit 32768 cells.";
    blocks.inputSchema = mcpBlocksSchema();
    blocks.side = McpSide::Game;
    blocks.run = [&runtime](const json& args) { return mcpBlocks(args, runtime.session().world()); };
    server.addTool(std::move(blocks));

    McpTool command;
    command.name = "server_command";
    command.description =
        "Send a game command, as the client's UI would, to the game script's `on :command` "
        "handlers. Where a window is open, client_command does the same as a player.";
    command.inputSchema = mcpCommandSchema();
    command.side = McpSide::Game;
    command.run = [&runtime](const json& args) {
        runtime.session().actions().sendGameCommand(mcpCommandValue(args, runtime.scripts().engine()));
        return McpResult::text("sent; the game thread takes it up between ticks");
    };
    server.addTool(std::move(command));
}

}  // namespace finevox::game
