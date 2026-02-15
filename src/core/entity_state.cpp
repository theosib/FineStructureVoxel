#include "finevox/core/entity_state.hpp"
#include "finevox/core/entity.hpp"

namespace finevox {

EntityState EntityState::fromEntity(const Entity& entity) {
    EntityState state;
    state.id = entity.id();
    state.entityType = static_cast<uint16_t>(entity.type());
    state.position = glm::dvec3(entity.position());
    state.velocity = glm::dvec3(entity.velocity());
    state.onGround = entity.isOnGround();
    state.yaw = entity.yaw();
    state.pitch = entity.pitch();
    state.animationId = entity.animationId();
    state.animationTime = entity.animationTime();
    return state;
}

}  // namespace finevox
