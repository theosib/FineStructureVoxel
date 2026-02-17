#include "finevox/core/entity_render_state.hpp"
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

void EntityRenderState::applySnapshot(const EntityState& state) {
    // Shift current to previous
    prevPosition = currentPosition;
    prevYaw = currentYaw;
    prevPitch = currentPitch;

    // Apply new state
    currentPosition = state.position;
    currentYaw = state.yaw;
    currentPitch = state.pitch;
}

}  // namespace finevox
