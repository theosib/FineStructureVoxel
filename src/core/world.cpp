#include "finevox/core/world.hpp"
#include "finevox/core/column_manager.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/event_queue.hpp"
#include "finevox/core/block_event.hpp"
#include "finevox/core/batch_builder.hpp"  // For BlockChange
#include "finevox/core/mesh_rebuild_queue.hpp"

#include <algorithm>
#include <cstdlib>

namespace finevox {

World::World() = default;
World::~World() = default;

ColumnPos World::blockToColumn(BlockCoord pos) {
    return ColumnPos::fromBlock(pos);
}

BlockTypeId World::getBlock(BlockCoord pos) const {
    return getBlock(pos.x, pos.y, pos.z);
}

BlockTypeId World::getBlock(int32_t x, int32_t y, int32_t z) const {
    ColumnPos colPos = blockToColumn(BlockCoord(x, y, z));

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it != columns_.end()) {
        return it->second->getBlock(x, y, z);
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
            return mc->column->getBlock(x, y, z);
        }
    }
    return AIR_BLOCK_TYPE;
}

void World::setBlock(BlockCoord pos, BlockTypeId type) {
    setBlock(pos.x, pos.y, pos.z, type);
}

void World::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    ColumnPos colPos = blockToColumn(BlockCoord(x, y, z));

    std::unique_lock lock(columnMutex_);

    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        if (columnManager_) {
            if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
                mc->column->setBlock(x, y, z, type);
                columnManager_->markDirty(colPos);
                return;
            }
        }
        // Create new column
        auto column = std::make_unique<ChunkColumn>(colPos);
        if (columnGenerator_) {
            columnGenerator_(*column);
        }
        it = columns_.emplace(colPos.pack(), std::move(column)).first;
    }

    it->second->setBlock(x, y, z, type);
    if (columnManager_) {
        columnManager_->markDirty(colPos);
    }
}

size_t World::setBlocks(std::span<const std::pair<BlockCoord, BlockTypeId>> changes) {
    if (changes.empty()) {
        return 0;
    }

    // Group changes by ChunkPos (subchunk coordinate) so we resolve each
    // ChunkColumn and SubChunk once per subchunk under a single columnMutex_ lock.
    struct LocalChange {
        int32_t lx;
        int32_t ly;
        int32_t lz;
        BlockTypeId type;
    };
    std::unordered_map<ChunkPos, std::vector<LocalChange>> bySubChunk;
    bySubChunk.reserve(std::min<size_t>(changes.size(), 64));

    for (const auto& [pos, type] : changes) {
        ChunkPos chunkPos = ChunkPos::fromBlock(pos);
        bySubChunk[chunkPos].push_back(LocalChange{
            pos.x & (SubChunk::SIZE - 1),
            ChunkColumn::worldYToLocalY(pos.y),
            pos.z & (SubChunk::SIZE - 1),
            type
        });
    }

    size_t changedCount = 0;
    std::unique_lock lock(columnMutex_);

    for (auto& [chunkPos, subChanges] : bySubChunk) {
        ColumnPos colPos = ColumnPos::fromChunk(chunkPos);
        ChunkColumn* colPtr = nullptr;

        auto colIt = columns_.find(colPos.pack());
        if (colIt != columns_.end()) {
            colPtr = colIt->second.get();
        } else if (columnManager_) {
            if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
                colPtr = mc->column.get();
            }
        }

        // Resolve SubChunk once for this subchunk batch
        SubChunk* sub = colPtr ? colPtr->getSubChunk(chunkPos.y) : nullptr;
        bool columnChanged = false;

        for (const auto& lc : subChanges) {
            if (!sub) {
                if (lc.type.isAir()) {
                    continue;  // Setting air in non-existent column/subchunk is a no-op
                }
                if (!colPtr) {
                    auto column = std::make_unique<ChunkColumn>(colPos);
                    if (columnGenerator_) {
                        columnGenerator_(*column);
                    }
                    colPtr = column.get();
                    columns_.emplace(colPos.pack(), std::move(column));
                }
                sub = &colPtr->getOrCreateSubChunk(chunkPos.y);
            }
            BlockTypeId oldType = sub->getBlock(lc.lx, lc.ly, lc.lz);
            if (oldType != lc.type) {
                sub->setBlock(lc.lx, lc.ly, lc.lz, lc.type);
                ++changedCount;
                columnChanged = true;
            }
        }

        if (sub && sub->isEmpty()) {
            colPtr->pruneEmptySubChunks();
        }

        if (columnChanged && columnManager_) {
            columnManager_->markDirty(colPos);
        }
    }

    return changedCount;
}

