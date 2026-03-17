#include "finevox/core/light_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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

std::vector<LightingUpdate> LightingQueue::dequeueBatch(size_t maxCount, bool* alarmFired) {
    std::unique_lock<std::mutex> lock(mutex_);

    if (alarmFired) *alarmFired = false;

    if (alarmSet_) {
        // Wait until updates, stopped, or alarm fires
        cv_.wait_until(lock, alarmTime_, [this] {
            return !pending_.empty() || stopped_.load(std::memory_order_acquire);
        });

        // Check if alarm fired (time expired without data or stop)
        if (pending_.empty() && !stopped_.load(std::memory_order_acquire)) {
            if (alarmFired) *alarmFired = true;
            alarmSet_ = false;
            return {};
        }
    } else {
        // No alarm — wait indefinitely for updates or stop
        cv_.wait(lock, [this] {
            return !pending_.empty() || stopped_.load(std::memory_order_acquire);
        });
    }

    if (stopped_.load(std::memory_order_acquire) && pending_.empty()) {
        return {};  // Stopped and no more work
    }

    return tryDequeueBatchUnlocked(maxCount);
}

void LightingQueue::setAlarm(std::chrono::steady_clock::time_point tp) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        alarmTime_ = tp;
        alarmSet_ = true;
    }
    cv_.notify_all();
}

