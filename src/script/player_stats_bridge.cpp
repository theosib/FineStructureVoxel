#include "finevox/script/player_stats_bridge.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/value.h>
#include <finescript/execution_context.h>

namespace finevox::script {

namespace {

/// Helper to get the local player MobEntity from EntityManager
MobEntity* getLocalPlayer(EntityManager& em) {
    EntityId playerId = em.localPlayerId();
    if (playerId == INVALID_ENTITY_ID) return nullptr;
    return em.getMob(playerId);
}

}  // anonymous namespace

void PlayerStatsBridge::registerNativeFunctions(finescript::ScriptEngine& engine) {

    // player_health() → float
    engine.registerFunction("player_health",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player) return finescript::Value::number(0.0);
            return finescript::Value::number(player->health());
        });

    // player_max_health() → float
    engine.registerFunction("player_max_health",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player) return finescript::Value::number(0.0);
            return finescript::Value::number(player->maxHealth());
        });

    // player_get_stat(name) → float (reads from DataContainer)
    engine.registerFunction("player_get_stat",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player || args.empty()) return finescript::Value::number(0.0);

            std::string key;
            if (args[0].isString()) key = args[0].asString();
            else if (args[0].isSymbol()) key = StringInterner::global().lookup(args[0].asSymbol());
            else return finescript::Value::number(0.0);

            auto* extra = player->entityData();
            if (!extra) return finescript::Value::number(0.0);

            // Try float first, then int
            float fval = extra->get<float>(key, 0.0f);
            if (fval != 0.0f) return finescript::Value::number(fval);
            int64_t ival = extra->get<int64_t>(key, 0);
            if (ival != 0) return finescript::Value::number(static_cast<double>(ival));
            return finescript::Value::number(0.0);
        });

    // player_set_stat(name, value) → nil (writes to DataContainer)
    engine.registerFunction("player_set_stat",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player || args.size() < 2) return finescript::Value::nil();

            std::string key;
            if (args[0].isString()) key = args[0].asString();
            else if (args[0].isSymbol()) key = StringInterner::global().lookup(args[0].asSymbol());
            else return finescript::Value::nil();

            auto& extra = player->getOrCreateEntityData();
            if (args[1].isFloat())
                extra.set<float>(key, static_cast<float>(args[1].asFloat()));
            else if (args[1].isInt())
                extra.set<int64_t>(key, args[1].asInt());
            return finescript::Value::nil();
        });

    // player_is_alive() → bool
    engine.registerFunction("player_is_alive",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player) return finescript::Value::boolean(false);
            return finescript::Value::boolean(!player->isDead());
        });

    // player_position() → [x, y, z] or nil
    engine.registerFunction("player_position",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            auto* player = getLocalPlayer(entityManager_);
            if (!player) return finescript::Value::nil();
            Vec3 pos = player->position();
            std::vector<finescript::Value> arr;
            arr.push_back(finescript::Value::number(pos.x));
            arr.push_back(finescript::Value::number(pos.y));
            arr.push_back(finescript::Value::number(pos.z));
            return finescript::Value::array(std::move(arr));
        });
}

}  // namespace finevox::script
