#pragma once

/**
 * @file entity_context_proxy.hpp
 * @brief ProxyMap wrapping a MobEntity for script field access
 *
 * Pre-interned field IDs for zero-overhead lookups.
 * Fields: x, y, z, vx, vy, vz, health, max_health, yaw, pitch, type, id, on_ground
 */

#include <finescript/proxy_map.h>
#include <finescript/value.h>
#include "finevox/core/string_interner.hpp"
#include <cstdint>
#include <memory>

namespace finevox {
class MobEntity;
}

namespace finevox::script {

/// Pre-interned field IDs for EntityContextProxy
struct EntityContextFields {
    uint32_t x, y, z;
    uint32_t vx, vy, vz;
    uint32_t health, max_health;
    uint32_t yaw, pitch;
    uint32_t type;
    uint32_t id;
    uint32_t on_ground;
    uint32_t speed;

    static const EntityContextFields& instance();
};

class EntityContextProxy : public finescript::ProxyMap {
public:
    explicit EntityContextProxy(MobEntity& mob);

    finescript::Value get(uint32_t key) const override;
    void set(uint32_t key, finescript::Value value) override;
    bool has(uint32_t key) const override;
    bool remove(uint32_t key) override;
    std::vector<uint32_t> keys() const override;

private:
    MobEntity& mob_;
};

/// Create a shared_ptr proxy for use in finescript::Value::proxyMap()
std::shared_ptr<EntityContextProxy> makeEntityProxy(MobEntity& mob);

}  // namespace finevox::script
