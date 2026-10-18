#include "finevox/core/block_handler.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/event_queue.hpp"  // For UpdateScheduler
#include "finevox/core/fluid_tick_manager.hpp"
#include "finevox/core/loot_registry.hpp"

#include <cassert>
#include <stdexcept>

namespace finevox {

// ============================================================================
// BlockContext Implementation
// ============================================================================

BlockContext::BlockContext(World& world, SubChunk& subChunk,
                           BlockCoord pos, LocalBlockCoord localPos)
    : world_(world)
    , subChunk_(subChunk)
    , pos_(pos)
    , localPos_(localPos)
{
}

BlockTypeId BlockContext::blockType() const {
    return subChunk_.getBlock(localPos_.x, localPos_.y, localPos_.z);
}

const BlockType* BlockContext::type() const {
    return &BlockRegistry::global().getType(blockType());
}

ChunkPos BlockContext::chunkPos() const {
    return subChunk_.position();
}

int32_t BlockContext::localIndex() const {
    // Index layout: y*256 + z*16 + x
    return localPos_.y * 256 + localPos_.z * 16 + localPos_.x;
}

bool BlockContext::isAir() const {
    return blockType() == AIR_BLOCK_TYPE;
}

bool BlockContext::isOpaque() const {
    return type()->isOpaque();
}

bool BlockContext::isTransparent() const {
    return type()->isTransparent();
}

Rotation BlockContext::rotation() const {
    return subChunk_.getRotation(localPos_);
}

void BlockContext::setRotation(Rotation rot) {
    subChunk_.setRotation(localPos_, rot);
}

uint8_t BlockContext::rotationIndex() const {
    return subChunk_.getRotationIndex(localPos_);
}

void BlockContext::setRotationIndex(uint8_t index) {
    subChunk_.setRotationIndex(localPos_, index);
}

uint8_t BlockContext::skyLight() const {
    return subChunk_.getSkyLight(localIndex());
}

uint8_t BlockContext::blockLight() const {
    return subChunk_.getBlockLight(localIndex());
}

uint8_t BlockContext::combinedLight() const {
    return subChunk_.getCombinedLight(localIndex());
}

DataContainer* BlockContext::data() {
    return subChunk_.blockData(localPos_.x, localPos_.y, localPos_.z);
}

DataContainer& BlockContext::getOrCreateData() {
    return subChunk_.getOrCreateBlockData(localPos_.x, localPos_.y, localPos_.z);
}

void BlockContext::scheduleTick(int ticksFromNow) {
    if (scheduler_) {
        scheduler_->scheduleTick(pos_, ticksFromNow, TickType::Scheduled);
    }
}

void BlockContext::setRepeatTickInterval(int interval) {
    if (scheduler_ && interval > 0) {
        // Schedule a repeating tick
        scheduler_->scheduleTick(pos_, interval, TickType::Repeat);
    }
    // Note: interval of 0 cancels repeating ticks, but that requires
    // additional infrastructure to track which blocks have repeating ticks.
    // For now, only scheduling is implemented.
}

void BlockContext::requestMeshRebuild() {
    // Touch the block to increment blockVersion and invoke blockChangeCallback_
    subChunk_.touchBlock(localPos_);
    if (auto* meshQueue = world_.meshRebuildQueue()) {
        for (const ChunkPos& affected : world_.getAffectedSubChunks(pos_)) {
            meshQueue->push(affected, MeshRebuildRequest::immediate());
        }
    }
}

void BlockContext::markDirty() {
    // Touch the block to trigger version increment and change notification
    subChunk_.touchBlock(localPos_);
}

BlockTypeId BlockContext::getNeighbor(Face face) const {
    BlockCoord neighborPos = pos_.neighbor(face);
    return world_.getBlock(neighborPos);
}

void BlockContext::notifyNeighbors() {
    neighborsNotified_ = true;

    // Get handler for each neighbor and call onNeighborUpdated
    BlockRegistry& registry = BlockRegistry::global();

    // Read source block's propagation policy once for all faces
    const BlockType* sourceType = type();
    PropagationPolicy policy = sourceType ? sourceType->propagationPolicy()
                                          : PropagationPolicy::Drop;

    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        Face face = static_cast<Face>(faceIdx);
        BlockCoord neighborPos = pos_.neighbor(face);

        // Get the subchunk containing the neighbor
        ChunkPos neighborChunkPos = ChunkPos::fromBlock(neighborPos);
        auto neighborSubChunk = world_.getSubChunkShared(neighborChunkPos);
        if (!neighborSubChunk) {
            // Neighbor chunk not loaded — apply propagation policy
            if (policy == PropagationPolicy::Defer && scheduler_) {
                scheduler_->outbox().push(BlockEvent::blockUpdate(neighborPos));
            }
            continue;
        }

        BlockTypeId neighborType = world_.getBlock(neighborPos);
        if (neighborType.isAir()) {
            continue;
        }

        // Get handler for neighbor
        BlockHandler* handler = registry.getHandler(neighborType);
        if (!handler) {
            continue;
        }

        // Calculate local position within neighbor's subchunk
        LocalBlockCoord neighborLocalPos = neighborPos.local();

        // Create context for neighbor, propagate scheduler, and notify
        BlockContext neighborCtx(world_, *neighborSubChunk, neighborPos, neighborLocalPos);
        neighborCtx.setScheduler(scheduler_);

        // The opposite face is the one that changed from the neighbor's perspective
        Face changedFace = oppositeFace(face);
        handler->onNeighborUpdated(neighborCtx, changedFace);
    }
}

void BlockContext::setBlock(BlockTypeId type) {
    BlockTypeId oldType = subChunk_.getBlock(localPos_.x, localPos_.y, localPos_.z);
    subChunk_.setBlock(localPos_.x, localPos_.y, localPos_.z, type);
    if (oldType != type) {
        if (world_.lightEngine()) {
            world_.enqueueLightingUpdateWithRemesh(pos_, oldType, type);
        }
        if (auto* mq = world_.meshRebuildQueue()) {
            for (const ChunkPos& affected : world_.getAffectedSubChunks(pos_)) {
                mq->push(affected, MeshRebuildRequest::immediate());
            }
        }
        if (auto* ftm = world_.fluidTickManager()) {
            ftm->notifyBlockChanged(pos_);
        }
    }
}

std::unique_ptr<DataContainer> BlockContext::takePreviousData() {
    return std::move(previousData_);
}

void BlockContext::setPreviousData(std::unique_ptr<DataContainer> data) {
    previousData_ = std::move(data);
}

std::vector<ItemStack> BlockContext::rollBlockLoot(const LootContext& extraCtx) const {
    auto blockId = blockType();
    auto* bt = type();
    if (!bt) return {};

    auto tableId = bt->lootTable();
    if (tableId.isEmpty()) return {};

    // Merge: fill in block-specific fields, preserve caller-provided fields
    LootContext ctx = extraCtx;
    ctx.brokenBlock = blockId;
    ctx.position = pos_;

    return LootRegistry::global().roll(tableId, ctx);
}

}  // namespace finevox
