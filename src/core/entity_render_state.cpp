#include "finevox/core/entity_render_state.hpp"
#include <algorithm>
#include <cmath>

namespace finevox {

glm::dvec3 EntityRenderState::interpolatedPosition(float alpha) const {
    return prevPosition + (currentPosition - prevPosition) * static_cast<double>(alpha);
}

float EntityRenderState::interpolatedYaw(float alpha) const {
    // Handle 360-degree wrapping
    float diff = currentYaw - prevYaw;
    if (diff > 180.0f) diff -= 360.0f;
    if (diff < -180.0f) diff += 360.0f;
    return prevYaw + diff * alpha;
}

float EntityRenderState::interpolatedPitch(float alpha) const {
    return prevPitch + (currentPitch - prevPitch) * alpha;
}

float EntityRenderState::interpolationAlpha(float tickRate) const {
    return std::min(1.0f, timeSinceSnapshot * tickRate);
}

void EntityRenderState::turn(float dt) {
    // Most of the way in a quarter of a second, whatever the angle: a body swings round
    // faster for a bigger turn
    constexpr float TURN_RATE = 12.0f;
    constexpr float TWO_PI = 6.28318531f;
    float diff = std::remainder(currentYaw - shownYaw, TWO_PI);
    shownYaw = std::remainder(shownYaw + diff * (1.0f - std::exp(-TURN_RATE * dt)), TWO_PI);
}

void EntityRenderState::followGround(const GroundCellProvider& cells, float alpha, float dt) {
    std::optional<double> surface;
    glm::dvec3 feet(0.0);
    if (cells) {
        feet = interpolatedPosition(alpha);
        surface = groundSurfaceY(cells, feet);
    }
    // Walking: stood on something, or coming down a step (the server drops a body off a ledge
    // a block, where the slope goes down smoothly). A body going up, or far above the ground, is
    // jumping or falling, and is drawn where it is.
    bool walking = false;
    if (surface) {
        double above = feet.y - *surface;
        walking = onGround || (velocity.y <= 0.5 && above < 1.2 && above > -1.2);
    }
    // Most of the way in a tenth of a second
    constexpr float FOLLOW_RATE = 20.0f;
    float target = walking ? 1.0f : 0.0f;
    groundWeight += (target - groundWeight) * (1.0f - std::exp(-FOLLOW_RATE * dt));
    if (!cells) groundWeight = 0.0f;
    groundLift = surface ? static_cast<float>(*surface - feet.y) * groundWeight : 0.0f;
}

void EntityRenderState::applySnapshot(const EntityState& state) {
    // Shift current to previous
    prevPosition = currentPosition;
    prevYaw = currentYaw;
    prevPitch = currentPitch;

    // Apply new state
    currentPosition = state.position;
    currentYaw = state.yaw;
    currentPitch = state.pitch;
    velocity = state.velocity;
    onGround = state.onGround;
    faction = state.faction;
    heldBlock = BlockTypeId(state.heldBlock);
    timeSinceSnapshot = 0.0f;
}

}  // namespace finevox