ChunkColumn* World::getColumn(ColumnPos pos) {
    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(pos.pack());
    if (it != columns_.end()) {
        return it->second.get();
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(pos); mc && mc->column) {
            return mc->column.get();
        }
    }
    return nullptr;
}

const ChunkColumn* World::getColumn(ColumnPos pos) const {
    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(pos.pack());
    if (it != columns_.end()) {
        return it->second.get();
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(pos); mc && mc->column) {
            return mc->column.get();
        }
    }
    return nullptr;
}

ChunkColumn& World::getOrCreateColumn(ColumnPos pos) {
    std::unique_lock lock(columnMutex_);

    auto it = columns_.find(pos.pack());
    if (it != columns_.end()) {
        return *it->second;
    }

    if (columnManager_) {
        if (auto* mc = columnManager_->get(pos); mc && mc->column) {
            return *mc->column;
        }
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
    if (columns_.contains(pos.pack())) {
        return true;
    }
    if (columnManager_) {
        return columnManager_->get(pos) != nullptr;
    }
    return false;
}

bool World::removeColumn(ColumnPos pos) {
    std::unique_lock lock(columnMutex_);
    return columns_.erase(pos.pack()) > 0;
}

void World::insertColumn(ColumnPos pos, std::unique_ptr<ChunkColumn> column) {
    if (!column) return;
    std::unique_lock lock(columnMutex_);
    columns_[pos.pack()] = std::move(column);
}

std::unique_ptr<ChunkColumn> World::extractColumn(ColumnPos pos) {
    std::unique_lock lock(columnMutex_);
    auto it = columns_.find(pos.pack());
    if (it == columns_.end()) {
        return nullptr;
    }
    auto col = std::move(it->second);
    columns_.erase(it);
    return col;
}

void World::setColumnManager(ColumnManager* manager) {
    columnManager_ = manager;
    if (columnManager_) {
        columnManager_->setCanUnloadCallback([this](ColumnPos pos) {
            return canUnloadColumn(pos);
        });
    }
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
    if (it != columns_.end()) {
        return it->second->getSubChunk(pos.y);
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
            return mc->column->getSubChunk(pos.y);
        }
    }
    return nullptr;
}

const SubChunk* World::getSubChunk(ChunkPos pos) const {
    ColumnPos colPos = ColumnPos::fromChunk(pos);

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it != columns_.end()) {
        return it->second->getSubChunk(pos.y);
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
            return mc->column->getSubChunk(pos.y);
        }
    }
    return nullptr;
}

std::shared_ptr<SubChunk> World::getSubChunkShared(ChunkPos pos) {
    ColumnPos colPos = ColumnPos::fromChunk(pos);

    std::shared_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it != columns_.end()) {
        return it->second->getSubChunkShared(pos.y);
    }
    if (columnManager_) {
        if (auto* mc = columnManager_->get(colPos); mc && mc->column) {
            return mc->column->getSubChunkShared(pos.y);
        }
    }
    return nullptr;
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
// Fluid Access
// ============================================================================

FluidTypeId World::getFluid(BlockCoord pos) const {
    ChunkPos chunkPos = ChunkPos::fromBlock(pos);
    const SubChunk* sub = getSubChunk(chunkPos);
    if (!sub) return EMPTY_FLUID_TYPE;

    auto local = pos.local();
    return sub->getFluid(local.x, local.y, local.z);
}

uint8_t World::getFluidLevel(BlockCoord pos) const {
    ChunkPos chunkPos = ChunkPos::fromBlock(pos);
    const SubChunk* sub = getSubChunk(chunkPos);
    if (!sub) return 0;

    auto local = pos.local();
    return sub->getFluidLevel(local.x, local.y, local.z);
}

