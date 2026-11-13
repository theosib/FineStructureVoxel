#include "finevox/core/world_replica.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/subchunk.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace finevox {

namespace {

/// Columns examined per tick; a 32x32-column map cycles about once a second
constexpr int32_t COLUMNS_PER_TICK = 40;

/// Ticks to wait before looking at a column a client said would change. A command is run in the
/// first tick after it arrives, and this subsystem runs late in the tick, so two would do.
constexpr uint64_t EXPECT_TICKS = 3;

uint64_t subchunkKey(const ChunkPos& pos) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(pos.x) & 0xFFFFF) << 40) |
           (static_cast<uint64_t>(static_cast<uint32_t>(pos.z) & 0xFFFFF) << 20) |
           static_cast<uint64_t>(static_cast<uint32_t>(pos.y) & 0xFFFFF);
}

}  // namespace

WorldReplica::WorldReplica(World& source, FactionVision& vision, uint16_t viewer)
    : source_(source), vision_(vision), viewer_(viewer) {
    const auto& bounds = vision.bounds();
    cursorX_ = bounds.minX >> 4;
    cursorZ_ = bounds.minZ >> 4;
}

bool WorldReplica::columnVisible(ColumnPos pos) const {
    if (revealAll_) return true;
    int32_t originX = pos.x * 16;
    int32_t originZ = pos.z * 16;
    for (int32_t dz = 0; dz < 16; dz += FactionVision::CELL_SIZE) {
        for (int32_t dx = 0; dx < 16; dx += FactionVision::CELL_SIZE) {
            if (vision_.isVisible(viewer_, originX + dx, originZ + dz)) return true;
        }
    }
    return false;
}

void WorldReplica::noteChanged(ChunkPos pos) {
    if (changesNoted_.insert(subchunkKey(pos)).second) changes_.push_back(pos);
}

void WorldReplica::syncColumn(ColumnPos pos) {
    ChunkColumn* column = source_.getColumn(pos);
    if (!column) return;

    column->forEachSubChunk([&](int32_t chunkY, const SubChunk& subchunk) {
        ChunkPos chunk(pos.x, chunkY, pos.z);
        uint64_t key = subchunkKey(chunk);
        uint64_t blocks = subchunk.blockVersion();
        uint64_t light = subchunk.lightVersion();
        uint64_t fluid = subchunk.fluidVersion();
        auto [it, inserted] = versions_.try_emplace(key);
        bool blocksChanged = inserted || it->second.blocks != blocks;
        bool lightChanged = inserted || it->second.light != light;
        bool fluidChanged = inserted || it->second.fluid != fluid;
        if (!blocksChanged && !lightChanged && !fluidChanged) return;
        it->second = {blocks, light, fluid};

        // Air has light too, so a subchunk with nothing in it still needs to exist here
        SubChunk& copy = replica_.getOrCreateColumn(pos).getOrCreateSubChunk(chunkY);
        if (blocksChanged) {
            if (subchunk.isAirOnly()) {
                if (!copy.isAirOnly()) copy.clear();
            } else if (copy.isAirOnly() && subchunk.nonAirCount() == SubChunk::VOLUME &&
                       subchunk.palette().activeCount() == 2) {
                copy.fill(subchunk.getBlock(uint16_t(0)));
            } else if (copy.isAirOnly()) {
                for (uint16_t i = 0; i < SubChunk::VOLUME; ++i) {
                    BlockTypeId block = subchunk.getBlock(i);
                    if (!block.isAir()) copy.setBlock(i, block);
                }
            } else {
                for (uint16_t i = 0; i < SubChunk::VOLUME; ++i) {
                    BlockTypeId block = subchunk.getBlock(i);
                    if (copy.getBlock(i) != block) copy.setBlock(i, block);
                }
            }
        }
        if (lightChanged) {
            copy.setLightData(subchunk.lightData());
            copy.setBiomeData(subchunk.biomeData());
        }

        if (fluidChanged && (subchunk.fluidLayer() || copy.fluidLayer())) {
            for (int32_t i = 0; i < SubChunk::VOLUME; ++i) {
                FluidTypeId fluid = subchunk.getFluid(i);
                uint8_t level = subchunk.getFluidLevel(i);
                if (copy.getFluid(i) == fluid && copy.getFluidLevel(i) == level) continue;
                if (fluid.isEmpty()) {
                    copy.removeFluid(i);
                } else {
                    copy.setFluid(i, fluid, level);
                }
            }
        }
        noteChanged(chunk);
    });
}

