#include "finevox/script/entity_context_proxy.hpp"
#include "finevox/core/mob_entity.hpp"

namespace finevox::script {

// Helper: extract float from Value that might be int or float
static float toFloat(const finescript::Value& v) {
    if (v.isFloat()) return static_cast<float>(v.asFloat());
    if (v.isInt()) return static_cast<float>(v.asInt());
    return 0.0f;
}

const EntityContextFields& EntityContextFields::instance() {
    static EntityContextFields fields = [] {
        auto& si = StringInterner::global();
        return EntityContextFields{
            si.intern("x"),
            si.intern("y"),
            si.intern("z"),
            si.intern("vx"),
            si.intern("vy"),
            si.intern("vz"),
            si.intern("health"),
            si.intern("max_health"),
            si.intern("yaw"),
            si.intern("pitch"),
            si.intern("type"),
            si.intern("id"),
            si.intern("on_ground"),
            si.intern("speed"),
        };
    }();
    return fields;
}

EntityContextProxy::EntityContextProxy(MobEntity& mob)
    : mob_(mob) {}

finescript::Value EntityContextProxy::get(uint32_t key) const {
    const auto& f = EntityContextFields::instance();

    if (key == f.x) return finescript::Value::number(mob_.position().x);
    if (key == f.y) return finescript::Value::number(mob_.position().y);
    if (key == f.z) return finescript::Value::number(mob_.position().z);
    if (key == f.vx) return finescript::Value::number(mob_.velocity().x);
    if (key == f.vy) return finescript::Value::number(mob_.velocity().y);
    if (key == f.vz) return finescript::Value::number(mob_.velocity().z);
    if (key == f.health) return finescript::Value::number(mob_.health());
    if (key == f.max_health) return finescript::Value::number(mob_.maxHealth());
    if (key == f.yaw) return finescript::Value::number(mob_.yaw());
    if (key == f.pitch) return finescript::Value::number(mob_.pitch());
    if (key == f.type) return finescript::Value::symbol(mob_.typeId().id);
    if (key == f.id) return finescript::Value::integer(static_cast<int64_t>(mob_.id()));
    if (key == f.on_ground) return finescript::Value::boolean(mob_.isOnGround());
    if (key == f.speed) return finescript::Value::number(mob_.speedMultiplier());

    return finescript::Value::nil();
}

void EntityContextProxy::set(uint32_t key, finescript::Value value) {
    const auto& f = EntityContextFields::instance();

    if (key == f.health) {
        mob_.setHealth(toFloat(value));
    } else if (key == f.max_health) {
        mob_.setMaxHealth(toFloat(value));
    } else if (key == f.speed) {
        mob_.setSpeedMultiplier(toFloat(value));
    }
}

bool EntityContextProxy::has(uint32_t key) const {
    const auto& f = EntityContextFields::instance();
    return key == f.x || key == f.y || key == f.z ||
           key == f.vx || key == f.vy || key == f.vz ||
           key == f.health || key == f.max_health ||
           key == f.yaw || key == f.pitch ||
           key == f.type || key == f.id ||
           key == f.on_ground || key == f.speed;
}

bool EntityContextProxy::remove(uint32_t /*key*/) {
    return false;  // Entity fields cannot be removed
}

std::vector<uint32_t> EntityContextProxy::keys() const {
    const auto& f = EntityContextFields::instance();
    return {
        f.x, f.y, f.z, f.vx, f.vy, f.vz,
        f.health, f.max_health, f.yaw, f.pitch,
        f.type, f.id, f.on_ground, f.speed
    };
}

std::shared_ptr<EntityContextProxy> makeEntityProxy(MobEntity& mob) {
    return std::make_shared<EntityContextProxy>(mob);
}

}  // namespace finevox::script