bool World::hasFluid(BlockCoord pos) const {
    ChunkPos chunkPos = ChunkPos::fromBlock(pos);
    const SubChunk* sub = getSubChunk(chunkPos);
    if (!sub) return false;

    auto local = pos.local();
    return sub->hasFluid(local.x, local.y, local.z);
}

bool World::setFluid(BlockCoord pos, FluidTypeId type, uint8_t level) {
    ColumnPos colPos = blockToColumn(pos);

    std::unique_lock lock(columnMutex_);
    auto it = columns_.find(colPos.pack());
    if (it == columns_.end()) {
        // Create new column for fluid
        auto column = std::make_unique<ChunkColumn>(colPos);
        if (columnGenerator_) {
            columnGenerator_(*column);
        }
        it = columns_.emplace(colPos.pack(), std::move(column)).first;
    }

    // Get or create the subchunk
    ChunkPos chunkPos = ChunkPos::fromBlock(pos);
    SubChunk& sub = it->second->getOrCreateSubChunk(chunkPos.y);

    auto local = pos.local();
    bool changed = sub.setFluid(local.x, local.y, local.z, type, level);
    if (changed && columnManager_) {
        columnManager_->markDirty(colPos);
    }
    return changed;
}

bool World::removeFluid(BlockCoord pos) {
    ChunkPos chunkPos = ChunkPos::fromBlock(pos);
    SubChunk* sub = getSubChunk(chunkPos);
    if (!sub) return false;

    auto local = pos.local();
    bool changed = sub->removeFluid(local.x, local.y, local.z);
    if (changed && columnManager_) {
        columnManager_->markDirty(blockToColumn(pos));
    }
    return changed;
}

// ============================================================================
// Mesh Utilities
// ============================================================================