void WorldReplica::syncAll(const std::function<void(ColumnPos)>& onColumn) {
    const auto& bounds = vision_.bounds();
    int32_t minX = bounds.minX >> 4;
    int32_t maxX = (bounds.maxX + 15) >> 4;
    int32_t minZ = bounds.minZ >> 4;
    int32_t maxZ = (bounds.maxZ + 15) >> 4;
    int32_t maxRing = std::max({-minX, maxX, -minZ, maxZ});
    for (int32_t ring = 0; ring < maxRing; ++ring) {
        int32_t lo = -ring - 1, hi = ring;
        for (int32_t cx = std::max(lo, minX); cx <= std::min(hi, maxX - 1); ++cx) {
            for (int32_t cz = std::max(lo, minZ); cz <= std::min(hi, maxZ - 1); ++cz) {
                if (cx != lo && cx != hi && cz != lo && cz != hi) continue;
                ColumnPos pos(cx, cz);
                if (columnVisible(pos)) syncColumn(pos);
                if (onColumn) onColumn(pos);
            }
        }
    }
}

void WorldReplica::expectChange(BlockCoord pos) {
    std::lock_guard lock(expectedMutex_);
    expected_.push_back({pos, tick_ + EXPECT_TICKS});
}

std::vector<ChunkPos> WorldReplica::takeChanges() {
    changesNoted_.clear();
    return std::exchange(changes_, {});
}

void WorldReplica::refreshLight() {
    replica_.forEachColumn([this](ColumnPos pos, ChunkColumn& column) {
        const ChunkColumn* source = source_.getColumn(pos);
        if (!source) return;
        column.forEachSubChunk([&](int32_t chunkY, SubChunk& copy) {
            if (const SubChunk* subchunk = source->getSubChunk(chunkY)) {
                copy.setLightData(subchunk->lightData());
                noteChanged(ChunkPos(pos.x, chunkY, pos.z));
            }
        });
    });
}

void WorldReplica::settleExpected() {
    std::vector<BlockCoord> due;
    {
        std::lock_guard lock(expectedMutex_);
        ++tick_;
        auto settled = std::stable_partition(expected_.begin(), expected_.end(),
                                             [this](const Expected& e) { return e.dueTick > tick_; });
        for (auto it = settled; it != expected_.end(); ++it) due.push_back(it->pos);
        expected_.erase(settled, expected_.end());
    }

    // A column it can't see, it can't look at: a client doesn't get to peek by asking
    for (const BlockCoord& pos : due) {
        ColumnPos column(pos.x >> 4, pos.z >> 4);
        if (columnVisible(column)) syncColumn(column);
    }
}

void WorldReplica::tick(float /*dt*/) {
    settleExpected();

    const auto& bounds = vision_.bounds();
    int32_t minX = bounds.minX >> 4;
    int32_t maxX = (bounds.maxX + 15) >> 4;
    int32_t minZ = bounds.minZ >> 4;
    int32_t maxZ = (bounds.maxZ + 15) >> 4;

    for (int32_t i = 0; i < COLUMNS_PER_TICK; ++i) {
        ColumnPos pos(cursorX_, cursorZ_);
        if (columnVisible(pos)) syncColumn(pos);

        if (++cursorX_ >= maxX) {
            cursorX_ = minX;
            if (++cursorZ_ >= maxZ) cursorZ_ = minZ;
        }
    }
}

}  // namespace finevox
