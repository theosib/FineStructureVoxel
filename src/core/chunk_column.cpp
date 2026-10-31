#include "finevox/core/chunk_column.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/shared_cell.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <vector>

namespace finevox {

ChunkColumn::ChunkColumn(ColumnPos pos) : pos_(pos) {
    // Initialize heightmap to NO_HEIGHT (no opaque blocks)
    heightmap_.fill(NO_HEIGHT);
}

ChunkColumn::~ChunkColumn() = default;

BlockTypeId ChunkColumn::getBlock(BlockCoord pos) const {
    return getBlock(pos.x, pos.y, pos.z);
}

BlockTypeId ChunkColumn::getBlock(int32_t x, int32_t y, int32_t z) const {
    int32_t chunkY = worldYToChunkY(y);
    const SubChunk* subChunk = liveSubChunk(chunkY);
    if (!subChunk) {
        return AIR_BLOCK_TYPE;
    }

    int32_t localX = x & (SubChunk::SIZE - 1);
    int32_t localY = worldYToLocalY(y);
    int32_t localZ = z & (SubChunk::SIZE - 1);

    return subChunk->getBlock(localX, localY, localZ);
}

void ChunkColumn::setBlock(BlockCoord pos, BlockTypeId type) {
    setBlock(pos.x, pos.y, pos.z, type);
}

void ChunkColumn::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    int32_t chunkY = worldYToChunkY(y);

    int32_t localX = x & (SubChunk::SIZE - 1);
    int32_t localY = worldYToLocalY(y);
    int32_t localZ = z & (SubChunk::SIZE - 1);

    // If setting to air and subchunk doesn't exist, nothing to do
    if (type.isAir()) {
        SubChunk* subChunk = liveSubChunk(chunkY);
        if (!subChunk) {
            return;
        }
        subChunk->setBlock(localX, localY, localZ, type);
        // Remove subchunk if it becomes empty
        if (subChunk->isEmpty()) {
            std::lock_guard lock(slotMutex_);
            takeOut(chunkY);
        }
    } else if (inRange(chunkY)) {
        // Create subchunk if needed
        getOrCreateSubChunk(chunkY).setBlock(localX, localY, localZ, type);
    }
}

bool ChunkColumn::hasSubChunk(int32_t chunkY) const {
    return liveSubChunk(chunkY) != nullptr;
}

SubChunk* ChunkColumn::getSubChunk(int32_t chunkY) {
    return liveSubChunk(chunkY);
}

const SubChunk* ChunkColumn::getSubChunk(int32_t chunkY) const {
    return liveSubChunk(chunkY);
}

std::shared_ptr<SubChunk> ChunkColumn::getSubChunkShared(int32_t chunkY) {
    if (!liveSubChunk(chunkY)) return nullptr;
    std::lock_guard lock(slotMutex_);
    return liveSubChunk(chunkY) ? owners_[chunkY - MIN_CHUNK_Y] : nullptr;
}

SubChunk& ChunkColumn::getOrCreateSubChunk(int32_t chunkY) {
    if (SubChunk* subChunk = liveSubChunk(chunkY)) {
        return *subChunk;
    }
    if (!inRange(chunkY)) {
        throw std::out_of_range("ChunkColumn: subchunk Y out of range");
    }

    // Two threads can get here at once: the game placing a block, lighting spreading
    std::lock_guard lock(slotMutex_);
    if (SubChunk* subChunk = liveSubChunk(chunkY)) {
        return *subChunk;
    }
    size_t slot = chunkY - MIN_CHUNK_Y;
    if (owners_[slot]) {
        owners_[slot]->recycle();
    } else {
        owners_[slot] = std::make_shared<SubChunk>();
        owners_[slot]->setPosition(toChunkPos(chunkY));
    }
    // It starts as what a missing subchunk reads as: with nothing above, that's open sky
    if (!hasContentAbove(chunkY)) owners_[slot]->fillSkyLight(SubChunk::MAX_LIGHT);
    if (chunkY > topChunkY_.load(std::memory_order_relaxed)) {
        topChunkY_.store(chunkY, std::memory_order_relaxed);
    }
    live_[slot].store(owners_[slot].get(), std::memory_order_release);
    return *owners_[slot];
}

