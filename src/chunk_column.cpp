#include "finevox/chunk_column.hpp"
#include <algorithm>
#include <limits>

namespace finevox {

ChunkColumn::ChunkColumn(ColumnPos pos) : pos_(pos) {}

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

}  // namespace finevox