void LightingQueue::clearAlarm() {
    std::lock_guard<std::mutex> lock(mutex_);
    alarmSet_ = false;
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
// Light Provider Management
// ============================================================================

void LightEngine::addLightProvider(std::shared_ptr<LightProvider> provider) {
    lightProviders_.push_back(std::move(provider));
    std::sort(lightProviders_.begin(), lightProviders_.end(),
        [](const std::shared_ptr<LightProvider>& a,
           const std::shared_ptr<LightProvider>& b) {
            return a->priority() < b->priority();
        });
}

void LightEngine::removeLightProvider(const std::shared_ptr<LightProvider>& provider) {
    auto it = std::find(lightProviders_.begin(), lightProviders_.end(), provider);
    if (it != lightProviders_.end()) {
        lightProviders_.erase(it);
    }
}

uint8_t LightEngine::queryCombinedEmission(BlockTypeId blockType) const {
    if (lightProviders_.empty()) {
        // Fallback: use existing internal method
        return getLightEmission(blockType);
    }
    uint8_t maxEmission = 0;
    for (const auto& provider : lightProviders_) {
        maxEmission = std::max(maxEmission, provider->getEmission(blockType));
    }
    return maxEmission;
}

uint8_t LightEngine::queryCombinedAttenuation(BlockTypeId blockType) const {
    if (lightProviders_.empty()) {
        // Fallback: use existing internal method
        return getAttenuation(blockType);
    }
    int total = 0;
    for (const auto& provider : lightProviders_) {
        total += provider->getAttenuation(blockType);
    }
    return static_cast<uint8_t>(std::min(total, 15));
}

float LightEngine::queryCombinedLogAttenuation(const BlockCoord& pos) const {
    float combined = 0.0f;
    for (const auto& provider : lightProviders_) {
        float logAtten = provider->getLogAttenuation(pos);
        if (logAtten > 0.0f) {
            // Accumulate multiplicatively: first non-zero sets base,
            // subsequent ones multiply in
            if (combined == 0.0f) {
                combined = logAtten;
            } else {
                combined *= logAtten;
            }
        }
    }
    return combined;
}

bool LightEngine::queryCombinedBlocksSkyLight(BlockTypeId blockType) const {
    if (lightProviders_.empty()) {
        // Fallback: use existing internal method
        return blocksSkyLight(blockType);
    }
    for (const auto& provider : lightProviders_) {
        if (provider->blocksSkyLight(blockType)) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Position Helpers
// ============================================================================

ChunkPos LightEngine::toChunkPos(const BlockCoord& pos) {
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

int32_t LightEngine::toLocalIndex(const BlockCoord& pos) {
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

uint8_t LightEngine::getSkyLight(const BlockCoord& pos) const {
    ChunkPos chunkPos = toChunkPos(pos);
    const SubChunk* subChunk = getSubChunkForLight(chunkPos);
    if (!subChunk) {
        // No subchunk - could be above world (full sky light) or unloaded
        return 0;
    }
    return subChunk->getSkyLight(toLocalIndex(pos));
}

uint8_t LightEngine::getBlockLight(const BlockCoord& pos) const {
    ChunkPos chunkPos = toChunkPos(pos);
    const SubChunk* subChunk = getSubChunkForLight(chunkPos);
    if (!subChunk) {
        return 0;
    }
    return subChunk->getBlockLight(toLocalIndex(pos));
}

uint8_t LightEngine::getCombinedLight(const BlockCoord& pos) const {
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

uint8_t LightEngine::getAttenuationWithFluid(const BlockCoord& pos, BlockTypeId blockType,
                                              float& outFluidMult) const {
    uint8_t blockAtten = getAttenuation(blockType);
    outFluidMult = 0.0f;  // No logarithmic fluid by default

    FluidTypeId fluidType = world_.getFluid(pos);
    if (fluidType.isEmpty()) {
        return blockAtten;
    }

    const FluidType* ft = FluidRegistry::global().getType(fluidType);
    if (!ft) {
        return blockAtten;
    }

    if (ft->customAttenuation) {
        // Logarithmic attenuation: caller applies multiplicatively after block atten
        outFluidMult = ft->attenuationBase;
        return blockAtten;
    }

    // Standard fixed attenuation: combine block + fluid (capped at 15)
    return static_cast<uint8_t>(std::min(static_cast<int>(blockAtten) + static_cast<int>(ft->lightAttenuation), 15));
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

void LightEngine::onBlockPlaced(const BlockCoord& pos, BlockTypeId oldType, BlockTypeId newType) {
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
                recordAffectedChunk(pos);
            }

            // Propagate darkness down
            for (int32_t y = pos.y - 1; y >= pos.y - 16; --y) {
                BlockCoord belowPos{pos.x, y, pos.z};
                uint8_t belowLight = getSkyLight(belowPos);
                if (belowLight == 0) break;

                BlockTypeId belowBlock = world_.getBlock(belowPos);
                if (blocksSkyLight(belowBlock)) break;

                ChunkPos belowChunkPos = toChunkPos(belowPos);
                SubChunk* belowSubChunk = getOrCreateSubChunkForLight(belowChunkPos);
                if (belowSubChunk) {
                    belowSubChunk->setSkyLight(toLocalIndex(belowPos), 0);
                    recordAffectedChunk(belowPos);
                }
            }
        }
    }
}

void LightEngine::onBlockRemoved(const BlockCoord& pos, BlockTypeId oldType) {
    onBlockPlaced(pos, oldType, AIR_BLOCK_TYPE);

    // If this block was blocking light, light can now flow through
    uint8_t oldAttenuation = getAttenuation(oldType);
    if (oldAttenuation >= 15) {
        // Was fully opaque - find the highest light from neighbors and propagate from here
        static const std::array<BlockCoord, 6> offsets = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        }};

        // Find the maximum light from all neighbors (minus attenuation)
        uint8_t maxNeighborLight = 0;
        for (const auto& offset : offsets) {
            BlockCoord neighborPos{pos.x + offset.x, pos.y + offset.y, pos.z + offset.z};
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
        BlockCoord abovePos{pos.x, pos.y + 1, pos.z};
        uint8_t aboveSkyLight = getSkyLight(abovePos);
        if (aboveSkyLight > 0) {
            propagateSkyLight(pos, aboveSkyLight > 1 ? aboveSkyLight - 1 : 0);
        }
    }
}

void LightEngine::propagateBlockLight(const BlockCoord& pos, uint8_t lightLevel) {
    if (lightLevel == 0) return;

    SubChunk* subChunk = getOrCreateSubChunkForLight(toChunkPos(pos));
    if (!subChunk) return;

    int32_t idx = toLocalIndex(pos);

    // Only propagate if this is higher than existing light
    if (subChunk->getBlockLight(idx) >= lightLevel) {
        return;
    }

    subChunk->setBlockLight(idx, lightLevel);
    recordAffectedChunk(pos);
    propagateLightBFS(pos, lightLevel, false);
}

void LightEngine::removeBlockLight(const BlockCoord& pos, uint8_t oldLevel) {
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
                BlockCoord surfacePos{worldX, height, worldZ};
                propagateSkyLight(surfacePos, SubChunk::MAX_LIGHT);
            }
        }
    }
}

