#include "finevox/light_engine.hpp"
#include "finevox/world.hpp"
#include "finevox/block_type.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/subchunk.hpp"

#include <algorithm>
#include <array>

namespace finevox {

// ============================================================================
// LightingQueue Implementation
// ============================================================================

void LightingQueue::enqueue(LightingUpdate update) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Consolidate by position - newer updates overwrite older
        pending_[update.pos] = update;
    }
    cv_.notify_one();
}

std::vector<LightingUpdate> LightingQueue::dequeueBatch(size_t maxCount) {
    std::unique_lock<std::mutex> lock(mutex_);

    // Wait until we have updates or are stopped
    cv_.wait(lock, [this] {
        return !pending_.empty() || stopped_.load(std::memory_order_acquire);
    });

    if (stopped_.load(std::memory_order_acquire) && pending_.empty()) {
        return {};  // Stopped and no more work
    }

    return tryDequeueBatchUnlocked(maxCount);
}

std::vector<LightingUpdate> LightingQueue::tryDequeueBatch(size_t maxCount) {
    std::lock_guard<std::mutex> lock(mutex_);
    return tryDequeueBatchUnlocked(maxCount);
}

std::vector<LightingUpdate> LightingQueue::tryDequeueBatchUnlocked(size_t maxCount) {
    std::vector<LightingUpdate> batch;
    batch.reserve(std::min(maxCount, pending_.size()));

    auto it = pending_.begin();
    while (it != pending_.end() && batch.size() < maxCount) {
        batch.push_back(it->second);
        it = pending_.erase(it);
    }

    return batch;
}

bool LightingQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.empty();
}

size_t LightingQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

void LightingQueue::stop() {
    stopped_.store(true, std::memory_order_release);
    cv_.notify_all();
}

void LightingQueue::reset() {
    stopped_.store(false, std::memory_order_release);
}

// ============================================================================
// LightEngine Construction/Destruction
// ============================================================================

LightEngine::LightEngine(World& world) : world_(world) {}

LightEngine::~LightEngine() {
    stop();
}

// ============================================================================
// Position Helpers
// ============================================================================

ChunkPos LightEngine::toChunkPos(const BlockPos& pos) {
    // Floor division for negative coordinates
    auto floorDiv = [](int32_t a, int32_t b) -> int32_t {
        return a >= 0 ? a / b : (a - b + 1) / b;
    };

    return ChunkPos{
        floorDiv(pos.x, 16),
        floorDiv(pos.y, 16),
        floorDiv(pos.z, 16)
    };
}

int32_t LightEngine::toLocalIndex(const BlockPos& pos) {
    // Proper modulo for negative numbers
    int32_t localX = pos.x & 15;
    int32_t localY = pos.y & 15;
    int32_t localZ = pos.z & 15;
    return localY * 256 + localZ * 16 + localX;
}

// ============================================================================
// SubChunk Access for Light
// ============================================================================

SubChunk* LightEngine::getSubChunkForLight(const ChunkPos& chunkPos) {
    return world_.getSubChunk(chunkPos);
}

const SubChunk* LightEngine::getSubChunkForLight(const ChunkPos& chunkPos) const {
    return world_.getSubChunk(chunkPos);
}

SubChunk* LightEngine::getOrCreateSubChunkForLight(const ChunkPos& chunkPos) {
    // First try to get existing subchunk
    SubChunk* subChunk = world_.getSubChunk(chunkPos);
    if (subChunk) {
        return subChunk;
    }

    // Need to create it - get or create the column first
    ColumnPos colPos{chunkPos.x, chunkPos.z};
    ChunkColumn& column = world_.getOrCreateColumn(colPos);

    // Get or create the subchunk within the column
    return &column.getOrCreateSubChunk(chunkPos.y);
}

// ============================================================================
// Light Access
// ============================================================================

uint8_t LightEngine::getSkyLight(const BlockPos& pos) const {
    ChunkPos chunkPos = toChunkPos(pos);
    const SubChunk* subChunk = getSubChunkForLight(chunkPos);
    if (!subChunk) {
        // No subchunk - could be above world (full sky light) or unloaded
        return 0;
    }
    return subChunk->getSkyLight(toLocalIndex(pos));
}

