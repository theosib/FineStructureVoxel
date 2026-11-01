#pragma once

/**
 * @file entity_render_state.hpp
 * @brief Per-entity render state for graphics-thread interpolation
 *
 * Stores previous + current snapshots for smooth position interpolation,
 * an AnimationController for skeletal animation, and visibility flags.
 */

#include "finevox/core/entity_state.hpp"
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/animation_controller.hpp"
#include <glm/glm.hpp>

namespace finevox {

struct EntityRenderState {
    EntityId id = INVALID_ENTITY_ID;
    EntityTypeId typeId;

    // Position interpolation (double precision for large worlds)
    glm::dvec3 prevPosition{0.0};
    glm::dvec3 currentPosition{0.0};
    float prevYaw = 0.0f;
    float currentYaw = 0.0f;
    float prevPitch = 0.0f;
    float currentPitch = 0.0f;
    glm::dvec3 velocity{0.0};
    uint16_t faction = 0;

    /// Seconds since the last snapshot; drives interpolation between ticks
    float timeSinceSnapshot = 0.0f;

    // Animation
    AnimationController animator;
    uint8_t currentAnimationId = 0;

    // Visibility
    bool visible = true;
    bool active = true;

    /// How far through dying, 0 to 1, with no snapshots to come. Negative while alive.
    float dying = -1.0f;
    bool deathBurstDone = false;

    [[nodiscard]] bool isDying() const { return dying >= 0.0f; }

    /// Alive and in view. The server stops sending what the viewer can't see, so silence
    /// means hidden.
    [[nodiscard]] bool isCurrent() const {
        constexpr float STALE_SECONDS = 0.5f;
        return visible && active && !isDying() && timeSinceSnapshot < STALE_SECONDS;
    }

    /// Interpolate position between prev and current
    [[nodiscard]] glm::dvec3 interpolatedPosition(float alpha) const;

    /// Interpolate yaw (handles 360-degree wrapping)
    [[nodiscard]] float interpolatedYaw(float alpha) const;

    /// Interpolate pitch
    [[nodiscard]] float interpolatedPitch(float alpha) const;

    /// Blend factor between the last two snapshots at the given tick rate
    [[nodiscard]] float interpolationAlpha(float tickRate) const;

    /// Interpolated yaw in radians, the unit MobEntity stores (wraps at pi)
    [[nodiscard]] float interpolatedYawRadians(float alpha) const;

    /// Update from a new entity state snapshot
    void applySnapshot(const EntityState& state);
};

}  // namespace finevox
