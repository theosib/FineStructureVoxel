#include "finevox/block_event.hpp"
#include "finevox/block_handler.hpp"  // For TickType

namespace finevox {

BlockEvent BlockEvent::blockPlaced(BlockPos pos, BlockTypeId newType,
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

BlockEvent BlockEvent::blockBroken(BlockPos pos, BlockTypeId oldType) {
    BlockEvent event;
    event.type = EventType::BlockBroken;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.previousType = oldType;
    event.blockType = AIR_BLOCK_TYPE;
    return event;
}

BlockEvent BlockEvent::blockChanged(BlockPos pos, BlockTypeId oldType, BlockTypeId newType) {
    BlockEvent event;
    event.type = EventType::BlockChanged;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.previousType = oldType;
    event.blockType = newType;
    return event;
}

BlockEvent BlockEvent::neighborChanged(BlockPos pos, Face changedFace) {
    BlockEvent event;
    event.type = EventType::NeighborChanged;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.changedFace = changedFace;
    return event;
}

BlockEvent BlockEvent::tick(BlockPos pos, TickType tickType) {
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

BlockEvent BlockEvent::playerUse(BlockPos pos, Face face) {
    BlockEvent event;
    event.type = EventType::PlayerUse;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.face = face;
    return event;
}

BlockEvent BlockEvent::playerHit(BlockPos pos, Face face) {
    BlockEvent event;
    event.type = EventType::PlayerHit;
    event.pos = pos;
    event.localPos = pos.local();
    event.chunkPos = ChunkPos::fromBlock(pos);
    event.face = face;
    return event;
}

}  // namespace finevox