uint8_t LightEngine::getBlockLight(const BlockPos& pos) const {
    ChunkPos chunkPos = toChunkPos(pos);
    const SubChunk* subChunk = getSubChunkForLight(chunkPos);
    if (!subChunk) {
        return 0;
    }
    return subChunk->getBlockLight(toLocalIndex(pos));
}

uint8_t LightEngine::getCombinedLight(const BlockPos& pos) const {
    ChunkPos chunkPos = toChunkPos(pos);
    const SubChunk* subChunk = getSubChunkForLight(chunkPos);
    if (!subChunk) {
        return 0;
    }
    return subChunk->getCombinedLight(toLocalIndex(pos));
}

// ============================================================================
// Block Type Queries
// ============================================================================

uint8_t LightEngine::getAttenuation(BlockTypeId blockType) const {
    if (blockType.isAir()) {
        return 1;  // Air has minimal attenuation
    }

    // Check for custom attenuation callback
    auto it = attenuationCallbacks_.find(blockType);
    if (it != attenuationCallbacks_.end()) {
        // Custom callback handles attenuation differently
        // For standard lookup, use the block type's base attenuation
    }

    const BlockType& type = BlockRegistry::global().getType(blockType);
    return type.lightAttenuation();
}

bool LightEngine::blocksSkyLight(BlockTypeId blockType) const {
    if (blockType.isAir()) {
        return false;
    }
    const BlockType& type = BlockRegistry::global().getType(blockType);
    return type.blocksSkyLight();
}

uint8_t LightEngine::getLightEmission(BlockTypeId blockType) const {
    if (blockType.isAir()) {
        return 0;
    }
    const BlockType& type = BlockRegistry::global().getType(blockType);
    return type.lightEmission();
}

// ============================================================================
// Block Light Updates
// ============================================================================

void LightEngine::onBlockPlaced(const BlockPos& pos, BlockTypeId oldType, BlockTypeId newType) {
    // Get light emissions
    uint8_t oldEmission = getLightEmission(oldType);
    uint8_t newEmission = getLightEmission(newType);

    // If old block emitted light, remove it
    if (oldEmission > 0) {
        uint8_t currentLight = getBlockLight(pos);
        if (currentLight > 0) {
            removeBlockLight(pos, currentLight);
        }
    }

    // If new block emits light, propagate it
    if (newEmission > 0) {
        propagateBlockLight(pos, newEmission);
    }

    // If new block is opaque, it may block existing light
    uint8_t newAttenuation = getAttenuation(newType);
    if (newAttenuation >= 15) {
        // Fully opaque - block all light passing through
        uint8_t currentLight = getBlockLight(pos);
        if (currentLight > 0 && newEmission == 0) {
            // Use BFS removal to properly clear light that propagated through this position
            // This removes the light here AND all dependent light beyond, then re-propagates
            // from any light sources found at the boundary
            removeBlockLight(pos, currentLight);
        }
    }

    // Handle sky light blocking
    if (blocksSkyLight(newType) && !blocksSkyLight(oldType)) {
        // New block now blocks sky light - update column below
        uint8_t currentSkyLight = getSkyLight(pos);
        if (currentSkyLight > 0) {
            // Remove sky light at this position and below
            ChunkPos chunkPos = toChunkPos(pos);
            SubChunk* subChunk = getOrCreateSubChunkForLight(chunkPos);
            if (subChunk) {
                subChunk->setSkyLight(toLocalIndex(pos), 0);
                recordAffectedChunk(chunkPos);
            }

            // Propagate darkness down
            for (int32_t y = pos.y - 1; y >= pos.y - 16; --y) {
                BlockPos belowPos{pos.x, y, pos.z};
                uint8_t belowLight = getSkyLight(belowPos);
                if (belowLight == 0) break;

                BlockTypeId belowBlock = world_.getBlock(belowPos);
                if (blocksSkyLight(belowBlock)) break;

                ChunkPos belowChunkPos = toChunkPos(belowPos);
                SubChunk* belowSubChunk = getOrCreateSubChunkForLight(belowChunkPos);
                if (belowSubChunk) {
                    belowSubChunk->setSkyLight(toLocalIndex(belowPos), 0);
                    recordAffectedChunk(belowChunkPos);
                }
            }
        }
    }
}

