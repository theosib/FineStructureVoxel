#pragma once

/**
 * @file entity_state.hpp
 * @brief Unified struct for entity state snapshots
 *
 * Used for:
 * - Game thread → graphics thread communication (entity snapshots)
 * - Graphics thread → game thread communication (player state updates)
 * - Network serialization (entity state packets)
 *
 * Uses double-precision position/velocity to avoid float precision
 * issues at large world coordinates.
 */

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace finevox {

/// Unique entity identifier
using EntityId = uint64_t;

/// Invalid entity ID constant
constexpr EntityId INVALID_ENTITY_ID = 0;

// Forward declarations
class Entity;
class DataContainer;

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

    // Extensible mod/script data (nullptr by default — zero overhead)
    std::unique_ptr<DataContainer> extra;

    // Default constructor
    EntityState();
    ~EntityState();

    // Copy (deep-clones extra DataContainer)
    EntityState(const EntityState& other);
    EntityState& operator=(const EntityState& other);

    // Move
    EntityState(EntityState&&) noexcept;
    EntityState& operator=(EntityState&&) noexcept;

    // Factory from Entity
    static EntityState fromEntity(const Entity& entity);

    // CBOR serialization
    [[nodiscard]] std::vector<uint8_t> toCBOR() const;
    static EntityState fromCBOR(std::span<const uint8_t> data);
};

}  // namespace finevox
