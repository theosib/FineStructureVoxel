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

    // Animation
    AnimationController animator;

    // Visibility
    bool visible = true;
    bool active = true;

    /// Interpolate position between prev and current
    [[nodiscard]] glm::dvec3 interpolatedPosition(float alpha) const;

    /// Interpolate yaw (handles 360-degree wrapping)
    [[nodiscard]] float interpolatedYaw(float alpha) const;

    /// Interpolate pitch
    [[nodiscard]] float interpolatedPitch(float alpha) const;

    /// Update from a new entity state snapshot
    void applySnapshot(const EntityState& state);
};

}  // namespace finevox