void LightEngine::onBlockRemoved(const BlockPos& pos, BlockTypeId oldType) {
    onBlockPlaced(pos, oldType, AIR_BLOCK_TYPE);

    // If this block was blocking light, light can now flow through
    uint8_t oldAttenuation = getAttenuation(oldType);
    if (oldAttenuation >= 15) {
        // Was fully opaque - find the highest light from neighbors and propagate from here
        static const std::array<BlockPos, 6> offsets = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        }};

        // Find the maximum light from all neighbors (minus attenuation)
        uint8_t maxNeighborLight = 0;
        for (const auto& offset : offsets) {
            BlockPos neighborPos{pos.x + offset.x, pos.y + offset.y, pos.z + offset.z};
            uint8_t neighborLight = getBlockLight(neighborPos);
            if (neighborLight > 1) {
                // Light entering this position would be neighborLight - 1 (air attenuation)
                uint8_t incomingLight = neighborLight - 1;
                if (incomingLight > maxNeighborLight) {
                    maxNeighborLight = incomingLight;
                }
            }
        }

        // If there's light that should now flow through, propagate from this position
        if (maxNeighborLight > 0) {
            propagateBlockLight(pos, maxNeighborLight);
        }
    }

    // If this block was blocking sky light, re-propagate from above
    if (blocksSkyLight(oldType)) {
        BlockPos abovePos{pos.x, pos.y + 1, pos.z};
        uint8_t aboveSkyLight = getSkyLight(abovePos);
        if (aboveSkyLight > 0) {
            propagateSkyLight(pos, aboveSkyLight > 1 ? aboveSkyLight - 1 : 0);
        }
    }
}

void LightEngine::propagateBlockLight(const BlockPos& pos, uint8_t lightLevel) {
    if (lightLevel == 0) return;

    SubChunk* subChunk = getOrCreateSubChunkForLight(toChunkPos(pos));
    if (!subChunk) return;

    int32_t idx = toLocalIndex(pos);

    // Only propagate if this is higher than existing light
    if (subChunk->getBlockLight(idx) >= lightLevel) {
        return;
    }

    subChunk->setBlockLight(idx, lightLevel);
    recordAffectedChunk(toChunkPos(pos));
    propagateLightBFS(pos, lightLevel, false);
}

void LightEngine::removeBlockLight(const BlockPos& pos, uint8_t oldLevel) {
    if (oldLevel == 0) return;
    removeLightBFS(pos, oldLevel, false);
}

// ============================================================================
// Sky Light Updates
// ============================================================================

void LightEngine::initializeSkyLight(const ColumnPos& columnPos) {
    ChunkColumn* column = world_.getColumn(columnPos);
    if (!column) return;

    // Ensure heightmap is up to date
    if (column->heightmapDirty()) {
        column->recalculateHeightmap();
    }

    // Get Y bounds
    auto bounds = column->getYBounds();
    if (!bounds) {
        // Empty column - fill all with max sky light (above ground)
        return;
    }

    int32_t minChunkY = bounds->first;
    int32_t maxChunkY = bounds->second;

    // For each X,Z in the column
    for (int32_t localZ = 0; localZ < 16; ++localZ) {
        for (int32_t localX = 0; localX < 16; ++localX) {
            int32_t height = column->getHeight(localX, localZ);
            int32_t worldX = columnPos.x * 16 + localX;
            int32_t worldZ = columnPos.z * 16 + localZ;

            // Above heightmap: full sky light
            // At and below: propagate from above

            // Start with full light above heightmap
            for (int32_t chunkY = maxChunkY; chunkY >= minChunkY; --chunkY) {
                ChunkPos chunkPos{columnPos.x, chunkY, columnPos.z};
                SubChunk* subChunk = getOrCreateSubChunkForLight(chunkPos);
                if (!subChunk) continue;

                for (int32_t localY = 15; localY >= 0; --localY) {
                    int32_t worldY = chunkY * 16 + localY;
                    int32_t idx = localY * 256 + localZ * 16 + localX;

                    if (height == std::numeric_limits<int32_t>::min() || worldY >= height) {
                        // Above heightmap - full sky light
                        subChunk->setSkyLight(idx, SubChunk::MAX_LIGHT);
                    } else {
                        // Below heightmap - need to propagate
                        // For now, set to 0 and let BFS handle it
                        subChunk->setSkyLight(idx, 0);
                    }
                }
            }

            // Now propagate sky light horizontally at the surface
            if (height != std::numeric_limits<int32_t>::min()) {
                BlockPos surfacePos{worldX, height, worldZ};
                propagateSkyLight(surfacePos, SubChunk::MAX_LIGHT);
            }
        }
    }
}

