/**
 * @file conflict_natives.cpp
 * @brief Script access to faction vision and projectiles
 */

#include "finevox/game/game_runtime.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"

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

Value option(const Args& args, size_t afterPositional, uint32_t key) {
    if (args.size() > afterPositional && args.back().isMap()) {
        return const_cast<Value&>(args.back()).asMap().get(key);
    }
    return Value::nil();
}

}  // namespace

void registerConflictNatives(GameRuntime& runtime) {
    auto& e = runtime.scripts().engine();
    auto& si = StringInterner::global();
    GameRuntime* rt = &runtime;

    const uint32_t symUnknown = si.intern("unknown");
    const uint32_t symExplored = si.intern("explored");
    const uint32_t symVisible = si.intern("visible");
    const uint32_t keySpeed = si.intern("speed");
    const uint32_t keyDamage = si.intern("damage");
    const uint32_t keyOwner = si.intern("owner");
    const uint32_t keyFaction = si.intern("faction");
    const uint32_t keyLob = si.intern("lob");
    const uint32_t keyX = si.intern("x");
    const uint32_t keyY = si.intern("y");
    const uint32_t keyZ = si.intern("z");
    const uint32_t keyEntity = si.intern("entity");

    e.registerFunction("vision_add_faction", [=](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) rt->vision().addFaction(static_cast<uint16_t>(number(args[0])));
        return Value::nil();
    });

    // vision_source faction x y z radius -> handle
    e.registerFunction("vision_source", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 5) return Value::nil();
        int64_t handle = rt->vision().addSource(
            static_cast<uint16_t>(number(args[0])),
            glm::dvec3(number(args[1]), number(args[2]), number(args[3])),
            static_cast<float>(number(args[4])));
        return Value::integer(handle);
    });

    e.registerFunction("vision_remove_source", [=](ExecutionContext&, const Args& args) -> Value {
        if (!args.empty()) rt->vision().removeSource(static_cast<int64_t>(number(args[0])));
        return Value::nil();
    });

    // vision_state faction x z -> :unknown | :explored | :visible
    e.registerFunction("vision_state", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 3) return Value::symbol(symUnknown);
        VisionState state = rt->vision().stateAt(static_cast<uint16_t>(number(args[0])),
                                                 static_cast<int32_t>(std::floor(number(args[1]))),
                                                 static_cast<int32_t>(std::floor(number(args[2]))));
        switch (state) {
            case VisionState::Visible: return Value::symbol(symVisible);
            case VisionState::Explored: return Value::symbol(symExplored);
            default: return Value::symbol(symUnknown);
        }
    });

    e.registerFunction("vision_can_see", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 3) return Value::boolean(false);
        return Value::boolean(rt->vision().isVisible(
            static_cast<uint16_t>(number(args[0])), static_cast<int32_t>(std::floor(number(args[1]))),
            static_cast<int32_t>(std::floor(number(args[2])))));
    });

    // vision_can_see_ent faction id -> bool. What a fair AI may use.
    e.registerFunction("vision_can_see_ent", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 2) return Value::boolean(false);
        MobEntity* mob = rt->session().entities().getMob(static_cast<EntityId>(number(args[1])));
        if (!mob) return Value::boolean(false);
        uint16_t faction = static_cast<uint16_t>(number(args[0]));
        if (mob->faction() == faction) return Value::boolean(true);
        Vec3 pos = mob->position();
        return Value::boolean(rt->vision().isVisible(faction, static_cast<int32_t>(std::floor(pos.x)),
                                                     static_cast<int32_t>(std::floor(pos.z))));
    });

    e.registerFunction("vision_explored_cells", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.empty()) return Value::integer(0);
        return Value::integer(static_cast<int64_t>(rt->vision().countCells(
            static_cast<uint16_t>(number(args[0])), VisionState::Explored)));
    });

    // line_of_sight x y z tx ty tz -> bool
    e.registerFunction("line_of_sight", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 6) return Value::boolean(false);
        return Value::boolean(rt->vision().hasLineOfSight(
            glm::dvec3(number(args[0]), number(args[1]), number(args[2])),
            glm::dvec3(number(args[3]), number(args[4]), number(args[5]))));
    });

    e.registerFunction("reveal_all", [=](ExecutionContext&, const Args& args) -> Value {
        rt->replica().setRevealAll(args.empty() ? true : args[0].truthy());
        return Value::nil();
    });

    // projectile_fire x y z tx ty tz [=speed s] [=damage d] [=owner id] [=faction f] [=lob b]
    // -> true if the target is within range at that speed
    e.registerFunction("projectile_fire", [=](ExecutionContext&, const Args& args) -> Value {
        if (args.size() < 6) return Value::boolean(false);
        glm::dvec3 from(number(args[0]), number(args[1]), number(args[2]));
        glm::dvec3 to(number(args[3]), number(args[4]), number(args[5]));

        float speed = static_cast<float>(number(option(args, 6, keySpeed), 24.0));
        float gravity = 14.0f;
        auto velocity = solveBallistic(from, to, speed, gravity, option(args, 6, keyLob).truthy());
        if (!velocity) return Value::boolean(false);

        ProjectileSpec spec;
        spec.origin = from;
        spec.velocity = *velocity;
        spec.gravity = gravity;
        spec.damage = static_cast<float>(number(option(args, 6, keyDamage), 1.0));
        spec.owner = static_cast<EntityId>(number(option(args, 6, keyOwner), 0.0));
        spec.faction = static_cast<uint16_t>(number(option(args, 6, keyFaction), 0.0));
        rt->projectiles().fire(spec);
        return Value::boolean(true);
    });

    // projectile_hits -> [{=x =y =z =entity =owner =faction =damage} ...] since last call
    e.registerFunction("projectile_hits", [=](ExecutionContext&, const Args&) -> Value {
        std::vector<Value> result;
        for (const ProjectileHit& hit : rt->projectiles().drainHits()) {
            auto entry = Value::map();
            auto& map = entry.asMap();
            map.set(keyX, Value::number(hit.position.x));
            map.set(keyY, Value::number(hit.position.y));
            map.set(keyZ, Value::number(hit.position.z));
            map.set(keyEntity, Value::integer(static_cast<int64_t>(hit.entity)));
            map.set(keyOwner, Value::integer(static_cast<int64_t>(hit.owner)));
            map.set(keyFaction, Value::integer(hit.faction));
            map.set(keyDamage, Value::number(hit.damage));
            result.push_back(std::move(entry));
        }
        return Value::array(std::move(result));
    });
}

}  // namespace finevox::game
