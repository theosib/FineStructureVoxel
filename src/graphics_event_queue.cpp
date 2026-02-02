#include "finevox/graphics_event_queue.hpp"
#include <chrono>

namespace finevox {

GraphicsEvent GraphicsEvent::entitySnapshot(const Entity& entity, uint64_t tick) {
    GraphicsEvent event;
    event.type = GraphicsEventType::EntitySnapshot;
    event.tickNumber = tick;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    event.entityId = entity.id();
    event.entityType = static_cast<uint16_t>(entity.type());

    event.setPosition(entity.position());
    event.setVelocity(entity.velocity());
    event.yaw = entity.yaw();
    event.pitch = entity.pitch();
    event.onGround = entity.isOnGround();

    event.animationTime = entity.animationTime();
    event.animationId = entity.animationId();

    return event;
}

GraphicsEvent GraphicsEvent::entitySpawn(EntityId id, EntityType type,
                                          Vec3 pos, float yaw, float pitch) {
    GraphicsEvent event;
    event.type = GraphicsEventType::EntitySpawn;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    event.entityId = id;
    event.entityType = static_cast<uint16_t>(type);
    event.setPosition(pos);
    event.yaw = yaw;
    event.pitch = pitch;

    return event;
}

GraphicsEvent GraphicsEvent::entityDespawn(EntityId id) {
    GraphicsEvent event;
    event.type = GraphicsEventType::EntityDespawn;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    event.entityId = id;
    return event;
}

GraphicsEvent GraphicsEvent::playerCorrection(EntityId id, Vec3 pos, Vec3 vel,
                                               bool ground, uint64_t seq,
                                               CorrectionReason reason) {
    GraphicsEvent event;
    event.type = GraphicsEventType::PlayerCorrection;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    event.entityId = id;
    event.setPosition(pos);
    event.setVelocity(vel);
    event.onGround = ground;
    event.inputSequence = seq;
    event.correctionReason = reason;

    return event;
}

GraphicsEvent GraphicsEvent::blockCorrection(BlockPos pos, BlockTypeId correct,
                                              BlockTypeId expected) {
    GraphicsEvent event;
    event.type = GraphicsEventType::BlockCorrection;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    event.blockX = pos.x;
    event.blockY = pos.y;
    event.blockZ = pos.z;
    event.correctBlockType = correct.id;
    event.expectedBlockType = expected.id;

    return event;
}

GraphicsEvent GraphicsEvent::animation(EntityId id, uint8_t animId, float time) {
    GraphicsEvent event;
    event.type = GraphicsEventType::EntityAnimation;
    event.timestamp = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    event.entityId = id;
    event.animationId = animId;
    event.animationTime = time;

    return event;
}

}  // namespace finevox