std::vector<ChunkPos> World::getAffectedSubChunks(BlockCoord blockPos) const {
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

// ============================================================================
// Force-Loading
// ============================================================================

void World::registerForceLoader(BlockCoord pos, int32_t radius) {
    std::unique_lock lock(forceLoaderMutex_);
    forceLoaders_[pos] = radius;
}

void World::unregisterForceLoader(BlockCoord pos) {
    std::unique_lock lock(forceLoaderMutex_);
    forceLoaders_.erase(pos);
}

bool World::canUnloadChunk(ChunkPos pos) const {
    std::shared_lock lock(forceLoaderMutex_);

    for (const auto& [loaderPos, radius] : forceLoaders_) {
        ChunkPos loaderChunk = ChunkPos::fromBlock(loaderPos);

        // Calculate Chebyshev distance (max of axis distances)
        int32_t dx = std::abs(pos.x - loaderChunk.x);
        int32_t dy = std::abs(pos.y - loaderChunk.y);
        int32_t dz = std::abs(pos.z - loaderChunk.z);
        int32_t distance = std::max({dx, dy, dz});

        if (distance <= radius) {
            return false;  // Chunk is within force-load radius
        }
    }

    return true;  // No force-loader is keeping this chunk loaded
}

bool World::isForceLoader(BlockCoord pos) const {
    std::shared_lock lock(forceLoaderMutex_);
    return forceLoaders_.contains(pos);
}

const std::unordered_map<BlockCoord, int32_t>& World::forceLoaders() const {
    // Note: Caller must ensure thread safety if iterating
    return forceLoaders_;
}

void World::setForceLoaders(std::unordered_map<BlockCoord, int32_t> loaders) {
    std::unique_lock lock(forceLoaderMutex_);
    forceLoaders_ = std::move(loaders);
}

bool World::canUnloadColumn(ColumnPos pos) const {
    std::shared_lock lock(forceLoaderMutex_);

    for (const auto& [loaderPos, radius] : forceLoaders_) {
        ChunkPos loaderChunk = ChunkPos::fromBlock(loaderPos);

        // Check XZ distance only - a column spans all Y values, so if XZ distance
        // is within radius, there's always a subchunk Y that would be in range
        int32_t dx = std::abs(pos.x - loaderChunk.x);
        int32_t dz = std::abs(pos.z - loaderChunk.z);
        int32_t xzDistance = std::max(dx, dz);

        if (xzDistance <= radius) {
            return false;  // Column is within force-load radius
        }
    }

    return true;  // No force-loader is keeping this column loaded
}

// ============================================================================
// Lighting Integration
// ============================================================================

void World::setLightEngine(LightEngine* engine) {
    lightEngine_ = engine;
}

void World::enqueueLightingUpdate(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType) {
    if (lightEngine_) {
        lightEngine_->enqueue(LightingUpdate{pos, oldType, newType, {}, {}});
    }
}

void World::processLightingUpdateSync(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType) {
    if (lightEngine_) {
        if (newType.isAir() && !oldType.isAir()) {
            // Block was removed - use onBlockRemoved which has logic to restore light
            lightEngine_->onBlockRemoved(pos, oldType);
        } else {
            lightEngine_->onBlockPlaced(pos, oldType, newType);
        }
    }
}

void World::setMeshRebuildQueue(MeshRebuildQueue* queue) {
    meshRebuildQueue_ = queue;
}

void World::enqueueLightingUpdateWithRemesh(BlockCoord pos, BlockTypeId oldType, BlockTypeId newType) {
    if (!lightEngine_) {
        return;
    }

    // Check if we should defer mesh rebuild to the lighting thread
    // Defer if: forced defer is enabled, OR the lighting queue is empty (no backlog)
    bool shouldDefer = alwaysDeferMeshRebuild_ || lightEngine_->queue().empty();

    if (shouldDefer) {
        // Queue is empty (or forced defer) - defer mesh rebuild to lighting thread
        // The lighting thread will push remesh requests after processing
        LightingUpdate update{pos, oldType, newType, {}, {}};
        update.triggerMeshRebuild = true;
        lightEngine_->enqueue(update);
    } else {
        // Queue is not empty - push remesh immediately for the changed block's chunk
        // Lighting thread may push additional remeshes if light actually changes
        if (meshRebuildQueue_) {
            ChunkPos chunkPos = ChunkPos::fromBlock(pos);
            meshRebuildQueue_->push(chunkPos, MeshRebuildRequest::normal());
        }

        // Enqueue lighting update without triggerMeshRebuild
        // Lighting thread will handle its own remesh requests for affected chunks
        LightingUpdate update{pos, oldType, newType, {}, {}};
        update.triggerMeshRebuild = false;
        lightEngine_->enqueue(update);
    }
}

// ============================================================================
// Event System Integration
// ============================================================================

void World::setUpdateScheduler(UpdateScheduler* scheduler) {
    updateScheduler_ = scheduler;
}

// ============================================================================
// External Block API (Event-Driven)
// ============================================================================

bool World::placeBlock(BlockCoord pos, BlockTypeId type, Rotation rotation, EntityId actor) {
    if (!updateScheduler_) {
        return false;
    }

    BlockTypeId oldType = getBlock(pos);
    updateScheduler_->pushExternalEvent(BlockEvent::blockPlaced(pos, type, oldType, rotation, actor));
    return true;
}

bool World::breakBlock(BlockCoord pos, EntityId actor) {
    if (!updateScheduler_) {
        return false;
    }

    updateScheduler_->pushExternalEvent(BlockEvent::blockBroken(pos, getBlock(pos), actor));
    return true;
}

size_t World::placeBlocks(const std::vector<BlockChange>& changes) {
    if (!updateScheduler_ || changes.empty()) {
        return 0;
    }

    std::vector<BlockEvent> events;
    events.reserve(changes.size());

    for (const auto& change : changes) {
        BlockTypeId oldType = getBlock(change.pos);
        events.push_back(BlockEvent::blockPlaced(change.pos, change.newType, oldType));
    }

    updateScheduler_->pushExternalEvents(std::move(events));
    return changes.size();
}

size_t World::breakBlocks(const std::vector<BlockCoord>& positions) {
    if (!updateScheduler_ || positions.empty()) {
        return 0;
    }

    std::vector<BlockEvent> events;
    events.reserve(positions.size());

    for (const auto& pos : positions) {
        events.push_back(BlockEvent::blockBroken(pos, getBlock(pos)));
    }

    updateScheduler_->pushExternalEvents(std::move(events));
    return positions.size();
}

}  // namespace finevox
