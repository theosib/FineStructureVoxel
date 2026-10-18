#include "finevox/script/player_stats_bridge.hpp"
#include "finevox/script/data_container_proxy.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/string_interner.hpp"

#include <finescript/value.h>
#include <finescript/execution_context.h>

namespace finevox::script {

void PlayerStatsBridge::registerNativeFunctions(finescript::ScriptEngine& engine) {

    // player_health() → float
    engine.registerFunction("player_health",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return entityManager_.withLocalPlayerMob([](MobEntity* player) -> finescript::Value {
                if (!player) return finescript::Value::number(0.0);
                return finescript::Value::number(player->health());
            });
        });

    // player_max_health() → float
    engine.registerFunction("player_max_health",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return entityManager_.withLocalPlayerMob([](MobEntity* player) -> finescript::Value {
                if (!player) return finescript::Value::number(0.0);
                return finescript::Value::number(player->maxHealth());
            });
        });

    // player_get_stat(name, [default]) → value (reads from DataContainer)
    engine.registerFunction("player_get_stat",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            finescript::Value defVal = args.size() >= 2 ? args[1] : finescript::Value::number(0.0);
            if (args.empty()) return defVal;

            uint32_t keyId;
            if (args[0].isSymbol()) keyId = args[0].asSymbol();
            else if (args[0].isString()) keyId = StringInterner::global().intern(args[0].asString());
            else return defVal;

            return entityManager_.withLocalPlayerMob([&](MobEntity* player) -> finescript::Value {
                if (!player) return defVal;
                const auto* extra = player->entityData();
                if (!extra || !extra->has(keyId)) return defVal;

                DataContainerProxy proxy(const_cast<DataContainer&>(*extra));
                return proxy.get(keyId);
            });
        });

    // player_set_stat(name, value) → nil (writes to DataContainer)
    engine.registerFunction("player_set_stat",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>& args)
            -> finescript::Value
        {
            if (args.size() < 2) return finescript::Value::nil();

            uint32_t keyId;
            if (args[0].isSymbol()) keyId = args[0].asSymbol();
            else if (args[0].isString()) keyId = StringInterner::global().intern(args[0].asString());
            else return finescript::Value::nil();

            return entityManager_.withLocalPlayerMob([&](MobEntity* player) -> finescript::Value {
                if (!player) return finescript::Value::nil();
                DataContainerProxy proxy(player->getOrCreateEntityData());
                proxy.set(keyId, args[1]);
                return finescript::Value::nil();
            });
        });

    // player_is_alive() → bool
    engine.registerFunction("player_is_alive",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return entityManager_.withLocalPlayerMob([](MobEntity* player) -> finescript::Value {
                if (!player) return finescript::Value::boolean(false);
                return finescript::Value::boolean(!player->isDead());
            });
        });

    // player_position() → [x, y, z] or nil
    engine.registerFunction("player_position",
        [this](finescript::ExecutionContext&, const std::vector<finescript::Value>&)
            -> finescript::Value
        {
            return entityManager_.withLocalPlayerMob([](MobEntity* player) -> finescript::Value {
                if (!player) return finescript::Value::nil();
                Vec3 pos = player->position();
                std::vector<finescript::Value> arr;
                arr.push_back(finescript::Value::number(pos.x));
                arr.push_back(finescript::Value::number(pos.y));
                arr.push_back(finescript::Value::number(pos.z));
                return finescript::Value::array(std::move(arr));
            });
        });
}

}  // namespace finevox::script
