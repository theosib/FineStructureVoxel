#include "finevox/chunk_column.hpp"
#include "finevox/data_container.hpp"
#include <algorithm>
#include <limits>
#include <vector>

namespace finevox {

ChunkColumn::ChunkColumn(ColumnPos pos) : pos_(pos) {
    // Initialize heightmap to NO_HEIGHT (no opaque blocks)
    heightmap_.fill(NO_HEIGHT);
}

ChunkColumn::~ChunkColumn() = default;

ChunkColumn::ChunkColumn(ChunkColumn&&) noexcept = default;
ChunkColumn& ChunkColumn::operator=(ChunkColumn&&) noexcept = default;

int32_t ChunkColumn::blockYToChunkY(int32_t blockY) {
    // Floor division for negative numbers
    if (blockY >= 0) {
        return blockY / SubChunk::SIZE;
    } else {
        return (blockY - SubChunk::SIZE + 1) / SubChunk::SIZE;
    }
}

int32_t ChunkColumn::blockYToLocalY(int32_t blockY) {
    // Modulo that always gives positive result 0-15
    return blockY & (SubChunk::SIZE - 1);
}

BlockTypeId ChunkColumn::getBlock(BlockPos pos) const {
    return getBlock(pos.x, pos.y, pos.z);
}

BlockTypeId ChunkColumn::getBlock(int32_t x, int32_t y, int32_t z) const {
    int32_t chunkY = blockYToChunkY(y);
    auto it = subChunks_.find(chunkY);
    if (it == subChunks_.end()) {
        return AIR_BLOCK_TYPE;
    }

    int32_t localX = x & (SubChunk::SIZE - 1);
    int32_t localY = blockYToLocalY(y);
    int32_t localZ = z & (SubChunk::SIZE - 1);

    return it->second->getBlock(localX, localY, localZ);
}

void ChunkColumn::setBlock(BlockPos pos, BlockTypeId type) {
    setBlock(pos.x, pos.y, pos.z, type);
}

void ChunkColumn::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    int32_t chunkY = blockYToChunkY(y);

    int32_t localX = x & (SubChunk::SIZE - 1);
    int32_t localY = blockYToLocalY(y);
    int32_t localZ = z & (SubChunk::SIZE - 1);

    // If setting to air and subchunk doesn't exist, nothing to do
    if (type.isAir()) {
        auto it = subChunks_.find(chunkY);
        if (it == subChunks_.end()) {
            return;
        }
        it->second->setBlock(localX, localY, localZ, type);
        // Remove subchunk if it becomes empty
        if (it->second->isEmpty()) {
            subChunks_.erase(it);
        }
    } else {
        // Create subchunk if needed
        SubChunk& subChunk = getOrCreateSubChunk(chunkY);
        subChunk.setBlock(localX, localY, localZ, type);
    }
}

bool ChunkColumn::hasSubChunk(int32_t chunkY) const {
    return subChunks_.contains(chunkY);
}

SubChunk* ChunkColumn::getSubChunk(int32_t chunkY) {
    auto it = subChunks_.find(chunkY);
    return it != subChunks_.end() ? it->second.get() : nullptr;
}

const SubChunk* ChunkColumn::getSubChunk(int32_t chunkY) const {
    auto it = subChunks_.find(chunkY);
    return it != subChunks_.end() ? it->second.get() : nullptr;
}

std::shared_ptr<SubChunk> ChunkColumn::getSubChunkShared(int32_t chunkY) {
    auto it = subChunks_.find(chunkY);
    return it != subChunks_.end() ? it->second : nullptr;
}

SubChunk& ChunkColumn::getOrCreateSubChunk(int32_t chunkY) {
    auto& ptr = subChunks_[chunkY];
    if (!ptr) {
        ptr = std::make_shared<SubChunk>();
    }
    return *ptr;
}

void ChunkColumn::pruneEmptySubChunks() {
    for (auto it = subChunks_.begin(); it != subChunks_.end(); ) {
        if (it->second->isEmpty()) {
            it = subChunks_.erase(it);
        } else {
            ++it;
        }
    }
}

int64_t ChunkColumn::nonAirCount() const {
    int64_t total = 0;
    for (const auto& [y, subChunk] : subChunks_) {
        total += subChunk->nonAirCount();
    }
    return total;
}

void ChunkColumn::forEachSubChunk(const std::function<void(int32_t, SubChunk&)>& callback) {
    for (auto& [y, subChunk] : subChunks_) {
        callback(y, *subChunk);
    }
}