void LightEngine::updateSkyLight(const BlockCoord& pos, int32_t oldHeight, int32_t newHeight) {
    if (oldHeight == newHeight) return;

    if (newHeight > oldHeight) {
        // Height increased - remove sky light from newly shaded area
        for (int32_t y = oldHeight; y < newHeight; ++y) {
            BlockCoord shadePos{pos.x, y, pos.z};
            uint8_t currentLight = getSkyLight(shadePos);
            if (currentLight > 0) {
                removeLightBFS(shadePos, currentLight, true);
            }
        }
    } else {
        // Height decreased - add sky light to newly exposed area
        for (int32_t y = newHeight; y < oldHeight; ++y) {
            BlockCoord exposePos{pos.x, y, pos.z};
            propagateSkyLight(exposePos, SubChunk::MAX_LIGHT);
        }
    }
}

void LightEngine::propagateSkyLight(const BlockCoord& pos, uint8_t lightLevel) {
    if (lightLevel == 0) return;

    SubChunk* subChunk = getOrCreateSubChunkForLight(toChunkPos(pos));
    if (!subChunk) return;

    int32_t idx = toLocalIndex(pos);

    // Only propagate if this is higher than existing light
    if (subChunk->getSkyLight(idx) >= lightLevel) {
        return;
    }

    subChunk->setSkyLight(idx, lightLevel);
    recordAffectedChunk(pos);
    propagateLightBFS(pos, lightLevel, true);
}

// ============================================================================
// BFS Light Propagation
// ============================================================================

void LightEngine::propagateLightBFS(const BlockCoord& start, uint8_t startLevel, bool isSkyLight) {
    if (startLevel == 0) return;

    // Use priority queue to process higher light levels first
    std::priority_queue<LightNode> queue;
    queue.push({start, startLevel});

    static const std::array<BlockCoord, 6> offsets = {{
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
            BlockCoord neighborPos{
                node.pos.x + offset.x,
                node.pos.y + offset.y,
                node.pos.z + offset.z
            };

            // Get block at neighbor position
            BlockTypeId neighborBlock = world_.getBlock(neighborPos);

            // Calculate light attenuation (block + fluid combined)
            float fluidMult = 0.0f;
            uint8_t attenuation = getAttenuationWithFluid(neighborPos, neighborBlock, fluidMult);

            // For sky light going straight down through air with no fluid, no attenuation
            if (isSkyLight && offset.y == -1 && neighborBlock.isAir() && fluidMult == 0.0f
                && attenuation == 1) {
                attenuation = 0;
            }

            // Calculate new light level
            int32_t newLight;
            if (fluidMult > 0.0f) {
                // Logarithmic fluid attenuation: subtract block atten, then multiply
                int32_t afterBlock = static_cast<int32_t>(currentLight) - getAttenuation(neighborBlock);
                if (afterBlock <= 0) continue;
                newLight = static_cast<int32_t>(std::floor(afterBlock * fluidMult));
            } else {
                newLight = static_cast<int32_t>(currentLight) - attenuation;
            }
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
                recordAffectedChunk(neighborPos);
                queue.push({neighborPos, newLightLevel});
            }
        }
    }
}

