/**
 * @file blueprint_natives.cpp
 * @brief Script access to blueprints and construction sites
 *
 * Lives with GameRuntime rather than in finevox_script because it needs worldgen,
 * which the script library deliberately doesn't link.
 */

#include "finevox/game/game_runtime.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/world.hpp"

#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <cmath>

namespace finevox::game {

namespace {

using finescript::ExecutionContext;
using finescript::Value;
using Args = std::vector<Value>;

double number(const Value& v, double fallback = 0.0) {
    return v.isNumeric() ? v.asNumber() : fallback;
}

std::string nameArg(const Value& v) {
    if (v.isSymbol()) return std::string(StringInterner::global().lookup(v.asSymbol()));
    return v.isString() ? v.asString() : std::string{};
}

}  // namespace

void registerBlueprintNatives(GameRuntime& runtime) {
    auto& e = runtime.scripts().engine();
    auto& si = StringInterner::global();
    const uint32_t keyX = si.intern("x");
    const uint32_t keyY = si.intern("y");
    const uint32_t keyZ = si.intern("z");
    const uint32_t keySizeX = si.intern("size_x");
    const uint32_t keySizeY = si.intern("size_y");
    const uint32_t keySizeZ = si.intern("size_z");
    const uint32_t keyBlock = si.intern("block");
    const uint32_t keyDone = si.intern("done");
    const uint32_t keyTotal = si.intern("total");
    const uint32_t keyComplete = si.intern("complete");
    const uint32_t keyAir = si.intern("air");

    GameRuntime* rt = &runtime;

    auto site = [rt](const Value& id) -> worldgen::BlueprintSite* {
        auto it = rt->sites().find(static_cast<int64_t>(number(id)));
        return it == rt->sites().end() ? nullptr : &it->second;
    };

    // blueprint_info name -> {=size_x =size_y =size_z} or nil
    e.registerFunction("blueprint_info", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::nil();
        const auto* schematic = rt->blueprints().find(nameArg(args[0]));
        if (!schematic) return Value::nil();
        auto result = Value::map();
        result.asMap().set(keySizeX, Value::integer(schematic->sizeX()));
        result.asMap().set(keySizeY, Value::integer(schematic->sizeY()));
        result.asMap().set(keySizeZ, Value::integer(schematic->sizeZ()));
        return result;
    });

    // blueprint_cost name -> {=block_name count ...}
    e.registerFunction("blueprint_cost", [=](ExecutionContext&, const Args& args) -> Value {
        auto result = Value::map();
        if (args.empty()) return result;
        const auto* schematic = rt->blueprints().find(nameArg(args[0]));
        if (!schematic) return result;
        std::unordered_map<uint32_t, int64_t> counts;
        schematic->forEachBlock([&counts](glm::ivec3, const worldgen::BlockSnapshot& snapshot) {
            if (snapshot.typeName.rfind("blueprint:", 0) == 0) return;
            ++counts[StringInterner::global().intern(snapshot.typeName)];
        });
        for (const auto& [id, count] : counts) result.asMap().set(id, Value::integer(count));
        return result;
    });

    // site_create name x y z [quarter_turns] -> id, or site_create -> empty site id
    e.registerFunction("site_create", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) {
            int64_t id = rt->allocateSiteId();
            rt->sites().emplace(id, worldgen::BlueprintSite{});
            return Value::integer(id);
        }
        if (args.size() < 4) return Value::nil();
        const auto* schematic = rt->blueprints().find(nameArg(args[0]));
        if (!schematic) return Value::nil();
        BlockCoord origin(static_cast<int32_t>(number(args[1])),
                          static_cast<int32_t>(number(args[2])),
                          static_cast<int32_t>(number(args[3])));
        int turns = args.size() > 4 ? static_cast<int>(number(args[4])) : 0;
        int64_t id = rt->allocateSiteId();
        rt->sites().emplace(id, worldgen::BlueprintSite(*schematic, origin, turns));
        return Value::integer(id);
    });

    // site_add id x y z block
    e.registerFunction("site_add", [=](ExecutionContext&, const Args& args) -> Value {
        auto* target = args.empty() ? nullptr : site(args[0]);
        if (!target || args.size() < 5) return Value::nil();
        std::string block = nameArg(args[4]);
        target->addCell(BlockCoord(static_cast<int32_t>(number(args[1])),
                                   static_cast<int32_t>(number(args[2])),
                                   static_cast<int32_t>(number(args[3]))),
                        block == "air" ? BlockTypeId() : BlockTypeId::fromName(block));
        return Value::nil();
    });

    // site_claim id agent x y z -> {=x =y =z =block} or nil
    e.registerFunction("site_claim", [=](ExecutionContext&, const Args& args) -> Value {
        auto* target = args.empty() ? nullptr : site(args[0]);
        if (!target || args.size() < 5) return Value::nil();
        auto cell = target->claimNext(
            rt->session().world(), glm::dvec3(number(args[2]), number(args[3]), number(args[4])),
            static_cast<EntityId>(number(args[1])));
        if (!cell) return Value::nil();
        auto result = Value::map();
        auto& map = result.asMap();
        map.set(keyX, Value::integer(cell->pos.x));
        map.set(keyY, Value::integer(cell->pos.y));
        map.set(keyZ, Value::integer(cell->pos.z));
        map.set(keyBlock, Value::symbol(cell->block.isAir() ? keyAir : cell->block.id));
        return result;
    });

    // site_release id agent: give back everything agent holds there
    e.registerFunction("site_release", [=](ExecutionContext&, const Args& args) -> Value {
        auto* target = args.empty() ? nullptr : site(args[0]);
        if (target && args.size() >= 2) {
            target->releaseAgent(static_cast<EntityId>(number(args[1])));
        }
        return Value::nil();
    });

    // site_progress id -> {=done =total =complete}
    e.registerFunction("site_progress", [=](ExecutionContext&, const Args& args) -> Value {
        auto* target = args.empty() ? nullptr : site(args[0]);
        if (!target) return Value::nil();
        auto progress = target->progress(rt->session().world());
        auto result = Value::map();
        auto& map = result.asMap();
        map.set(keyDone, Value::integer(static_cast<int64_t>(progress.done)));
        map.set(keyTotal, Value::integer(static_cast<int64_t>(progress.total)));
        map.set(keyComplete, Value::boolean(progress.complete()));
        return result;
    });

    // site_remaining id -> {=block_name count ...}: materials still needed
    e.registerFunction("site_remaining", [=](ExecutionContext&, const Args& args) -> Value {
        auto result = Value::map();
        auto* target = args.empty() ? nullptr : site(args[0]);
        if (!target) return result;
        std::unordered_map<uint32_t, int64_t> counts;
        for (const auto& cell : target->remaining(rt->session().world())) {
            if (!cell.block.isAir()) ++counts[cell.block.id];
        }
        for (const auto& [id, count] : counts) result.asMap().set(id, Value::integer(count));
        return result;
    });

    e.registerFunction("site_remove", [=](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) rt->sites().erase(static_cast<int64_t>(number(args[0])));
        return Value::nil();
    });
}

}  // namespace finevox::game
