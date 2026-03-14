#include "finevox/core/block_event.hpp"
#include "finevox/core/block_handler.hpp"  // For TickType

namespace finevox {

BlockEvent BlockEvent::blockPlaced(BlockCoord pos, BlockTypeId newType,
                                   BlockTypeId oldType, Rotation rot) {
    BlockEvent event;
    event.type = EventType::BlockPlaced;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.blockType = newType;
    event.previousType = oldType;
    event.rotation = rot;
    return event;
}

BlockEvent BlockEvent::blockBroken(BlockCoord pos, BlockTypeId oldType) {
    BlockEvent event;
    event.type = EventType::BlockBroken;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.previousType = oldType;
    event.blockType = AIR_BLOCK_TYPE;
    return event;
}

BlockEvent BlockEvent::blockChanged(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType) {
    BlockEvent event;
    event.type = EventType::BlockChanged;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.previousType = oldType;
    event.blockType = newType;
    return event;
}

BlockEvent BlockEvent::neighborUpdated(BlockCoord pos, Face changedFace) {
    BlockEvent event;
    event.type = EventType::NeighborUpdated;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.changedFace = changedFace;
    return event;
}

BlockEvent BlockEvent::tick(BlockCoord pos, TickType tickType) {
    BlockEvent event;
    switch (tickType) {
        case TickType::Scheduled:
            event.type = EventType::TickScheduled;
            break;
        case TickType::Repeat:
            event.type = EventType::TickRepeat;
            break;
        case TickType::Random:
            event.type = EventType::TickRandom;
            break;
        default:
            event.type = EventType::TickScheduled;
            break;
    }
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.tickType = tickType;
    return event;
}

BlockEvent BlockEvent::playerUse(BlockCoord pos, Face face) {
    BlockEvent event;
    event.type = EventType::PlayerUse;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.face = face;
    return event;
}

BlockEvent BlockEvent::playerHit(BlockCoord pos, Face face) {
    BlockEvent event;
    event.type = EventType::PlayerHit;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.face = face;
    return event;
}

BlockEvent BlockEvent::blockUpdate(BlockCoord pos) {
    BlockEvent event;
    event.type = EventType::BlockUpdate;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    return event;
}

// ============================================================================
// Player Event Factory Methods
// ============================================================================

BlockEvent BlockEvent::playerPosition(EntityId id, glm::dvec3 position, glm::dvec3 velocity,
                                       bool onGround, uint64_t inputSequence) {
    BlockEvent event;
    event.type = EventType::PlayerPosition;
    event.entityId = id;
    event.entityState.id = id;
    event.entityState.position = position;
    event.entityState.velocity = velocity;
    event.entityState.onGround = onGround;
    event.entityState.inputSequence = inputSequence;
    return event;
}

BlockEvent BlockEvent::playerLook(EntityId id, float yaw, float pitch) {
    BlockEvent event;
    event.type = EventType::PlayerLook;
    event.entityId = id;
    event.entityState.id = id;
    event.entityState.yaw = yaw;
    event.entityState.pitch = pitch;
    return event;
}

BlockEvent BlockEvent::playerJump(EntityId id) {
    BlockEvent event;
    event.type = EventType::PlayerJump;
    event.entityId = id;
    return event;
}

BlockEvent BlockEvent::playerSprint(EntityId id, bool starting) {
    BlockEvent event;
    event.type = starting ? EventType::PlayerStartSprint : EventType::PlayerStopSprint;
    event.entityId = id;
    return event;
}

BlockEvent BlockEvent::playerSneak(EntityId id, bool starting) {
    BlockEvent event;
    event.type = starting ? EventType::PlayerStartSneak : EventType::PlayerStopSneak;
    event.entityId = id;
    return event;
}

BlockEvent BlockEvent::setWorldTime(int64_t ticks) {
    BlockEvent event;
    event.type = EventType::SetWorldTime;
    event.entityState.inputSequence = static_cast<uint64_t>(ticks);
    return event;
}

// ============================================================================
// Fluid Event Factory Methods
// ============================================================================

BlockEvent BlockEvent::fluidPlaced(BlockCoord pos, FluidTypeId type, uint8_t level) {
    BlockEvent event;
    event.type = EventType::FluidPlaced;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.fluidType = type;
    event.fluidLevel = level;
    return event;
}

BlockEvent BlockEvent::fluidRemoved(BlockCoord pos, FluidTypeId previousFluid) {
    BlockEvent event;
    event.type = EventType::FluidRemoved;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.fluidType = previousFluid;
    return event;
}

// ============================================================================
// Crafting Event Factory Methods
// ============================================================================

BlockEvent BlockEvent::craftItem(BlockCoord stationPos, RecipeId recipe) {
    BlockEvent event;
    event.type = EventType::CraftItem;
    event.pos = stationPos;
    event.localPos = stationPos.local();
    event.chunkPos = ChunkPos::fromBlock(stationPos);
    event.recipeId = recipe;
    return event;
}

}  // namespace finevox