void LightEngine::removeLightBFS(const BlockCoord& start, uint8_t startLevel, bool isSkyLight) {
    if (startLevel == 0) return;

    // Two-phase algorithm:
    // 1. BFS to find all affected blocks and set them to 0
    // 2. Re-propagate from light sources at the boundary

    struct RemovalNode {
        BlockCoord pos;
        uint8_t oldLight;
    };

    std::queue<RemovalNode> removalQueue;
    std::vector<LightNode> repropagateQueue;

    removalQueue.push({start, startLevel});

    static const std::array<BlockCoord, 6> offsets = {{
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    }};

    while (!removalQueue.empty()) {
        RemovalNode node = removalQueue.front();
        removalQueue.pop();

        for (const auto& offset : offsets) {
            BlockCoord neighborPos{
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
                recordAffectedChunk(neighborPos);
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
        recordAffectedChunk(start);
    }

    // Re-propagate from boundary sources
    for (const auto& node : repropagateQueue) {
        propagateLightBFS(node.pos, node.light, isSkyLight);
    }
}

// ============================================================================
// Fluid Light Updates
// ============================================================================

void LightEngine::onFluidPlaced(const BlockCoord& pos, FluidTypeId fluidTypeId) {
    const FluidType* ft = FluidRegistry::global().getType(fluidTypeId);
    if (!ft) return;

    // If fluid emits light, propagate it
    if (ft->lightEmission > 0) {
        propagateBlockLight(pos, ft->lightEmission);
    }

    // If fluid attenuates light more than air (standard atten > 1, or custom),
    // existing light passing through needs recalculation
    if (ft->lightAttenuation > 1 || ft->customAttenuation) {
        uint8_t currentBlock = getBlockLight(pos);
        if (currentBlock > 0 && ft->lightEmission == 0) {
            removeBlockLight(pos, currentBlock);
        }
        uint8_t currentSky = getSkyLight(pos);
        if (currentSky > 0) {
            removeLightBFS(pos, currentSky, true);
            // Re-propagate sky from neighbors
            static const std::array<BlockCoord, 6> offsets = {{
                {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
            }};
            for (const auto& offset : offsets) {
                BlockCoord neighbor{pos.x + offset.x, pos.y + offset.y, pos.z + offset.z};
                uint8_t neighborSky = getSkyLight(neighbor);
                if (neighborSky > 0) {
                    propagateSkyLight(neighbor, neighborSky);
                }
            }
        }
    }

    recordAffectedChunk(pos);
}

void LightEngine::onFluidRemoved(const BlockCoord& pos, FluidTypeId fluidTypeId) {
    const FluidType* ft = FluidRegistry::global().getType(fluidTypeId);
    if (!ft) return;

    // If fluid was emitting light, remove it
    if (ft->lightEmission > 0) {
        uint8_t currentLight = getBlockLight(pos);
        if (currentLight > 0) {
            removeBlockLight(pos, currentLight);
        }
    }

    // If fluid was attenuating light, remove stale light and re-propagate
    if (ft->lightAttenuation > 1 || ft->customAttenuation) {
        // Remove existing (too-low) light at this position so neighbors can refill it
        uint8_t currentBlock = getBlockLight(pos);
        if (currentBlock > 0) {
            removeBlockLight(pos, currentBlock);
        }
        uint8_t currentSky = getSkyLight(pos);
        if (currentSky > 0) {
            removeLightBFS(pos, currentSky, true);
        }

        // Re-propagate from all neighbors (they now push through with lower attenuation)
        static const std::array<BlockCoord, 6> offsets = {{
            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
        }};
        for (const auto& offset : offsets) {
            BlockCoord neighbor{pos.x + offset.x, pos.y + offset.y, pos.z + offset.z};
            uint8_t neighborBlock = getBlockLight(neighbor);
            if (neighborBlock > 0) {
                propagateBlockLight(neighbor, neighborBlock);
            }
            uint8_t neighborSky = getSkyLight(neighbor);
            if (neighborSky > 0) {
                propagateSkyLight(neighbor, neighborSky);
            }
        }
    }

    recordAffectedChunk(pos);
}

// ============================================================================
// Batch Operations
// ============================================================================

void LightEngine::initializeColumnLighting(const ColumnPos& columnPos) {
    ChunkColumn* column = world_.getColumn(columnPos);
    if (!column) return;

    auto bounds = column->getYBounds();
    if (!bounds) return;

    for (int32_t chunkY = bounds->first; chunkY <= bounds->second; ++chunkY) {
        SubChunk* sub = column->getSubChunk(chunkY);
        if (!sub) continue;

        bool hasFluid = sub->hasFluidLayer();

        for (int32_t y = 0; y < 16; ++y) {
            for (int32_t z = 0; z < 16; ++z) {
                for (int32_t x = 0; x < 16; ++x) {
                    uint8_t blockEmission = 0;
                    BlockTypeId block = sub->getBlock(x, y, z);
                    if (!block.isAir()) {
                        blockEmission = getLightEmission(block);
                    }

                    FluidTypeId fluid;
                    uint8_t fluidEmission = 0;
                    if (hasFluid) {
                        fluid = sub->getFluid(x, y, z);
                        if (!fluid.isEmpty()) {
                            const FluidType* ft = FluidRegistry::global().getType(fluid);
                            if (ft) fluidEmission = ft->lightEmission;
                        }
                    }

                    if (blockEmission > 0 || fluidEmission > 0) {
                        BlockCoord worldPos{
                            columnPos.x * 16 + x,
                            chunkY * 16 + y,
                            columnPos.z * 16 + z
                        };
                        LightingUpdate update;
                        update.pos = worldPos;
                        update.oldType = AIR_BLOCK_TYPE;
                        update.newType = blockEmission > 0 ? block : AIR_BLOCK_TYPE;
                        update.oldFluid = FluidTypeId{};
                        update.newFluid = fluidEmission > 0 ? fluid : FluidTypeId{};
                        update.triggerMeshRebuild = true;
                        enqueue(std::move(update));
                    }
                }
            }
        }
    }
}

void LightEngine::recalculateSubChunk(const ChunkPos& chunkPos) {
    SubChunk* subChunk = world_.getSubChunk(chunkPos);
    if (!subChunk) {
        return;
    }

    subChunk->clearLight();

    bool hasFluid = subChunk->hasFluidLayer();

    // Find all light-emitting blocks and fluids, then propagate
    for (int32_t y = 0; y < 16; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) {
                uint8_t emission = 0;

                BlockTypeId block = subChunk->getBlock(x, y, z);
                emission = getLightEmission(block);

                // Check fluid light emission if no block emission
                if (emission == 0 && hasFluid) {
                    FluidTypeId fid = subChunk->getFluid(x, y, z);
                    if (!fid.isEmpty()) {
                        const FluidType* ft = FluidRegistry::global().getType(fid);
                        if (ft && ft->lightEmission > emission) {
                            emission = ft->lightEmission;
                        }
                    }
                }

                if (emission > 0) {
                    BlockCoord worldPos{
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

void LightEngine::markDirty(const BlockCoord& pos) {
    pendingUpdates_.insert(pos);
}

void LightEngine::processUpdates() {
    // Process pending updates
    for (const BlockCoord& pos : pendingUpdates_) {
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
    using Clock = std::chrono::steady_clock;

    // Set initial alarm for background scan
    auto nextScanTime = Clock::now() + computeScanInterval();
    queue_.setAlarm(nextScanTime);

    while (running_.load(std::memory_order_acquire)) {
        // Dequeue a batch of updates (blocks until data, alarm, or stop)
        bool alarmFired = false;
        auto batch = queue_.dequeueBatch(batchSize_, &alarmFired);

        if (batch.empty() && !alarmFired) {
            // Queue was stopped
            break;
        }

        if (!batch.empty()) {
            // Clear affected chunks set before processing batch
            batchAffectedChunks_.clear();

            // Process each update in the batch
            for (const auto& update : batch) {
                processLightingUpdate(update);
            }

            // After processing entire batch, push mesh rebuild requests for all affected chunks
            flushAffectedChunks();
        }

        // Background scan: if alarm fired, scan one entry
        if (alarmFired || Clock::now() >= nextScanTime) {
            backgroundScanStep();
            nextScanTime = Clock::now() + computeScanInterval();
            queue_.setAlarm(nextScanTime);
        }
    }
}

void LightEngine::recordAffectedChunk(const BlockCoord& pos) {
    ChunkPos chunkPos = toChunkPos(pos);
    batchAffectedChunks_.insert(chunkPos);

    // Check if position is at subchunk boundary - if so, also mark adjacent chunk
    // since faces in neighboring chunks may sample light from this position.
    // Local coordinates within a 16x16x16 subchunk
    int32_t localX = ((pos.x % 16) + 16) % 16;
    int32_t localY = ((pos.y % 16) + 16) % 16;
    int32_t localZ = ((pos.z % 16) + 16) % 16;

    if (localX == 0) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x - 1, chunkPos.y, chunkPos.z});
    } else if (localX == 15) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x + 1, chunkPos.y, chunkPos.z});
    }

    if (localY == 0) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x, chunkPos.y - 1, chunkPos.z});
    } else if (localY == 15) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x, chunkPos.y + 1, chunkPos.z});
    }

    if (localZ == 0) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x, chunkPos.y, chunkPos.z - 1});
    } else if (localZ == 15) {
        batchAffectedChunks_.insert(ChunkPos{chunkPos.x, chunkPos.y, chunkPos.z + 1});
    }
}