void ChunkColumn::forEachSubChunk(const std::function<void(int32_t, const SubChunk&)>& callback) const {
    for (const auto& [y, subChunk] : subChunks_) {
        callback(y, *subChunk);
    }
}

std::optional<std::pair<int32_t, int32_t>> ChunkColumn::getYBounds() const {
    if (subChunks_.empty()) {
        return std::nullopt;
    }

    int32_t minY = std::numeric_limits<int32_t>::max();
    int32_t maxY = std::numeric_limits<int32_t>::min();

    for (const auto& [y, subChunk] : subChunks_) {
        if (!subChunk->isEmpty()) {
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }

    if (minY > maxY) {
        return std::nullopt;  // All subchunks were empty
    }

    return std::make_pair(minY, maxY);
}

void ChunkColumn::compactAll() {
    for (auto& [y, subChunk] : subChunks_) {
        if (subChunk->needsCompaction()) {
            (void)subChunk->compactPalette();
        }
    }
}

// ============================================================================
// Heightmap Implementation
// ============================================================================

int32_t ChunkColumn::getHeight(int32_t localX, int32_t localZ) const {
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        return NO_HEIGHT;
    }
    return heightmap_[toHeightmapIndex(localX, localZ)];
}

void ChunkColumn::updateHeight(int32_t localX, int32_t localZ, int32_t blockY, bool blocksSkyLight) {
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) {
        return;
    }

    int32_t idx = toHeightmapIndex(localX, localZ);
    int32_t currentHeight = heightmap_[idx];

    if (blocksSkyLight) {
        // Placing an opaque block - update height if this is higher
        int32_t newHeight = blockY + 1;  // Height is top of block
        if (currentHeight == NO_HEIGHT || newHeight > currentHeight) {
            heightmap_[idx] = newHeight;
        }
    } else {
        // Removing/replacing with transparent block
        // If this was at the current height, we need to recalculate
        if (currentHeight != NO_HEIGHT && blockY + 1 == currentHeight) {
            // Scan down to find new highest opaque block
            heightmap_[idx] = NO_HEIGHT;

            // We need to look through all subchunks at this X,Z
            // Start from the block below the one we just removed
            for (int32_t y = blockY - 1; y >= -2048; --y) {
                int32_t chunkY = blockYToChunkY(y);
                auto it = subChunks_.find(chunkY);
                if (it == subChunks_.end()) {
                    // Skip to next subchunk below
                    y = chunkY * 16;  // Will be decremented by loop
                    continue;
                }

                int32_t localY = blockYToLocalY(y);
                BlockTypeId block = it->second->getBlock(localX, localY, localZ);

                // Check if this block is opaque (blocks sky light)
                // For now, we consider any non-air block as potentially blocking
                // The caller should use BlockRegistry to check blocksSkyLight
                if (!block.isAir()) {
                    // Found a non-air block - assume it blocks sky light
                    // (Proper implementation would check BlockRegistry)
                    heightmap_[idx] = y + 1;
                    break;
                }
            }
        }
    }
}

void ChunkColumn::recalculateHeightmap() {
    // Reset heightmap
    heightmap_.fill(NO_HEIGHT);

    // Get sorted list of subchunk Y coordinates (highest first)
    std::vector<int32_t> yCoords;
    yCoords.reserve(subChunks_.size());
    for (const auto& [y, subChunk] : subChunks_) {
        yCoords.push_back(y);
    }
    std::sort(yCoords.begin(), yCoords.end(), std::greater<int32_t>());

    // For each X,Z column, find the highest opaque block
    for (int32_t localZ = 0; localZ < 16; ++localZ) {
        for (int32_t localX = 0; localX < 16; ++localX) {
            int32_t idx = toHeightmapIndex(localX, localZ);

            // Scan from top to bottom
            for (int32_t chunkY : yCoords) {
                const SubChunk& subChunk = *subChunks_.at(chunkY);

                // Scan this subchunk from top to bottom
                for (int32_t localY = 15; localY >= 0; --localY) {
                    BlockTypeId block = subChunk.getBlock(localX, localY, localZ);
                    if (!block.isAir()) {
                        // Found a non-air block
                        // For proper implementation, check BlockRegistry::getType(block).blocksSkyLight()
                        int32_t worldY = chunkY * 16 + localY;
                        heightmap_[idx] = worldY + 1;
                        goto next_column;
                    }
                }
            }
            next_column:;
        }
    }

    heightmapDirty_ = false;
}

void ChunkColumn::setHeightmapData(const std::array<int32_t, 256>& data) {
    heightmap_ = data;
    heightmapDirty_ = false;
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

}  // namespace finevox