void LightEngine::updateSkyLight(const BlockPos& pos, int32_t oldHeight, int32_t newHeight) {
    if (oldHeight == newHeight) return;

    if (newHeight > oldHeight) {
        // Height increased - remove sky light from newly shaded area
        for (int32_t y = oldHeight; y < newHeight; ++y) {
            BlockPos shadePos{pos.x, y, pos.z};
            uint8_t currentLight = getSkyLight(shadePos);
            if (currentLight > 0) {
                removeLightBFS(shadePos, currentLight, true);
            }
        }
    } else {
        // Height decreased - add sky light to newly exposed area
        for (int32_t y = newHeight; y < oldHeight; ++y) {
            BlockPos exposePos{pos.x, y, pos.z};
            propagateSkyLight(exposePos, SubChunk::MAX_LIGHT);
        }
    }
}

void LightEngine::propagateSkyLight(const BlockPos& pos, uint8_t lightLevel) {
    if (lightLevel == 0) return;

    SubChunk* subChunk = getOrCreateSubChunkForLight(toChunkPos(pos));
    if (!subChunk) return;

    int32_t idx = toLocalIndex(pos);

    // Only propagate if this is higher than existing light
    if (subChunk->getSkyLight(idx) >= lightLevel) {
        return;
    }

    subChunk->setSkyLight(idx, lightLevel);
    recordAffectedChunk(toChunkPos(pos));
    propagateLightBFS(pos, lightLevel, true);
}

// ============================================================================
// BFS Light Propagation
// ============================================================================

void LightEngine::propagateLightBFS(const BlockPos& start, uint8_t startLevel, bool isSkyLight) {
    if (startLevel == 0) return;

    // Use priority queue to process higher light levels first
    std::priority_queue<LightNode> queue;
    queue.push({start, startLevel});

    static const std::array<BlockPos, 6> offsets = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};

    int32_t processed = 0;

    while (!queue.empty() && processed < maxPropagationDistance_) {
        LightNode node = queue.top();
        queue.pop();
        ++processed;

        // Get current light at this position
        ChunkPos chunkPos = toChunkPos(node.pos);
        SubChunk* subChunk = getSubChunkForLight(chunkPos);
        if (!subChunk) continue;

        int32_t idx = toLocalIndex(node.pos);
        uint8_t currentLight = isSkyLight ? subChunk->getSkyLight(idx) : subChunk->getBlockLight(idx);

        // Skip if light decreased since we queued this node
        if (currentLight < node.light) {
            continue;
        }

        // Propagate to neighbors
        for (const auto& offset : offsets) {
            BlockPos neighborPos{
                node.pos.x + offset.x,
                node.pos.y + offset.y,
                node.pos.z + offset.z
            };

            // Get block at neighbor position
            BlockTypeId neighborBlock = world_.getBlock(neighborPos);

            // Calculate light attenuation
            uint8_t attenuation = getAttenuation(neighborBlock);

            // For sky light going straight down through air, no attenuation
            if (isSkyLight && offset.y == -1 && neighborBlock.isAir()) {
                attenuation = 0;
            }

            // Calculate new light level
            int32_t newLight = static_cast<int32_t>(currentLight) - attenuation;
            if (newLight <= 0) continue;

            uint8_t newLightLevel = static_cast<uint8_t>(newLight);

            // Get or create subchunk for neighbor
            ChunkPos neighborChunk = toChunkPos(neighborPos);
            SubChunk* neighborSubChunk = getOrCreateSubChunkForLight(neighborChunk);
            if (!neighborSubChunk) continue;

            int32_t neighborIdx = toLocalIndex(neighborPos);

            uint8_t neighborLight = isSkyLight ?
                neighborSubChunk->getSkyLight(neighborIdx) :
                neighborSubChunk->getBlockLight(neighborIdx);

            // Only update if we're increasing the light level
            if (newLightLevel > neighborLight) {
                if (isSkyLight) {
                    neighborSubChunk->setSkyLight(neighborIdx, newLightLevel);
                } else {
                    neighborSubChunk->setBlockLight(neighborIdx, newLightLevel);
                }
                recordAffectedChunk(neighborChunk);
                queue.push({neighborPos, newLightLevel});
            }
        }
    }
}

