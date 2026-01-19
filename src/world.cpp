#include "finevox/world.hpp"

namespace finevox {

World::World() = default;
World::~World() = default;

ColumnPos World::blockToColumn(BlockPos pos) {
    // Arithmetic right shift gives floor division for signed integers
    return ColumnPos(pos.x >> 4, pos.z >> 4);
}

BlockTypeId World::getBlock(BlockPos pos) const {
    return getBlock(pos.x, pos.y, pos.z);
}

BlockTypeId World::getBlock(int32_t x, int32_t y, int32_t z) const {
    ColumnPos colPos = blockToColumn(BlockPos(x, y, z));

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        return AIR_BLOCK_TYPE;
    }
    return it->second->getBlock(x, y, z);
}

void World::setBlock(BlockPos pos, BlockTypeId type) {
    setBlock(pos.x, pos.y, pos.z, type);
}

void World::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    ColumnPos colPos = blockToColumn(BlockPos(x, y, z));

    std::unique_lock lock(columnMutex_);

    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        // Create new column
        auto column = std::make_unique<ChunkColumn>(colPos);
        if (columnGenerator_) {
            columnGenerator_(*column);
        }
        it = columns_.emplace(colPos.pack(), std::move(column)).first;
    }

    it->second->setBlock(x, y, z, type);
}

ChunkColumn* World::getColumn(ColumnPos pos) {
    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(pos.pack());
    return it != columns_.end() ? it->second.get() : nullptr;
}

const ChunkColumn* World::getColumn(ColumnPos pos) const {
    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(pos.pack());
    return it != columns_.end() ? it->second.get() : nullptr;
}

ChunkColumn& World::getOrCreateColumn(ColumnPos pos) {
    std::unique_lock lock(columnMutex_);

    auto it = columns_.find(pos.pack());
    if (it != columns_.end()) {
        return *it->second;
    }

    auto column = std::make_unique<ChunkColumn>(pos);
    if (columnGenerator_) {
        columnGenerator_(*column);
    }
    auto& ref = *column;
    columns_.emplace(pos.pack(), std::move(column));
    return ref;
}

bool World::hasColumn(ColumnPos pos) const {
    std::shared_lock lock(columnMutex_);
    return columns_.contains(pos.pack());
}

bool World::removeColumn(ColumnPos pos) {
    std::unique_lock lock(columnMutex_);
    return columns_.erase(pos.pack()) > 0;
}

void World::forEachColumn(const std::function<void(ColumnPos, ChunkColumn&)>& callback) {
    std::shared_lock lock(columnMutex_);
    for (auto& [packed, column] : columns_) {
        callback(ColumnPos::unpack(packed), *column);
    }
}

void World::forEachColumn(const std::function<void(ColumnPos, const ChunkColumn&)>& callback) const {
    std::shared_lock lock(columnMutex_);
    for (const auto& [packed, column] : columns_) {
        callback(ColumnPos::unpack(packed), *column);
    }
}

size_t World::columnCount() const {
    std::shared_lock lock(columnMutex_);
    return columns_.size();
}

int64_t World::totalNonAirBlocks() const {
    std::shared_lock lock(columnMutex_);
    int64_t total = 0;
    for (const auto& [packed, column] : columns_) {
        total += column->nonAirCount();
    }
    return total;
}

void World::setColumnGenerator(ColumnGenerator generator) {
    columnGenerator_ = std::move(generator);
}

SubChunk* World::getSubChunk(ChunkPos pos) {
    ColumnPos colPos = ColumnPos::fromChunk(pos);

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        return nullptr;
    }
    return it->second->getSubChunk(pos.y);
}

const SubChunk* World::getSubChunk(ChunkPos pos) const {
    ColumnPos colPos = ColumnPos::fromChunk(pos);

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        return nullptr;
    }
    return it->second->getSubChunk(pos.y);
}

std::vector<ChunkPos> World::getAllSubChunkPositions() const {
    std::vector<ChunkPos> positions;

    std::shared_lock lock(columnMutex_);
    for (const auto& [packed, column] : columns_) {
        ColumnPos colPos = ColumnPos::unpack(packed);

        // Iterate over all existing subchunks (sparse storage)
        column->forEachSubChunk([&](int32_t chunkY, const SubChunk& subchunk) {
            if (!subchunk.isEmpty()) {
                positions.push_back(ChunkPos(colPos.x, chunkY, colPos.z));
            }
        });
    }

    return positions;
}

void World::clear() {
    std::unique_lock lock(columnMutex_);
    columns_.clear();
}

// ============================================================================
// Mesh Utilities
// ============================================================================

std::vector<ChunkPos> World::getAffectedSubChunks(BlockPos blockPos) const {
    std::vector<ChunkPos> affected;
    affected.reserve(4);  // At most 1 + 3 adjacent (corner case)

    // Calculate the containing subchunk position
    ChunkPos containingChunk = ChunkPos::fromBlock(blockPos);
    affected.push_back(containingChunk);

    // Calculate local coordinates within the subchunk (0-15)
    // Bitwise AND with 15 extracts lowest 4 bits, giving correct result for
    // both positive and negative coordinates (two's complement)
    int32_t localX = blockPos.x & 15;
    int32_t localY = blockPos.y & 15;
    int32_t localZ = blockPos.z & 15;

    // Check if block is at any boundary and add adjacent subchunks
    // A block at x=0 affects the chunk at x-1 (its +X face)
    // A block at x=15 affects the chunk at x+1 (its -X face)

    if (localX == 0) {
        affected.push_back(ChunkPos(containingChunk.x - 1, containingChunk.y, containingChunk.z));
    } else if (localX == 15) {
        affected.push_back(ChunkPos(containingChunk.x + 1, containingChunk.y, containingChunk.z));
    }

    if (localY == 0) {
        affected.push_back(ChunkPos(containingChunk.x, containingChunk.y - 1, containingChunk.z));
    } else if (localY == 15) {
        affected.push_back(ChunkPos(containingChunk.x, containingChunk.y + 1, containingChunk.z));
    }

    if (localZ == 0) {
        affected.push_back(ChunkPos(containingChunk.x, containingChunk.y, containingChunk.z - 1));
    } else if (localZ == 15) {
        affected.push_back(ChunkPos(containingChunk.x, containingChunk.y, containingChunk.z + 1));
    }

    return affected;
}

}  // namespace finevox
