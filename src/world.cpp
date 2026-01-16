#include "finevox/world.hpp"

namespace finevox {

World::World() = default;
World::~World() = default;

ColumnPos World::blockToColumn(BlockPos pos) {
    // Floor division for negative coordinates
    int32_t cx = pos.x >= 0 ? pos.x / 16 : (pos.x - 15) / 16;
    int32_t cz = pos.z >= 0 ? pos.z / 16 : (pos.z - 15) / 16;
    return ColumnPos(cx, cz);
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

void World::clear() {
    std::unique_lock lock(columnMutex_);
    columns_.clear();
}

}  // namespace finevox