void LightEngine::removeLightBFS(const BlockPos& start, uint8_t startLevel, bool isSkyLight) {
    if (startLevel == 0) return;

    // Two-phase algorithm:
    // 1. BFS to find all affected blocks and set them to 0
    // 2. Re-propagate from light sources at the boundary

    struct RemovalNode {
        BlockPos pos;
        uint8_t oldLight;
    };

    std::queue<RemovalNode> removalQueue;
    std::vector<LightNode> repropagateQueue;

    removalQueue.push({start, startLevel});

    static const std::array<BlockPos, 6> offsets = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};

    while (!removalQueue.empty()) {
        RemovalNode node = removalQueue.front();
        removalQueue.pop();

        for (const auto& offset : offsets) {
            BlockPos neighborPos{
                node.pos.x + offset.x,
                node.pos.y + offset.y,
                node.pos.z + offset.z
            };

            ChunkPos neighborChunk = toChunkPos(neighborPos);
            SubChunk* neighborSubChunk = getSubChunkForLight(neighborChunk);
            if (!neighborSubChunk) continue;

            int32_t neighborIdx = toLocalIndex(neighborPos);
            uint8_t neighborLight = isSkyLight ?
                neighborSubChunk->getSkyLight(neighborIdx) :
                neighborSubChunk->getBlockLight(neighborIdx);

            if (neighborLight == 0) continue;

            if (neighborLight < node.oldLight) {
                // This light was coming from the removed source
                if (isSkyLight) {
                    neighborSubChunk->setSkyLight(neighborIdx, 0);
                } else {
                    neighborSubChunk->setBlockLight(neighborIdx, 0);
                }
                recordAffectedChunk(neighborChunk);
                removalQueue.push({neighborPos, neighborLight});
            } else {
                // This light is from another source - need to re-propagate
                repropagateQueue.push_back({neighborPos, neighborLight});
            }
        }
    }

    // Clear light at the starting position
    ChunkPos startChunk = toChunkPos(start);
    SubChunk* startSubChunk = getSubChunkForLight(startChunk);
    if (startSubChunk) {
        if (isSkyLight) {
            startSubChunk->setSkyLight(toLocalIndex(start), 0);
        } else {
            startSubChunk->setBlockLight(toLocalIndex(start), 0);
        }
        recordAffectedChunk(startChunk);
    }

    // Re-propagate from boundary sources
    for (const auto& node : repropagateQueue) {
        propagateLightBFS(node.pos, node.light, isSkyLight);
    }
}

// ============================================================================
// Batch Operations
// ============================================================================

void LightEngine::recalculateSubChunk(const ChunkPos& chunkPos) {
    SubChunk* subChunk = world_.getSubChunk(chunkPos);
    if (!subChunk) {
        return;
    }

    subChunk->clearLight();

    // Find all light-emitting blocks and propagate
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) {
                BlockTypeId block = subChunk->getBlock(x, y, z);
                uint8_t emission = getLightEmission(block);
                if (emission > 0) {
                    BlockPos worldPos{
                        chunkPos.x * 16 + x,
                        chunkPos.y * 16 + y,
                        chunkPos.z * 16 + z
                    };
                    propagateBlockLight(worldPos, emission);
                }
            }
        }
    }
}

void LightEngine::recalculateColumn(const ColumnPos& columnPos) {
    ChunkColumn* column = world_.getColumn(columnPos);
    if (!column) return;

    // Recalculate heightmap
    column->recalculateHeightmap();

    // Get Y bounds
    auto bounds = column->getYBounds();
    if (!bounds) return;

    // Clear existing light data for this column
    for (int32_t chunkY = bounds->first; chunkY <= bounds->second; ++chunkY) {
        SubChunk* subChunk = column->getSubChunk(chunkY);
        if (subChunk) {
            subChunk->clearLight();
        }
    }

    // Initialize sky light
    initializeSkyLight(columnPos);

    // Recalculate block light for each subchunk
    for (int32_t chunkY = bounds->first; chunkY <= bounds->second; ++chunkY) {
        ChunkPos chunkPos{columnPos.x, chunkY, columnPos.z};
        recalculateSubChunk(chunkPos);
    }
}

void LightEngine::markDirty(const BlockPos& pos) {
    pendingUpdates_.insert(pos);
}

