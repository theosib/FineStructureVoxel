#include "finevox/core/event_queue.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/chunk_column.hpp"  // For ChunkColumn activity timer
#include "finevox/core/block_type.hpp"    // For BlockRegistry
#include "finevox/core/block_handler.hpp" // For BlockContext, BlockHandler

namespace finevox {

void EventOutbox::push(BlockEvent event) {
    EventKey key{event.pos, event.type};
    auto [it, inserted] = pending_.emplace(key, event);
    if (!inserted) {
        // Same event type at same position - merge them
        it->second = mergeEvents(it->second, event);
    }
}

void EventOutbox::swapTo(std::vector<BlockEvent>& inbox) {
    inbox.reserve(inbox.size() + pending_.size());

    for (auto& [pos, event] : pending_) {
        inbox.push_back(std::move(event));
    }

    pending_.clear();
}

BlockEvent EventOutbox::mergeEvents(const BlockEvent& existing, const BlockEvent& incoming) {
    // Events are keyed by (pos, type), so existing.type == incoming.type is guaranteed
    BlockEvent merged = incoming;  // Start with incoming (more recent)

    switch (existing.type) {
        case EventType::NeighborChanged:
            // Merge face masks - OR them together
            merged.neighborFaceMask = existing.neighborFaceMask | incoming.neighborFaceMask;
            // Keep changedFace from incoming (most recent primary face)
            break;

        case EventType::BlockPlaced:
        case EventType::BlockBroken:
        case EventType::BlockChanged:
            // For block changes, keep the most recent block type info
            // but preserve the original previousType for the full change history
            if (existing.hasPreviousType() && !incoming.hasPreviousType()) {
                merged.previousType = existing.previousType;
            }
            break;

        default:
            // For other event types, just use the incoming event
            break;
    }

    // Keep the earlier timestamp for ordering
    merged.timestamp = std::min(existing.timestamp, incoming.timestamp);

    return merged;
}

// ============================================================================
// UpdateScheduler Implementation
// ============================================================================

UpdateScheduler::UpdateScheduler(World& world)
    : world_(world)
{
    // Initialize RNG - use config seed if non-zero, otherwise random
    if (config_.randomSeed != 0) {
        rng_.seed(static_cast<std::mt19937::result_type>(config_.randomSeed));
    } else {
        std::random_device rd;
        rng_.seed(rd());
    }
}

UpdateScheduler::~UpdateScheduler() = default;

void UpdateScheduler::setTickConfig(const TickConfig& config) {
    config_ = config;

    // Re-seed RNG if seed changed
    if (config_.randomSeed != 0) {
        rng_.seed(static_cast<std::mt19937::result_type>(config_.randomSeed));
    }
}

void UpdateScheduler::scheduleTick(BlockPos pos, int ticksFromNow, TickType type) {
    if (ticksFromNow <= 0) {
        ticksFromNow = 1;  // Minimum 1 tick in the future
    }

    ScheduledTick tick;
    tick.pos = pos;
    tick.targetTick = currentTick_ + static_cast<uint64_t>(ticksFromNow);
    tick.type = type;

    scheduledTicks_.push(tick);
}

void UpdateScheduler::cancelScheduledTicks(BlockPos pos) {
    // O(n) operation - rebuild queue without matching position
    std::vector<ScheduledTick> remaining;
    remaining.reserve(scheduledTicks_.size());

    while (!scheduledTicks_.empty()) {
        ScheduledTick tick = scheduledTicks_.top();
        scheduledTicks_.pop();

        if (tick.pos.x != pos.x || tick.pos.y != pos.y || tick.pos.z != pos.z) {
            remaining.push_back(tick);
        }
    }

    for (const auto& tick : remaining) {
        scheduledTicks_.push(tick);
    }
}

bool UpdateScheduler::hasScheduledTick(BlockPos pos) const {
    // Need non-const access to iterate - use a copy
    auto copy = scheduledTicks_;
    while (!copy.empty()) {
        const auto& tick = copy.top();
        if (tick.pos.x == pos.x && tick.pos.y == pos.y && tick.pos.z == pos.z) {
            return true;
        }
        copy.pop();
    }
    return false;
}

void UpdateScheduler::pushExternalEvent(BlockEvent event) {
    std::lock_guard<std::mutex> lock(externalMutex_);
    externalInput_.push_back(std::move(event));
}

void UpdateScheduler::pushExternalEvents(std::vector<BlockEvent> events) {
    if (events.empty()) return;

    std::lock_guard<std::mutex> lock(externalMutex_);
    externalInput_.reserve(externalInput_.size() + events.size());
    for (auto& event : events) {
        externalInput_.push_back(std::move(event));
    }
}

size_t UpdateScheduler::pendingEventCount() const {
    std::lock_guard<std::mutex> lock(externalMutex_);
    return externalInput_.size() + inbox_.size() + outbox_.size();
}

size_t UpdateScheduler::deferredEventCount() const {
    return deferredEvents_.size();
}

void UpdateScheduler::setChunkLoadCallback(std::function<void(ColumnPos)> callback) {
    chunkLoadCallback_ = std::move(callback);
}

void UpdateScheduler::drainExternalInput() {
    std::lock_guard<std::mutex> lock(externalMutex_);
    for (auto& event : externalInput_) {
        inbox_.push_back(std::move(event));
    }
    externalInput_.clear();
}

size_t UpdateScheduler::processEvents() {
    size_t processed = 0;

    // Drain external input first
    drainExternalInput();

    // Process until stable
    while (!inbox_.empty() || !outbox_.empty()) {
        // Process all events in inbox
        while (!inbox_.empty()) {
            BlockEvent event = std::move(inbox_.back());
            inbox_.pop_back();

            processEvent(event);
            ++processed;
        }

        // Swap outbox to inbox
        if (!outbox_.empty()) {
            outbox_.swapTo(inbox_);
        }
    }

    return processed;
}

void UpdateScheduler::advanceGameTick() {
    ++currentTick_;

    // Process deferred events whose chunks are now loaded
    processDeferredEvents();

    // Generate tick events
    if (config_.gameTicksEnabled) {
        generateGameTickEvents();
    }

    if (config_.randomTicksEnabled && config_.randomTicksPerSubchunk > 0) {
        generateRandomTickEvents();
    }

    // Process scheduled ticks that are due
    processScheduledTicks();
}

void UpdateScheduler::processDeferredEvents() {
    if (deferredEvents_.empty()) {
        return;
    }

    // Try to process deferred events - keep those that still can't be processed
    std::vector<BlockEvent> stillDeferred;

    for (auto& event : deferredEvents_) {
        SubChunk* subchunk = world_.getSubChunk(event.chunkPos);
        if (subchunk) {
            // Chunk is now loaded - move to inbox for processing
            inbox_.push_back(std::move(event));
        } else {
            // Still not loaded - keep deferred
            stillDeferred.push_back(std::move(event));
        }
    }

    deferredEvents_ = std::move(stillDeferred);
}

bool UpdateScheduler::processEvent(const BlockEvent& event) {
    // Get the subchunk
    SubChunk* subchunk = world_.getSubChunk(event.chunkPos);
    if (!subchunk) {
        // Chunk not loaded - for BlockUpdate events, defer and request load
        if (event.type == EventType::BlockUpdate) {
            deferredEvents_.push_back(event);
            if (chunkLoadCallback_) {
                ColumnPos colPos{event.chunkPos.x, event.chunkPos.z};
                chunkLoadCallback_(colPos);
            }
            return false;  // Event was deferred, not processed
        }
        return true;  // Other events are dropped if chunk not loaded
    }

    // For BlockUpdate events, touch the column's activity timer
    if (event.type == EventType::BlockUpdate) {
        ColumnPos colPos{event.chunkPos.x, event.chunkPos.z};
        ChunkColumn* column = world_.getColumn(colPos);
        if (column) {
            column->touchActivity();
        }
    }

    // Calculate local index for game tick registration
    uint16_t localIndex = event.localPos.toIndex();

    BlockTypeId blockType = subchunk->getBlock(
        event.localPos.x, event.localPos.y, event.localPos.z);

    // Handle BlockBroken specially - need to process before the block is gone
    if (event.type == EventType::BlockBroken) {
        // Unregister from game ticks
        subchunk->unregisterFromGameTicks(localIndex);

        // Cancel any scheduled ticks for this position
        cancelScheduledTicks(event.pos);

        // Call handler if exists (before removing the block)
        if (!blockType.isAir()) {
            BlockHandler* handler = BlockRegistry::global().getHandler(blockType);
            if (handler) {
                BlockContext ctx(world_, *subchunk, event.pos, event.localPos);
                ctx.setScheduler(this);
                handler->onBreak(ctx);
            }
        }

        // Actually remove the block from the world
        world_.setBlock(event.pos, AIR_BLOCK_TYPE);

        // Enqueue lighting update with smart remesh deferral
        // If lighting queue is empty, lighting thread handles remesh
        // Otherwise we push remesh immediately and lighting handles additional
        world_.enqueueLightingUpdateWithRemesh(event.pos, blockType, AIR_BLOCK_TYPE);
        return true;
    }

    // Handle BlockPlaced - need to place the block first, then call handler
    if (event.type == EventType::BlockPlaced) {
        // Place the block in the world first
        world_.setBlock(event.pos, event.blockType);

        // Look up handler for the NEW block type
        BlockHandler* handler = BlockRegistry::global().getHandler(event.blockType);

        // Create context and call handler
        BlockContext ctx(world_, *subchunk, event.pos, event.localPos);
        ctx.setScheduler(this);
        if (handler) {
            ctx.setPreviousType(event.previousType);
            handler->onPlace(ctx);
        }

        // After handler returns, check if block wants game ticks
        // (Re-read block type in case handler changed it)
        BlockTypeId currentType = subchunk->getBlock(
            event.localPos.x, event.localPos.y, event.localPos.z);
        if (!currentType.isAir()) {
            const BlockType& typeInfo = BlockRegistry::global().getType(currentType);
            if (typeInfo.wantsGameTicks()) {
                subchunk->registerForGameTicks(localIndex);
            }
        }

        // Enqueue lighting update with smart remesh deferral
        // If lighting queue is empty, lighting thread handles remesh
        // Otherwise we push remesh immediately and lighting handles additional
        world_.enqueueLightingUpdateWithRemesh(event.pos, event.previousType, currentType);
        return true;
    }

    // For other events, need a valid block with handler
    if (blockType.isAir()) {
        return true;  // No handler for air
    }

    // Look up handler (may be null for simple blocks)
    BlockHandler* handler = BlockRegistry::global().getHandler(blockType);

    // Create context for handler calls
    BlockContext ctx(world_, *subchunk, event.pos, event.localPos);
    ctx.setScheduler(this);

    switch (event.type) {

        case EventType::NeighborChanged:
            if (handler) {
                // Call for each changed face
                event.forEachChangedNeighbor([&](Face face) {
                    handler->onNeighborChanged(ctx, face);
                });
            }
            break;

        case EventType::BlockUpdate:
            if (handler) {
                handler->onBlockUpdate(ctx);
            }
            break;

        case EventType::TickGame:
        case EventType::TickScheduled:
        case EventType::TickRepeat:
        case EventType::TickRandom:
            if (handler) {
                handler->onTick(ctx, event.tickType);
            }
            break;

        case EventType::PlayerUse:
            if (handler) {
                handler->onUse(ctx, event.face);
            }
            break;

        case EventType::PlayerHit:
            if (handler) {
                handler->onHit(ctx, event.face);
            }
            break;

        case EventType::RepaintRequested:
            if (handler) {
                handler->onRepaint(ctx);
            }
            break;

        default:
            // Ignore other event types for now
            break;
    }

    return true;  // Event was processed
}

void UpdateScheduler::generateGameTickEvents() {
    // Iterate over all loaded subchunks
    auto subchunkPositions = world_.getAllSubChunkPositions();

    for (const ChunkPos& chunkPos : subchunkPositions) {
        SubChunk* subchunk = world_.getSubChunk(chunkPos);
        if (!subchunk) continue;

        const auto& tickBlocks = subchunk->gameTickBlocks();
        for (uint16_t localIndex : tickBlocks) {
            BlockPos worldPos = chunkPos.toWorld(localIndex);
            outbox_.push(BlockEvent::tick(worldPos, TickType::Scheduled));  // Use Scheduled for game ticks
        }
    }
}

void UpdateScheduler::generateRandomTickEvents() {
    std::uniform_int_distribution<int32_t> dist(0, SubChunk::VOLUME - 1);

    auto subchunkPositions = world_.getAllSubChunkPositions();

    for (const ChunkPos& chunkPos : subchunkPositions) {
        SubChunk* subchunk = world_.getSubChunk(chunkPos);
        if (!subchunk) continue;

        // Generate N random tick positions
        for (uint32_t i = 0; i < config_.randomTicksPerSubchunk; ++i) {
            int32_t localIndex = dist(rng_);
            BlockPos worldPos = chunkPos.toWorld(localIndex);

            // Only generate tick if block is not air
            BlockTypeId blockType = subchunk->getBlock(localIndex);
            if (!blockType.isAir()) {
                outbox_.push(BlockEvent::tick(worldPos, TickType::Random));
            }
        }
    }
}

void UpdateScheduler::processScheduledTicks() {
    while (!scheduledTicks_.empty() && scheduledTicks_.top().targetTick <= currentTick_) {
        ScheduledTick tick = scheduledTicks_.top();
        scheduledTicks_.pop();

        // Generate event for this scheduled tick
        outbox_.push(BlockEvent::tick(tick.pos, tick.type));
    }
}

}  // namespace finevox