void ChunkColumn::setSubChunk(int32_t chunkY, std::shared_ptr<SubChunk> subChunk) {
    if (!inRange(chunkY)) return;

    std::lock_guard lock(slotMutex_);
    size_t slot = chunkY - MIN_CHUNK_Y;
    if (!subChunk || subChunk->isEmpty()) {
        takeOut(chunkY);
        heightmapDirty_.store(true, std::memory_order_release);
        return;
    }
    subChunk->setPosition(toChunkPos(chunkY));
    if (owners_[slot]) replaced_.push_back(std::move(owners_[slot]));
    owners_[slot] = std::move(subChunk);
    if (chunkY > topChunkY_.load(std::memory_order_relaxed)) {
        topChunkY_.store(chunkY, std::memory_order_relaxed);
    }
    live_[slot].store(owners_[slot].get(), std::memory_order_release);
    heightmapDirty_.store(true, std::memory_order_release);
}

void ChunkColumn::setSubChunk(int32_t chunkY, std::unique_ptr<SubChunk> subChunk) {
    setSubChunk(chunkY, std::shared_ptr<SubChunk>(std::move(subChunk)));
}

void ChunkColumn::pruneEmptySubChunks() {
    std::lock_guard lock(slotMutex_);
    for (int32_t chunkY = MIN_CHUNK_Y; chunkY <= MAX_CHUNK_Y; ++chunkY) {
        const SubChunk* subChunk = liveSubChunk(chunkY);
        if (subChunk && subChunk->isEmpty()) {
            takeOut(chunkY);
        }
    }
}

size_t ChunkColumn::subChunkCount() const {
    size_t count = 0;
    for (const auto& slot : live_) {
        if (slot.load(std::memory_order_acquire)) ++count;
    }
    return count;
}

int64_t ChunkColumn::nonAirCount() const {
    int64_t total = 0;
    for (const auto& slot : live_) {
        if (const SubChunk* subChunk = slot.load(std::memory_order_acquire)) {
            total += subChunk->nonAirCount();
        }
    }
    return total;
}

void ChunkColumn::forEachSubChunk(const std::function<void(int32_t, SubChunk&)>& callback) {
    for (int32_t chunkY = MIN_CHUNK_Y; chunkY <= MAX_CHUNK_Y; ++chunkY) {
        if (SubChunk* subChunk = liveSubChunk(chunkY)) callback(chunkY, *subChunk);
    }
}

void ChunkColumn::forEachSubChunk(const std::function<void(int32_t, const SubChunk&)>& callback) const {
    for (int32_t chunkY = MIN_CHUNK_Y; chunkY <= MAX_CHUNK_Y; ++chunkY) {
        if (const SubChunk* subChunk = liveSubChunk(chunkY)) callback(chunkY, *subChunk);
    }
}

std::optional<std::pair<int32_t, int32_t>> ChunkColumn::getYBounds() const {
    int32_t minY = std::numeric_limits<int32_t>::max();
    int32_t maxY = std::numeric_limits<int32_t>::min();

    for (int32_t chunkY = MIN_CHUNK_Y; chunkY <= MAX_CHUNK_Y; ++chunkY) {
        const SubChunk* subChunk = liveSubChunk(chunkY);
        if (subChunk && !subChunk->isEmpty()) {
            minY = std::min(minY, chunkY);
            maxY = std::max(maxY, chunkY);
        }
    }

    if (minY > maxY) {
        return std::nullopt;  // All subchunks were empty
    }

    return std::make_pair(minY, maxY);
}

bool ChunkColumn::hasContentAbove(int32_t chunkY) const {
    int32_t top = topChunkY_.load(std::memory_order_relaxed);
    for (int32_t y = top; y > chunkY; --y) {
        const SubChunk* subChunk = liveSubChunk(y);
        if (subChunk && !subChunk->isEmpty()) return true;
    }
    return false;
}

void ChunkColumn::compactAll() {
    forEachSubChunk([](int32_t, SubChunk& subChunk) {
        if (subChunk.needsCompaction()) {
            (void)subChunk.compactPalette();
        }
    });
}

// ============================================================================
// Heightmap Implementation
// ============================================================================

int32_t ChunkColumn::getHeight(int32_t localX, int32_t localZ) const {
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        return NO_HEIGHT;
    }
    return sharedLoad(heightmap_[toHeightmapIndex(localX, localZ)]);
}

