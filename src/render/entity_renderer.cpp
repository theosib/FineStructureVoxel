#include "finevox/render/entity_renderer.hpp"

namespace finevox::render {

void EntityRenderer::processEvents(GraphicsEventQueue& queue) {
    auto events = queue.drainAll();
    for (const auto& event : events) {
        switch (event.type) {
            case GraphicsEventType::EntitySpawn:
                handleSpawn(event);
                break;
            case GraphicsEventType::EntityDespawn:
                handleDespawn(event);
                break;
            case GraphicsEventType::EntitySnapshot:
                handleSnapshot(event);
                break;
            case GraphicsEventType::EntityAnimation:
                handleAnimation(event);
                break;
            default:
                break;
        }
    }
}

void EntityRenderer::update(float dt, const glm::dvec3& /*cameraPos*/) {
    for (auto& [id, state] : entities_) {
        if (!state.active) continue;
        state.animator.update(dt);
    }
}

void EntityRenderer::buildEntityVertices(
    EntityId /*id*/,
    const EntityMesh& mesh,
    const std::vector<glm::mat4>& worldPoses,
    std::vector<EntityVertex>& vertices,
    std::vector<uint32_t>& indices) const
{
    mesh.buildVertices(worldPoses, vertices, indices);
}

size_t EntityRenderer::visibleCount() const {
    size_t count = 0;
    for (const auto& [id, state] : entities_) {
        if (state.visible && state.active) ++count;
    }
    return count;
}

void EntityRenderer::clear() {
    entities_.clear();
}

void EntityRenderer::handleSpawn(const GraphicsEvent& event) {
    EntityId id = event.entity.id;
    if (entities_.count(id)) return;  // Already exists

    EntityRenderState state;
    state.id = id;
    state.prevPosition = event.entity.position;
    state.currentPosition = event.entity.position;
    state.prevYaw = event.entity.yaw;
    state.currentYaw = event.entity.yaw;
    state.prevPitch = event.entity.pitch;
    state.currentPitch = event.entity.pitch;
    state.visible = true;
    state.active = true;

    entities_[id] = std::move(state);
}

void EntityRenderer::handleDespawn(const GraphicsEvent& event) {
    entities_.erase(event.entity.id);
}

void EntityRenderer::handleSnapshot(const GraphicsEvent& event) {
    auto it = entities_.find(event.entity.id);
    if (it == entities_.end()) return;
    it->second.applySnapshot(event.entity);
}

void EntityRenderer::handleAnimation(const GraphicsEvent& event) {
    auto it = entities_.find(event.entity.id);
    if (it == entities_.end()) return;
    // Animation clip lookup would happen here in full integration
    // For now, just note the animation change was received
    (void)event.entity.animationId;
}

}  // namespace finevox::render
