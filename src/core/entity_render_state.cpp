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

float EntityRenderState::interpolatedYawRadians(float alpha) const {
    constexpr float PI = 3.14159265f;
    float diff = currentYaw - prevYaw;
    while (diff > PI) diff -= 2.0f * PI;
    while (diff < -PI) diff += 2.0f * PI;
    return prevYaw + diff * alpha;
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
    timeSinceSnapshot = 0.0f;
}

}  // namespace finevox