void LightEngine::flushAffectedChunks() {
    if (!meshRebuildQueue_ || batchAffectedChunks_.empty()) {
        return;
    }

    for (const auto& chunkPos : batchAffectedChunks_) {
        // Push rebuild request - MeshRebuildQueue will coalesce duplicates
        meshRebuildQueue_->push(chunkPos, MeshRebuildRequest::normal());
        // Ensure affected positions appear in next scan cycle
        scanOutbox_.insert(chunkPos);
    }

    batchAffectedChunks_.clear();
}

void LightEngine::processLightingUpdate(const LightingUpdate& update) {
    // --- Block change handling ---
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
        if (oldAttenuation >= 15 && newAttenuation < 15) {
            onBlockRemoved(update.pos, update.oldType);
        } else {
            onBlockPlaced(update.pos, update.oldType, update.newType);
        }
    }

    // --- Fluid change handling ---
    if (update.oldFluid != update.newFluid) {
        if (!update.oldFluid.isEmpty()) {
            onFluidRemoved(update.pos, update.oldFluid);
        }
        if (!update.newFluid.isEmpty()) {
            onFluidPlaced(update.pos, update.newFluid);
        }
    }

    // If triggerMeshRebuild is set, ensure this chunk is in the affected set
    // even if no lighting changed (e.g., block change that doesn't affect light)
    if (update.triggerMeshRebuild) {
        recordAffectedChunk(update.pos);
    }

    // Note: Mesh rebuild requests are now batched and pushed by flushAffectedChunks()
    // at the end of each batch, not per-update
}

