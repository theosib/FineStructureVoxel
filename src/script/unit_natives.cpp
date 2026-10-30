/**
 * @file unit_natives.cpp
 * @brief Script natives addressed by entity id, for game-wide logic
 *
 * The `mob_*` family acts on the mob running the script. Central logic such as a
 * job system holds no such mob, so `ent_*` takes the id first. Anything that
 * would otherwise be a loop over cells or entities in script is done here.
 */

#include "finevox/script/game_script_engine.hpp"
#include "finevox/script/script_game_subsystem.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/navigation.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/work_claims.hpp"
#include "finevox/core/world.hpp"

#include <finescript/map_data.h>
#include <finescript/script_engine.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <unordered_map>

namespace finevox::script {

namespace {

using finescript::ExecutionContext;
using finescript::Value;
using Args = std::vector<Value>;

ScriptUserData* scriptData(ExecutionContext& ctx) {
    return static_cast<ScriptUserData*>(ctx.userData());
}

Value emptyArray() {
    return Value::array(std::vector<Value>{});
}

double number(const Value& v, double fallback = 0.0) {
    return v.isNumeric() ? v.asNumber() : fallback;
}

EntityId entityArg(const Args& args, size_t index = 0) {
    if (index >= args.size() || !args[index].isNumeric()) return INVALID_ENTITY_ID;
    return static_cast<EntityId>(args[index].asNumber());
}

MobEntity* mobArg(ExecutionContext& ctx, const Args& args, size_t index = 0) {
    auto* ud = scriptData(ctx);
    if (!ud || !ud->entityManager) return nullptr;
    EntityId id = entityArg(args, index);
    return id == INVALID_ENTITY_ID ? nullptr : ud->entityManager->getMob(id);
}

Value vec3Value(double x, double y, double z) {
    return Value::array(std::vector<Value>{Value::number(x), Value::number(y), Value::number(z)});
}

Value coordValue(const BlockCoord& p) {
    return Value::array(
        std::vector<Value>{Value::integer(p.x), Value::integer(p.y), Value::integer(p.z)});
}

/// Trailing `{=key val}` map, as natives receive named arguments
Value optionsArg(const Args& args, size_t firstPositionalAfter) {
    if (args.size() > firstPositionalAfter && args.back().isMap()) return args.back();
    return Value::nil();
}

Value option(const Value& options, uint32_t key) {
    return options.isMap() ? options.asMap().get(key) : Value::nil();
}

/// x y z at args[index..], or an [x y z] array at args[index]
bool positionArg(const Args& args, size_t index, double& x, double& y, double& z) {
    if (index < args.size() && args[index].isArray()) {
        const auto& arr = args[index].asArray();
        if (arr.size() < 3) return false;
        x = number(arr[0]);
        y = number(arr[1]);
        z = number(arr[2]);
        return true;
    }
    if (index + 2 >= args.size()) return false;
    x = number(args[index]);
    y = number(args[index + 1]);
    z = number(args[index + 2]);
    return true;
}

bool coordArg(const Args& args, size_t index, BlockCoord& out) {
    double x, y, z;
    if (!positionArg(args, index, x, y, z)) return false;
    out = BlockCoord(static_cast<int32_t>(std::floor(x)), static_cast<int32_t>(std::floor(y)),
                     static_cast<int32_t>(std::floor(z)));
    return true;
}

uint32_t blockSymbol(const Value& v) {
    if (v.isSymbol()) return v.asSymbol();
    if (v.isString()) return StringInterner::global().intern(v.asString());
    return 0;
}

const char* navStateName(NavState state) {
    switch (state) {
        case NavState::Idle: return "idle";
        case NavState::Pending: return "pending";
        case NavState::Moving: return "moving";
        case NavState::Blocked: return "blocked";
        case NavState::Arrived: return "arrived";
        case NavState::Failed: return "failed";
    }
    return "idle";
}

}  // namespace

void GameScriptEngine::registerUnitNativeFunctions() {
    auto& e = *engine_;
    auto& si = StringInterner::global();

    const uint32_t keyFaction = si.intern("faction");
    const uint32_t keyEnemyOf = si.intern("enemy_of");
    const uint32_t keyType = si.intern("type");
    const uint32_t keyMax = si.intern("max");
    const uint32_t keyRange = si.intern("range");
    const uint32_t keyPlace = si.intern("place");
    const uint32_t keyDig = si.intern("dig");
    const uint32_t keyMaxNodes = si.intern("max_nodes");
    const uint32_t keyKind = si.intern("kind");
    const uint32_t keyPos = si.intern("pos");
    const uint32_t keyStand = si.intern("stand");
    const uint32_t symPlace = si.intern("place");
    const uint32_t symDig = si.intern("dig");

    // ------------------------------------------------------------------------
    // Lifecycle and queries
    // ------------------------------------------------------------------------

    // ent_spawn type x y z [=faction n] -> id
    e.registerFunction("ent_spawn", [=](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->entityManager || args.size() < 2) return Value::nil();

        uint32_t typeSym = blockSymbol(args[0]);
        if (typeSym == 0) return Value::nil();
        double x, y, z;
        if (!positionArg(args, 1, x, y, z)) return Value::nil();

        auto mob = std::make_unique<MobEntity>(INVALID_ENTITY_ID, EntityTypeId(typeSym));
        mob->setPosition(Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        Value faction = option(optionsArg(args, 2), keyFaction);
        if (faction.isNumeric()) mob->setFaction(static_cast<uint16_t>(faction.asNumber()));
        EntityId id = ud->entityManager->spawnEntity(std::move(mob));
        return Value::integer(static_cast<int64_t>(id));
    });

    e.registerFunction("ent_remove", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->entityManager) return Value::boolean(false);
        if (ud->navigation) ud->navigation->stop(entityArg(args));
        if (ud->workClaims) ud->workClaims->releaseAll(entityArg(args));
        return Value::boolean(ud->entityManager->despawnEntity(entityArg(args)));
    });

    e.registerFunction("ent_alive", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return Value::boolean(mob && mob->isAlive() && !mob->isDead());
    });

    e.registerFunction("ent_type", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::symbol(mob->typeId().id) : Value::nil();
    });

    e.registerFunction("ent_pos", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (!mob) return Value::nil();
        Vec3 p = mob->position();
        return vec3Value(p.x, p.y, p.z);
    });

    e.registerFunction("ent_set_pos", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        double x, y, z;
        if (!mob || !positionArg(args, 1, x, y, z)) return Value::nil();
        mob->setPosition(Vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)));
        return Value::nil();
    });

    e.registerFunction("ent_faction", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::integer(mob->faction()) : Value::nil();
    });

    e.registerFunction("ent_set_faction", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (mob && args.size() > 1) mob->setFaction(static_cast<uint16_t>(number(args[1])));
        return Value::nil();
    });

    e.registerFunction("ent_health", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::number(mob->health()) : Value::nil();
    });

    e.registerFunction("ent_max_health", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::number(mob->maxHealth()) : Value::nil();
    });

    e.registerFunction("ent_set_health", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (mob && args.size() > 1) mob->setHealth(static_cast<float>(number(args[1])));
        return Value::nil();
    });

    // ent_damage id amount [source]
    e.registerFunction("ent_damage", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (!mob || args.size() < 2) return Value::boolean(false);
        EntityId source = args.size() > 2 ? entityArg(args, 2) : INVALID_ENTITY_ID;
        mob->damage(static_cast<float>(number(args[1])), source);
        return Value::boolean(mob->isDead());
    });

    e.registerFunction("ent_last_attacker", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::integer(static_cast<int64_t>(mob->lastAttacker())) : Value::nil();
    });

    e.registerFunction("ent_since_damage", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        return mob ? Value::number(mob->timeSinceLastDamage()) : Value::nil();
    });

    e.registerFunction("ent_speed", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (mob && args.size() > 1) mob->setSpeedMultiplier(static_cast<float>(number(args[1], 1.0)));
        return Value::nil();
    });

    e.registerFunction("ent_anim", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        if (!mob || args.size() < 2) return Value::nil();
        if (args[1].isSymbol()) {
            mob->playAnimation(StringInterner::global().lookup(args[1].asSymbol()));
        } else if (args[1].isString()) {
            mob->playAnimation(args[1].asString());
        }
        return Value::nil();
    });

    e.registerFunction("ent_look_at", [](ExecutionContext& ctx, const Args& args) -> Value {
        MobEntity* mob = mobArg(ctx, args);
        double x, y, z;
        if (mob && positionArg(args, 1, x, y, z)) mob->lookAt(glm::dvec3(x, y, z));
        return Value::nil();
    });

    // ent_find x y z radius [=faction f] [=enemy_of f] [=type t] [=max n] -> ids, nearest first
    e.registerFunction("ent_find", [=](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        double x, y, z;
        if (!ud || !ud->entityManager || !positionArg(args, 0, x, y, z)) {
            return emptyArray();
        }
        size_t radiusIndex = args[0].isArray() ? 1 : 3;
        if (radiusIndex >= args.size()) return emptyArray();
        float radius = static_cast<float>(number(args[radiusIndex]));

        Value options = optionsArg(args, radiusIndex + 1);
        Value faction = option(options, keyFaction);
        Value enemyOf = option(options, keyEnemyOf);
        Value type = option(options, keyType);
        Value maxResults = option(options, keyMax);

        int wantFaction = faction.isNumeric() ? static_cast<int>(faction.asNumber()) : -1;
        int notFaction = enemyOf.isNumeric() ? static_cast<int>(enemyOf.asNumber()) : -1;
        uint32_t wantType = blockSymbol(type);

        Vec3 center(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        std::vector<EntityId> near = ud->entityManager->spatialIndex().queryRadius(center, radius);

        std::vector<std::pair<float, EntityId>> found;
        found.reserve(near.size());
        for (EntityId id : near) {
            MobEntity* mob = ud->entityManager->getMob(id);
            if (!mob || !mob->isAlive() || mob->isDead()) continue;
            if (wantFaction >= 0 && mob->faction() != wantFaction) continue;
            if (notFaction >= 0 && (mob->faction() == notFaction || mob->faction() == 0)) continue;
            if (wantType != 0 && mob->typeId().id != wantType) continue;
            Vec3 d = mob->position() - center;
            found.emplace_back(d.x * d.x + d.y * d.y + d.z * d.z, id);
        }
        std::sort(found.begin(), found.end());

        size_t limit = maxResults.isNumeric() ? static_cast<size_t>(maxResults.asNumber())
                                              : found.size();
        std::vector<Value> result;
        result.reserve(std::min(limit, found.size()));
        for (size_t i = 0; i < found.size() && i < limit; ++i) {
            result.push_back(Value::integer(static_cast<int64_t>(found[i].second)));
        }
        return Value::array(std::move(result));
    });

    // ------------------------------------------------------------------------
    // Navigation
    // ------------------------------------------------------------------------

    // ent_goto id x y z [=range r] [=place true] [=dig true] [=max_nodes n] -> queued?
    e.registerFunction("ent_goto", [=](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->navigation || !mobArg(ctx, args)) return Value::boolean(false);
        double x, y, z;
        if (!positionArg(args, 1, x, y, z)) return Value::boolean(false);

        NavOrder order;
        order.goal = glm::dvec3(x, y, z);
        Value options = optionsArg(args, args[1].isArray() ? 2 : 4);
        Value range = option(options, keyRange);
        Value nodes = option(options, keyMaxNodes);
        if (range.isNumeric()) order.goalRange = static_cast<float>(range.asNumber());
        if (nodes.isNumeric()) order.maxNodes = static_cast<int>(nodes.asNumber());
        order.allowPlace = option(options, keyPlace).truthy();
        order.allowDig = option(options, keyDig).truthy();

        ud->navigation->order(entityArg(args), order);
        return Value::boolean(true);
    });

    e.registerFunction("ent_nav", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->navigation) return Value::symbol(StringInterner::global().intern("idle"));
        NavState state = ud->navigation->state(entityArg(args));
        return Value::symbol(StringInterner::global().intern(navStateName(state)));
    });

    // ent_nav_action id -> {=kind :place|:dig =pos [x y z] =stand [x y z]} while blocked
    e.registerFunction("ent_nav_action", [=](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->navigation) return Value::nil();
        auto step = ud->navigation->currentAction(entityArg(args));
        if (!step) return Value::nil();
        auto result = Value::map();
        auto& map = result.asMap();
        map.set(keyKind, Value::symbol(step->action == NavAction::Place ? symPlace : symDig));
        map.set(keyPos, coordValue(step->actionPos));
        map.set(keyStand, coordValue(step->pos));
        return result;
    });

    e.registerFunction("ent_nav_resume", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (ud && ud->navigation) ud->navigation->resume(entityArg(args));
        return Value::nil();
    });

    e.registerFunction("ent_stop", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (ud && ud->navigation) ud->navigation->stop(entityArg(args));
        return Value::nil();
    });

    e.registerFunction("nav_nodes_last_tick", [](ExecutionContext& ctx, const Args&) -> Value {
        auto* ud = scriptData(ctx);
        return Value::integer(ud && ud->navigation ? ud->navigation->nodesLastTick() : 0);
    });

    e.registerFunction("ent_nav_ack", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (ud && ud->navigation) ud->navigation->acknowledge(entityArg(args));
        return Value::nil();
    });

    // ------------------------------------------------------------------------
    // Work claims
    // ------------------------------------------------------------------------

    // claim_block scope x y z owner [ttl_ticks] -> got it?
    e.registerFunction("claim_block", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        BlockCoord pos;
        if (!ud || !ud->workClaims || args.size() < 3 || !coordArg(args, 1, pos)) {
            return Value::boolean(false);
        }
        size_t next = args[1].isArray() ? 2 : 4;
        if (next >= args.size()) return Value::boolean(false);
        uint32_t ttl = next + 1 < args.size() ? static_cast<uint32_t>(number(args[next + 1])) : 0;
        return Value::boolean(ud->workClaims->claim(static_cast<uint16_t>(number(args[0])), pos,
                                                    entityArg(args, next), ttl));
    });

    // release_block scope x y z owner
    e.registerFunction("release_block", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        BlockCoord pos;
        if (!ud || !ud->workClaims || args.size() < 3 || !coordArg(args, 1, pos)) {
            return Value::boolean(false);
        }
        size_t next = args[1].isArray() ? 2 : 4;
        if (next >= args.size()) return Value::boolean(false);
        return Value::boolean(ud->workClaims->release(static_cast<uint16_t>(number(args[0])), pos,
                                                      entityArg(args, next)));
    });

    e.registerFunction("release_claims", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->workClaims) return Value::integer(0);
        return Value::integer(static_cast<int64_t>(ud->workClaims->releaseAll(entityArg(args))));
    });

    // block_claimed scope x y z -> bool
    e.registerFunction("block_claimed", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        BlockCoord pos;
        if (!ud || !ud->workClaims || args.empty() || !coordArg(args, 1, pos)) {
            return Value::boolean(false);
        }
        return Value::boolean(ud->workClaims->isClaimed(static_cast<uint16_t>(number(args[0])), pos));
    });

    // ------------------------------------------------------------------------
    // World queries
    // ------------------------------------------------------------------------

    // surface_y x z -> y of the top occupied block, or nil outside the world
    e.registerFunction("surface_y", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->world || args.size() < 2) return Value::nil();
        int32_t x = static_cast<int32_t>(std::floor(number(args[0])));
        int32_t z = static_cast<int32_t>(std::floor(number(args[1])));
        // The heightmap tracks sky-blocking blocks and isn't kept current by direct
        // writes, so walk down from the highest subchunk the column actually has.
        const ChunkColumn* column = ud->world->getColumn(ColumnPos(x >> 4, z >> 4));
        if (!column) return Value::nil();
        int32_t top = std::numeric_limits<int32_t>::min();
        column->forEachSubChunk([&top](int32_t chunkY, const SubChunk&) {
            top = std::max(top, chunkY);
        });
        if (top == std::numeric_limits<int32_t>::min()) return Value::nil();
        for (int32_t y = top * 16 + 15; y > (top - 32) * 16; --y) {
            if (!ud->world->getBlock(x, y, z).isAir()) return Value::integer(y);
        }
        return Value::nil();
    });

    // is_standable x y z -> can a 2-high body stand with feet here?
    e.registerFunction("is_standable", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        BlockCoord pos;
        if (!ud || !ud->world || !coordArg(args, 0, pos)) return Value::boolean(false);
        return Value::boolean(isStandable(*ud->world, pos, 1.8f));
    });

    // find_blocks x y z radius block_or_array [=max n] -> [[x y z] ...], nearest first
    // Scans only subchunks whose palette holds a wanted type.
    e.registerFunction("find_blocks", [=](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        BlockCoord center;
        if (!ud || !ud->world || !coordArg(args, 0, center)) return emptyArray();
        size_t next = args[0].isArray() ? 1 : 3;
        if (next + 1 >= args.size()) return emptyArray();

        int32_t radius = static_cast<int32_t>(number(args[next]));
        std::vector<BlockTypeId> wanted;
        if (args[next + 1].isArray()) {
            for (const auto& v : args[next + 1].asArray()) {
                if (uint32_t sym = blockSymbol(v)) wanted.push_back(BlockTypeId(sym));
            }
        } else if (uint32_t sym = blockSymbol(args[next + 1])) {
            wanted.push_back(BlockTypeId(sym));
        }
        if (wanted.empty()) return emptyArray();

        Value maxResults = option(optionsArg(args, next + 2), keyMax);
        size_t limit = maxResults.isNumeric() ? static_cast<size_t>(maxResults.asNumber()) : 64;

        std::vector<std::pair<int64_t, BlockCoord>> found;
        int32_t minCx = (center.x - radius) >> 4, maxCx = (center.x + radius) >> 4;
        int32_t minCy = (center.y - radius) >> 4, maxCy = (center.y + radius) >> 4;
        int32_t minCz = (center.z - radius) >> 4, maxCz = (center.z + radius) >> 4;
        int64_t radiusSq = static_cast<int64_t>(radius) * radius;

        for (int32_t cx = minCx; cx <= maxCx; ++cx) {
            for (int32_t cz = minCz; cz <= maxCz; ++cz) {
                for (int32_t cy = minCy; cy <= maxCy; ++cy) {
                    const SubChunk* sub = ud->world->getSubChunk(ChunkPos(cx, cy, cz));
                    if (!sub) continue;
                    bool holdsWanted = false;
                    for (BlockTypeId type : wanted) {
                        if (sub->hasBlockType(type)) {
                            holdsWanted = true;
                            break;
                        }
                    }
                    if (!holdsWanted) continue;

                    for (int32_t lx = 0; lx < 16; ++lx) {
                        for (int32_t lz = 0; lz < 16; ++lz) {
                            for (int32_t ly = 0; ly < 16; ++ly) {
                                BlockTypeId here = sub->getBlock(lx, ly, lz);
                                if (std::find(wanted.begin(), wanted.end(), here) == wanted.end()) {
                                    continue;
                                }
                                BlockCoord pos(cx * 16 + lx, cy * 16 + ly, cz * 16 + lz);
                                int64_t dx = pos.x - center.x;
                                int64_t dy = pos.y - center.y;
                                int64_t dz = pos.z - center.z;
                                int64_t distSq = dx * dx + dy * dy + dz * dz;
                                if (distSq <= radiusSq) found.emplace_back(distSq, pos);
                            }
                        }
                    }
                }
            }
        }

        std::sort(found.begin(), found.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<Value> result;
        result.reserve(std::min(limit, found.size()));
        for (size_t i = 0; i < found.size() && i < limit; ++i) {
            result.push_back(coordValue(found[i].second));
        }
        return Value::array(std::move(result));
    });

    // count_blocks x0 y0 z0 x1 y1 z1 [block ...] -> {=name count ...}, inclusive box.
    // With no block names, counts everything that isn't air.
    e.registerFunction("count_blocks", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        auto result = Value::map();
        if (!ud || !ud->world || args.size() < 6) return result;

        BlockCoord a(static_cast<int32_t>(number(args[0])), static_cast<int32_t>(number(args[1])),
                     static_cast<int32_t>(number(args[2])));
        BlockCoord b(static_cast<int32_t>(number(args[3])), static_cast<int32_t>(number(args[4])),
                     static_cast<int32_t>(number(args[5])));

        std::vector<BlockTypeId> wanted;
        for (size_t i = 6; i < args.size(); ++i) {
            if (uint32_t sym = blockSymbol(args[i])) wanted.push_back(BlockTypeId(sym));
        }

        std::unordered_map<uint32_t, int64_t> counts;
        for (int32_t x = std::min(a.x, b.x); x <= std::max(a.x, b.x); ++x) {
            for (int32_t z = std::min(a.z, b.z); z <= std::max(a.z, b.z); ++z) {
                for (int32_t y = std::min(a.y, b.y); y <= std::max(a.y, b.y); ++y) {
                    BlockTypeId here = ud->world->getBlock(x, y, z);
                    if (here.isAir()) continue;
                    if (!wanted.empty() &&
                        std::find(wanted.begin(), wanted.end(), here) == wanted.end()) {
                        continue;
                    }
                    ++counts[here.id];
                }
            }
        }
        for (const auto& [id, count] : counts) {
            result.asMap().set(id, Value::integer(count));
        }
        return result;
    });

    // ------------------------------------------------------------------------
    // Game script services
    // ------------------------------------------------------------------------

    e.registerFunction("game_tick", [](ExecutionContext& ctx, const Args&) -> Value {
        auto* ud = scriptData(ctx);
        if (!ud || !ud->gameScript) return Value::integer(0);
        return Value::integer(static_cast<int64_t>(ud->gameScript->tickCount()));
    });

    // ui_publish value: hand a snapshot to the client (CBOR, so nothing is shared)
    e.registerFunction("ui_publish", [](ExecutionContext& ctx, const Args& args) -> Value {
        auto* ud = scriptData(ctx);
        if (ud && ud->gameScript && !args.empty()) ud->gameScript->publish(args[0]);
        return Value::nil();
    });

    // log a b c: diagnostics, distinct from script `print`
    e.registerFunction("log", [](ExecutionContext&, const Args& args) -> Value {
        std::string line;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) line += ' ';
            line += args[i].toString();
        }
        std::cout << "[game] " << line << std::endl;
        return Value::nil();
    });

    (void)keyDig;
}

}  // namespace finevox::script
