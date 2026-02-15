#pragma once

/**
 * @file entity_state.hpp
 * @brief Unified POD struct for entity state snapshots
 *
 * Used for:
 * - Game thread → graphics thread communication (entity snapshots)
 * - Graphics thread → game thread communication (player state updates)
 * - Future network serialization (entity state packets)
 *
 * Uses double-precision position/velocity to avoid float precision
 * issues at large world coordinates.
 */

#include <glm/glm.hpp>
#include <cstdint>

namespace finevox {

/// Unique entity identifier
using EntityId = uint64_t;

/// Invalid entity ID constant
constexpr EntityId INVALID_ENTITY_ID = 0;

// Forward declaration
class Entity;

struct EntityState {
    EntityId id = INVALID_ENTITY_ID;
    uint16_t entityType = 0;           // EntityType as uint16_t for POD/serialization

    // Position/motion — doubles for precision at large world coordinates
    glm::dvec3 position{0.0};
    glm::dvec3 velocity{0.0};
    bool onGround = false;

    // Look direction
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Animation
    uint8_t animationId = 0;
    float animationTime = 0.0f;

    // Client prediction
    uint64_t inputSequence = 0;

    // Factory from Entity (converts Entity's float position to double)
    static EntityState fromEntity(const Entity& entity);
};

}  // namespace finevox