// ============================================================================
// Background Scan (Safety Net)
// ============================================================================

void LightEngine::backgroundScanStep() {
    if (!meshRebuildQueue_) return;

    // If inbox is empty, swap with outbox (start new cycle)
    if (scanInbox_.empty()) {
        scanInbox_ = std::move(scanOutbox_);
        scanOutbox_.clear();  // outbox is now moved-from; reset to empty

        // Seed outbox with all currently loaded subchunks for next cycle
        for (const auto& pos : world_.getAllSubChunkPositions()) {
            scanOutbox_.insert(pos);
        }
    }

    if (scanInbox_.empty()) return;

    // Pull one entry from inbox
    auto it = scanInbox_.begin();
    ChunkPos pos = *it;
    scanInbox_.erase(it);

    // Copy to outbox (so it appears in next cycle)
    scanOutbox_.insert(pos);

    // Drop stale entries (subchunk no longer loaded)
    if (!world_.getSubChunk(pos)) return;

    // Skip subchunks too far from any player to matter
    if (!isNearAnyPlayer(pos)) return;

    // Push background-priority remesh
    meshRebuildQueue_->push(pos, MeshRebuildRequest::background());
}

std::chrono::milliseconds LightEngine::computeScanInterval() const {
    size_t count = scanInbox_.size() + scanOutbox_.size();
    if (count == 0) count = 1;

    // ms per entry = (cycle_seconds * 1000) / count
    int64_t ms = (SCAN_CYCLE_SECONDS * 1000) / static_cast<int64_t>(count);
    ms = std::clamp(ms, int64_t(100), int64_t(2000));
    return std::chrono::milliseconds(ms);
}

bool LightEngine::isNearAnyPlayer(ChunkPos pos) const {
    std::lock_guard<std::mutex> lock(playerPosMutex_);
    if (playerPositions_.empty()) return true;  // No players tracked: scan everything

    for (const auto& playerPos : playerPositions_) {
        float dist = LODConfig::distanceToChunk(playerPos, pos);
        if (dist <= scanRadius_) return true;
    }
    return false;
}

void LightEngine::setPlayerPositions(std::vector<glm::dvec3> positions) {
    std::lock_guard<std::mutex> lock(playerPosMutex_);
    playerPositions_ = std::move(positions);
}

}  // namespace finevox
