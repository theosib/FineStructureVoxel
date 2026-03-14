#include "finevox/render/entity_renderer.hpp"
#include "finevox/script/event_value.hpp"

namespace finevox::render {

using namespace finevox::script;

void EntityRenderer::processEvents(GraphicsEventQueue& queue) {
    auto messages = queue.drainAll();
    for (auto& msg : messages) {
        if (msg.kind == GraphicsMessage::Kind::Snapshot) {
            handleSnapshot(msg.snapshot);
        } else {
            handleEvent(msg.event);
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

void EntityRenderer::handleSnapshot(const EntitySnapshot& snap) {
    auto it = entities_.find(snap.entity.id);
    if (it == entities_.end()) return;
    it->second.applySnapshot(snap.entity);
}

void EntityRenderer::handleEvent(const finescript::Value& event) {
    auto type = readEventType(event);

    if (type == EVT_ENTITY_SPAWN) {
        EntityId id = readEntityId(event);
        if (entities_.count(id)) return;

        const auto& s = EventSymbols::instance();
        auto pos = readDVec3(event, s.pos_x, s.pos_y, s.pos_z);
        float yaw = readFloat(event, s.yaw);
        float pitch = readFloat(event, s.pitch);

        EntityRenderState state;
        state.id = id;
        state.prevPosition = pos;
        state.currentPosition = pos;
        state.prevYaw = yaw;
        state.currentYaw = yaw;
        state.prevPitch = pitch;
        state.currentPitch = pitch;
        state.visible = true;
        state.active = true;

        entities_[id] = std::move(state);
    }
    else if (type == EVT_ENTITY_DESPAWN) {
        entities_.erase(readEntityId(event));
    }
    else if (type == EVT_ENTITY_ANIMATION) {
        EntityId id = readEntityId(event);
        auto it = entities_.find(id);
        if (it == entities_.end()) return;
        // Animation clip lookup would happen here in full integration
    }
}

}  // namespace finevox::render
