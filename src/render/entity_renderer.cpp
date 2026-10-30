#include "finevox/render/entity_renderer.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/script/event_value.hpp"

namespace finevox::render {

using namespace finevox::script;

void EntityRenderer::registerAnimationClip(EntityTypeId typeId, uint8_t slotId, const AnimationClip* clip) {
    if (!typeId.isValid()) return;
    animationClips_[makeClipKey(typeId, slotId)] = clip;
}

void EntityRenderer::registerAnimationClip(EntityTypeId typeId, std::string_view stateName, const AnimationClip* clip) {
    if (!typeId.isValid()) return;
    uint8_t slotId = 0;
    if (const auto* def = EntityTypeRegistry::global().getType(typeId)) {
        slotId = def->resolveAnimation(stateName, 0);
    }
    registerAnimationClip(typeId, slotId, clip);
}

const AnimationClip* EntityRenderer::findAnimationClip(EntityTypeId typeId, uint8_t slotId) const {
    if (!typeId.isValid()) return nullptr;
    auto it = animationClips_.find(makeClipKey(typeId, slotId));
    return it != animationClips_.end() ? it->second : nullptr;
}

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
        state.timeSinceSnapshot += dt;
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
    if (snap.entity.animationId != it->second.currentAnimationId) {
        it->second.currentAnimationId = snap.entity.animationId;
        if (const auto* clip = findAnimationClip(it->second.typeId, snap.entity.animationId)) {
            it->second.animator.play(clip);
        }
    }
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
        std::string_view typeName = readString(event, s.entity_type_name);

        EntityRenderState state;
        state.id = id;
        if (!typeName.empty()) {
            state.typeId = EntityTypeId::fromName(typeName);
        }
        state.prevPosition = pos;
        state.currentPosition = pos;
        state.prevYaw = yaw;
        state.currentYaw = yaw;
        state.prevPitch = pitch;
        state.currentPitch = pitch;
        state.visible = true;
        state.active = true;

        if (const auto* clip = findAnimationClip(state.typeId, 0)) {
            state.animator.play(clip, 0.0f);
        }

        entities_[id] = std::move(state);
    }
    else if (type == EVT_ENTITY_DESPAWN) {
        entities_.erase(readEntityId(event));
    }
    else if (type == EVT_ENTITY_ANIMATION) {
        EntityId id = readEntityId(event);
        auto it = entities_.find(id);
        if (it == entities_.end()) return;

        const auto& s = EventSymbols::instance();
        uint8_t animId = static_cast<uint8_t>(readInt(event, s.animation_id, 0));
        float animTime = readFloat(event, s.animation_time, 0.2f);
        it->second.currentAnimationId = animId;

        if (const auto* clip = findAnimationClip(it->second.typeId, animId)) {
            it->second.animator.play(clip, animTime >= 0.0f ? animTime : 0.2f);
        }
    }
    else if (type == EVT_PLAYER_CORRECTION) {
        const auto& s = EventSymbols::instance();
        EntityId id = readEntityId(event);
        auto pos = readDVec3(event, s.pos_x, s.pos_y, s.pos_z);
        auto vel = readDVec3(event, s.vel_x, s.vel_y, s.vel_z);
        bool onGround = readBool(event, s.on_ground);
        uint64_t seq = static_cast<uint64_t>(readInt(event, s.input_sequence));
        std::string_view reason = readString(event, s.correction_reason);

        auto it = entities_.find(id);
        if (it != entities_.end()) {
            it->second.prevPosition = pos;
            it->second.currentPosition = pos;
        }

        if (playerCorrectionCallback_) {
            playerCorrectionCallback_(id, pos, vel, onGround, seq, reason);
        }
    }
}

}  // namespace finevox::render