void ChunkColumn::updateHeight(int32_t localX, int32_t localZ, int32_t blockY, bool blocksSkyLight) {
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        return;
    }

    int32_t idx = toHeightmapIndex(localX, localZ);
    int32_t currentHeight = sharedLoad(heightmap_[idx]);

    if (blocksSkyLight) {
        // Placing an opaque block - update height if this is higher
        int32_t newHeight = blockY + 1;  // Height is top of block
        if (currentHeight == NO_HEIGHT || newHeight > currentHeight) {
            sharedStore(heightmap_[idx], newHeight);
        }
    } else {
        // Removing/replacing with transparent block
        // If this was at the current height, we need to recalculate
        if (currentHeight != NO_HEIGHT && blockY + 1 == currentHeight) {
            // Scan down to find new highest opaque block
            sharedStore(heightmap_[idx], NO_HEIGHT);

            // We need to look through all subchunks at this X,Z
            // Start from the block below the one we just removed
            for (int32_t y = blockY - 1; y >= -2048; --y) {
                int32_t chunkY = worldYToChunkY(y);
                const SubChunk* subChunk = liveSubChunk(chunkY);
                if (!subChunk) {
                    // Skip to next subchunk below
                    y = chunkY * 16;  // Will be decremented by loop
                    continue;
                }

                int32_t localY = worldYToLocalY(y);
                BlockTypeId block = subChunk->getBlock(localX, localY, localZ);

                // Check if this block is opaque (blocks sky light)
                // For now, we consider any non-air block as potentially blocking
                // The caller should use BlockRegistry to check blocksSkyLight
                if (!block.isAir()) {
                    // Found a non-air block - assume it blocks sky light
                    // (Proper implementation would check BlockRegistry)
                    sharedStore(heightmap_[idx], y + 1);
                    break;
                }
            }
        }
    }
}

void ChunkColumn::recalculateHeightmap() {
    // For each X,Z column, find the highest opaque block
    for (int32_t localZ = 0; localZ < 16; ++localZ) {
        for (int32_t localX = 0; localX < 16; ++localX) {
            int32_t height = NO_HEIGHT;

            // Scan from top to bottom
            for (int32_t chunkY = MAX_CHUNK_Y; chunkY >= MIN_CHUNK_Y; --chunkY) {
                const SubChunk* subChunk = liveSubChunk(chunkY);
                if (!subChunk) continue;

                // Scan this subchunk from top to bottom
                for (int32_t localY = 15; localY >= 0; --localY) {
                    BlockTypeId block = subChunk->getBlock(localX, localY, localZ);
                    if (!block.isAir()) {
                        // Found a non-air block
                        // For proper implementation, check BlockRegistry::getType(block).blocksSkyLight()
                        height = chunkY * 16 + localY + 1;
                        goto next_column;
                    }
                }
            }
            next_column:
            // One cell at a time: a reader must never see a height torn down mid-scan
            sharedStore(heightmap_[toHeightmapIndex(localX, localZ)], height);
        }
    }

    heightmapDirty_.store(false, std::memory_order_release);
}

void ChunkColumn::setHeightmapData(const std::array<int32_t, 256>& data) {
    for (size_t i = 0; i < data.size(); ++i) sharedStore(heightmap_[i], data[i]);
    heightmapDirty_.store(false, std::memory_order_release);
}

// ============================================================================
// Column Extra Data Implementation
// ============================================================================

DataContainer* ChunkColumn::data() {
    return data_.get();
}

const DataContainer* ChunkColumn::data() const {
    return data_.get();
}

DataContainer& ChunkColumn::getOrCreateData() {
    if (!data_) {
        data_ = std::make_unique<DataContainer>();
    }
    return *data_;
}

bool ChunkColumn::hasData() const {
    return data_ != nullptr;
}

void ChunkColumn::removeData() {
    data_.reset();
}

// ============================================================================
// Game Tick Registry Implementation
// ============================================================================

void ChunkColumn::rebuildGameTickRegistries() {
    forEachSubChunk([](int32_t, SubChunk& subChunk) { subChunk.rebuildGameTickRegistry(); });
}

// ============================================================================
// Activity Timer Implementation
// ============================================================================

void ChunkColumn::touchActivity() {
    lastActivityTime_ = std::chrono::steady_clock::now();
}

int64_t ChunkColumn::activityAgeMs() const {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - lastActivityTime_);
    return duration.count();
}

bool ChunkColumn::activityExpired(int64_t timeoutMs) const {
    return activityAgeMs() >= timeoutMs;
}

}  // namespace finevox
