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
    faction = state.faction;
    heldBlock = BlockTypeId(state.heldBlock);
    timeSinceSnapshot = 0.0f;
}

}  // namespace finevox
