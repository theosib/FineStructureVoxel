#pragma once

/**
 * @file entity_wire.hpp
 * @brief Entities and projectiles on the wire (§6), written and read in one place
 *
 * Each record says how long it is, so a field can be added at the end of one without breaking
 * anyone: an older reader skips what it doesn't know, and a newer one reading an older sender gets
 * the field's default. See §10.
 */

#include "finevox/net/protocol.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace finevox::net {

struct EntitySpawnWire {
    uint64_t id = 0;
    uint32_t type = 0;
    uint16_t faction = 0;
    glm::dvec3 position{0.0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint8_t health = FULL_HEALTH;  ///< Since revision 0 of protocol 2
};

struct EntityMoveWire {
    uint64_t id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint8_t animation = 0;
    bool onGround = false;
    uint8_t health = FULL_HEALTH;  ///< Since revision 0 of protocol 2
};

struct EntityBatchWire {
    uint64_t tick = 0;
    std::vector<EntityMoveWire> entities;
};

struct ProjectileWire {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
};

[[nodiscard]] std::vector<uint8_t> encodeEntitySpawn(const EntitySpawnWire& spawn);
[[nodiscard]] std::optional<EntitySpawnWire> decodeEntitySpawn(std::span<const uint8_t> body);

[[nodiscard]] std::vector<uint8_t> encodeEntityBatch(const EntityBatchWire& batch);
/// nullopt if it's damaged. A record that's short of the oldest fields is dropped, not the batch.
[[nodiscard]] std::optional<EntityBatchWire> decodeEntityBatch(std::span<const uint8_t> body);

[[nodiscard]] std::vector<uint8_t> encodeProjectiles(const std::vector<ProjectileWire>& shots);
[[nodiscard]] std::optional<std::vector<ProjectileWire>> decodeProjectiles(std::span<const uint8_t> body);

}  // namespace finevox::net