void LightEngine::processUpdates() {
    // Process pending updates
    for (const BlockPos& pos : pendingUpdates_) {
        // Re-propagate light from this position if it's a light source
        BlockTypeId block = world_.getBlock(pos);
        uint8_t emission = getLightEmission(block);
        if (emission > 0) {
            propagateBlockLight(pos, emission);
        }
    }
    pendingUpdates_.clear();
}

// ============================================================================
// Custom Attenuation
// ============================================================================

void LightEngine::setAttenuationCallback(BlockTypeId blockType, LightAttenuationCallback callback) {
    attenuationCallbacks_[blockType] = std::move(callback);
}

void LightEngine::clearAttenuationCallback(BlockTypeId blockType) {
    attenuationCallbacks_.erase(blockType);
}

// ============================================================================
// Async Lighting Thread
// ============================================================================

void LightEngine::enqueue(LightingUpdate update) {
    queue_.enqueue(std::move(update));
}

void LightEngine::start() {
    if (running_.load(std::memory_order_acquire)) {
        return;  // Already running
    }

    running_.store(true, std::memory_order_release);
    queue_.reset();
    thread_ = std::thread(&LightEngine::lightingThreadLoop, this);
}

void LightEngine::stop() {
    if (!running_.load(std::memory_order_acquire)) {
        return;  // Not running
    }

    running_.store(false, std::memory_order_release);
    queue_.stop();

    if (thread_.joinable()) {
        thread_.join();
    }
}

void LightEngine::lightingThreadLoop() {
    while (running_.load(std::memory_order_acquire)) {
        // Dequeue a batch of updates (blocks if queue is empty)
        auto batch = queue_.dequeueBatch(batchSize_);

        if (batch.empty()) {
            // Queue was stopped
            break;
        }

        // Clear affected chunks set before processing batch
        batchAffectedChunks_.clear();

        // Process each update in the batch
        for (const auto& update : batch) {
            processLightingUpdate(update);
        }

        // After processing entire batch, push mesh rebuild requests for all affected chunks
        flushAffectedChunks();
    }
}

void LightEngine::recordAffectedChunk(const ChunkPos& pos) {
    batchAffectedChunks_.insert(pos);
}

void LightEngine::flushAffectedChunks() {
    if (!meshRebuildQueue_ || batchAffectedChunks_.empty()) {
        return;
    }

    for (const auto& chunkPos : batchAffectedChunks_) {
        SubChunk* subChunk = getSubChunkForLight(chunkPos);
        if (subChunk) {
            // Push rebuild request - MeshRebuildQueue will coalesce duplicates
            meshRebuildQueue_->push(chunkPos, MeshRebuildRequest::normal(
                subChunk->blockVersion(),
                subChunk->lightVersion()
            ));
        }
    }

    batchAffectedChunks_.clear();
}

void LightEngine::processLightingUpdate(const LightingUpdate& update) {
    // Quick check: opaque non-emitter → opaque non-emitter is no-op
    uint8_t oldEmission = getLightEmission(update.oldType);
    uint8_t newEmission = getLightEmission(update.newType);
    uint8_t oldAttenuation = getAttenuation(update.oldType);
    uint8_t newAttenuation = getAttenuation(update.newType);

    bool lightingChanged = true;
    if (oldAttenuation >= 15 && newAttenuation >= 15 &&
        oldEmission == 0 && newEmission == 0) {
        lightingChanged = false;  // No light change possible
    }

    if (lightingChanged) {
        // Check if an opaque block was replaced with a transparent one
        // This requires re-propagation from neighbors (onBlockRemoved logic)
        if (oldAttenuation >= 15 && newAttenuation < 15) {
            // Opaque → transparent: use onBlockRemoved which re-propagates from neighbors
            onBlockRemoved(update.pos, update.oldType);
        } else {
            // Other cases: use onBlockPlaced
            // Light propagation will call recordAffectedChunk for each modified chunk
            onBlockPlaced(update.pos, update.oldType, update.newType);
        }
    }

    // If triggerMeshRebuild is set, ensure this chunk is in the affected set
    // even if no lighting changed (e.g., block change that doesn't affect light)
    if (update.triggerMeshRebuild) {
        recordAffectedChunk(toChunkPos(update.pos));
    }

    // Note: Mesh rebuild requests are now batched and pushed by flushAffectedChunks()
    // at the end of each batch, not per-update
}

}  // namespace finevox
