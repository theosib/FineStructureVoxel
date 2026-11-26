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
#include "finevox/core/string_interner.hpp"
#include "finevox/core/animation_controller.hpp"
#include "finevox/core/ground_smoothing.hpp"
#include <glm/glm.hpp>
#include <string>

namespace finevox {

struct EntityRenderState {
    EntityId id = INVALID_ENTITY_ID;
    EntityTypeId typeId;
    std::string item;  ///< An item lying in the world: which, drawn as its picture
    BlockTypeId heldBlock;  ///< A block it holds, drawn in its model's `held:` part; air for none

    // Position interpolation (double precision for large worlds)
    glm::dvec3 prevPosition{0.0};
    glm::dvec3 currentPosition{0.0};
    float prevYaw = 0.0f;
    float currentYaw = 0.0f;
    float prevPitch = 0.0f;
    float currentPitch = 0.0f;
    /// Radians: the way it's drawn facing. The server turns a body in a single tick, so this
    /// follows currentYaw at the pace of a body turning (turn()).
    float shownYaw = 0.0f;
    glm::dvec3 velocity{0.0};
    bool onGround = false;
    uint16_t faction = 0;

    /// How far above where the server has it it's drawn, to stand on smoothed ground
    /// (followGround). groundWeight eases between 0 in the air and 1 on the ground.
    float groundLift = 0.0f;
    float groundWeight = 0.0f;

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

    /// Alive and in view. The server says when something leaves view, and sends nothing about
    /// what hasn't moved, so silence means it's still there.
    [[nodiscard]] bool isCurrent() const {
        return visible && active && !isDying();
    }

    /// Interpolate position between prev and current
    [[nodiscard]] glm::dvec3 interpolatedPosition(float alpha) const;

    /// Interpolate yaw (handles 360-degree wrapping)
    [[nodiscard]] float interpolatedYaw(float alpha) const;

    /// Interpolate pitch
    [[nodiscard]] float interpolatedPitch(float alpha) const;

    /// Blend factor between the last two snapshots at the given tick rate
    [[nodiscard]] float interpolationAlpha(float tickRate) const;

    /// Bring shownYaw round toward currentYaw over a frame of dt seconds
    void turn(float dt);

    /// Work out groundLift for a frame of dt seconds at this interpolation alpha: on the ground,
    /// or stepping down off a block, it's drawn on the smoothed surface under it. Null cells
    /// draw it where it is.
    void followGround(const GroundCellProvider& cells, float alpha, float dt);

    /// Update from a new entity state snapshot
    void applySnapshot(const EntityState& state);
};

}  // namespace finevox
