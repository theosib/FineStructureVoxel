#include "finevox/block_handler.hpp"
#include "finevox/world.hpp"
#include "finevox/subchunk.hpp"
#include "finevox/block_type.hpp"
#include "finevox/data_container.hpp"

#include <cassert>
#include <stdexcept>

namespace finevox {

// ============================================================================
// BlockContext Implementation
// ============================================================================

BlockContext::BlockContext(World& world, SubChunk& subChunk,
                           BlockPos pos, BlockPos localPos)
    : world_(world)
    , subChunk_(subChunk)
    , pos_(pos)
    , localPos_(localPos)
{
}

BlockTypeId BlockContext::blockType() const {
    return subChunk_.getBlock(localPos_.x, localPos_.y, localPos_.z);
}

Rotation BlockContext::rotation() const {
    // TODO: Implement rotation storage in SubChunk (Phase 9)
    // For now, return identity rotation
    return Rotation::IDENTITY;
}

void BlockContext::setRotation(Rotation rot) {
    // TODO: Implement rotation storage in SubChunk (Phase 9)
    // For now, this is a no-op
    (void)rot;
}

DataContainer* BlockContext::data() {
    return subChunk_.blockData(localPos_.x, localPos_.y, localPos_.z);
}

DataContainer& BlockContext::getOrCreateData() {
    return subChunk_.getOrCreateBlockData(localPos_.x, localPos_.y, localPos_.z);
}

void BlockContext::scheduleTick(int ticksFromNow) {
    // TODO: Implement tick scheduling (Phase 9)
    // For now, this is a no-op
    (void)ticksFromNow;
}

void BlockContext::setRepeatTickInterval(int interval) {
    // TODO: Implement tick scheduling (Phase 9)
    // For now, this is a no-op
    (void)interval;
}

void BlockContext::requestMeshRebuild() {
    // Touch the block to trigger version increment
    // setBlock with the same type increments blockVersion
    BlockTypeId currentType = blockType();
    subChunk_.setBlock(localPos_.x, localPos_.y, localPos_.z, currentType);
}

void BlockContext::markDirty() {
    // Touch the block to trigger version increment
    // This flags the subchunk for persistence via version change
    BlockTypeId currentType = blockType();
    subChunk_.setBlock(localPos_.x, localPos_.y, localPos_.z, currentType);
}

BlockTypeId BlockContext::getNeighbor(Face face) const {
    BlockPos neighborPos = pos_.neighbor(face);
    return world_.getBlock(neighborPos);
}

void BlockContext::notifyNeighbors() {
    // Get handler for each neighbor and call onNeighborChanged
    BlockRegistry& registry = BlockRegistry::global();

    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        Face face = static_cast<Face>(faceIdx);
        BlockPos neighborPos = pos_.neighbor(face);

        BlockTypeId neighborType = world_.getBlock(neighborPos);
        if (neighborType.isAir()) {
            continue;
        }

        // Get handler for neighbor
        BlockHandler* handler = registry.getHandler(neighborType);
        if (!handler) {
            continue;
        }

        // Get the subchunk containing the neighbor
        ChunkPos neighborChunkPos = ChunkPos::fromBlock(neighborPos);
        auto neighborSubChunk = world_.getSubChunkShared(neighborChunkPos);
        if (!neighborSubChunk) {
            continue;
        }

        // Calculate local position within neighbor's subchunk
        BlockPos neighborLocalPos;
        neighborLocalPos.x = ((neighborPos.x % 16) + 16) % 16;
        neighborLocalPos.y = ((neighborPos.y % 16) + 16) % 16;
        neighborLocalPos.z = ((neighborPos.z % 16) + 16) % 16;

        // Create context for neighbor and notify
        BlockContext neighborCtx(world_, *neighborSubChunk, neighborPos, neighborLocalPos);

        // The opposite face is the one that changed from the neighbor's perspective
        Face changedFace = oppositeFace(face);
        handler->onNeighborChanged(neighborCtx, changedFace);
    }
}

void BlockContext::setBlock(BlockTypeId type) {
    subChunk_.setBlock(localPos_.x, localPos_.y, localPos_.z, type);
}

std::unique_ptr<DataContainer> BlockContext::takePreviousData() {
    return std::move(previousData_);
}

void BlockContext::setPreviousData(std::unique_ptr<DataContainer> data) {
    previousData_ = std::move(data);
}

}  // namespace finevox
