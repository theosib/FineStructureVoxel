#pragma once

/**
 * @file particle.hpp
 * @brief Runtime Particle and ParticleEmitter — POD-ish per-instance state.
 *
 * Kept separate from ParticleTypeDef/EmitterPresetDef so the immutable
 * registry-loaded config can be shared across many instances.
 */

#include "finevox/core/entity_state.hpp"        // EntityId
#include "finevox/core/particle_type.hpp"
#include <glm/glm.hpp>

#include <cstdint>

namespace finevox {

// ============================================================================
// Particle flag bits
// ============================================================================

namespace ParticleFlag {
    constexpr uint8_t None            = 0;
    constexpr uint8_t Collides        = 1 << 0;  ///< test voxel at next step
    constexpr uint8_t KillOnCollision = 1 << 1;  ///< die on contact (vs. reflect)
    constexpr uint8_t AffectedByGravity = 1 << 2;
}

// ============================================================================
// Particle — runtime per-instance state
// ============================================================================

/**
 * Per-particle state. Position is double-precision for parity with entities
 * (particles may render at any world coordinate without float jitter).
 * Velocity is single-precision because particle-scale errors are imperceptible.
 */
struct Particle {
    glm::dvec3 position{0.0};
    glm::vec3 velocity{0.0f};

    float age = 0.0f;              ///< seconds since spawn
    float maxAge = 1.0f;           ///< seconds before death

    float sizeStart = 0.1f;
    float sizeEnd = 0.1f;

    glm::vec4 startTint{1.0f};
    glm::vec4 endTint{1.0f};

    glm::vec4 atlasUV{0.0f, 0.0f, 1.0f, 1.0f}; ///< (minU, minV, maxU, maxV)

    float gravity = 0.0f;
    float drag = 0.0f;
    float restitution = 0.3f;

    uint32_t typeIdRaw = 0;        ///< ParticleTypeId.id (so POD vector works)
    uint8_t flags = 0;             ///< ParticleFlag bits
    BillboardMode billboard = BillboardMode::CameraFacing;

    [[nodiscard]] ParticleTypeId type() const { return ParticleTypeId{typeIdRaw}; }

    [[nodiscard]] float lifeFraction() const {
        return maxAge > 0.0f ? (age / maxAge) : 1.0f;
    }

    /// Current interpolated size
    [[nodiscard]] float currentSize() const {
        float t = lifeFraction();
        return sizeStart + (sizeEnd - sizeStart) * t;
    }

    /// Current interpolated tint
    [[nodiscard]] glm::vec4 currentTint() const {
        float t = lifeFraction();
        return startTint + (endTint - startTint) * t;
    }

    [[nodiscard]] bool isDead() const { return age >= maxAge; }
};

// ============================================================================
// ParticleEmitter — runtime per-instance emitter
// ============================================================================

using EmitterId = uint32_t;
constexpr EmitterId INVALID_EMITTER_ID = 0;

struct ParticleEmitter {
    EmitterId id = INVALID_EMITTER_ID;
    EmitterPresetId presetId;

    glm::dvec3 position{0.0};
    EntityId attachedEntity = INVALID_ENTITY_ID;   ///< follows this entity if set

    float age = 0.0f;
    float duration = -1.0f;        ///< -1 = continuous
    float spawnAccumulator = 0.0f; ///< fractional spawn count for continuous emitters
    bool bursted = false;          ///< initial burst fired?
    bool active = true;

    [[nodiscard]] bool isExpired() const {
        return duration >= 0.0f && age >= duration;
    }
};

} // namespace finevox
