#include "finevox/core/world_replica.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"

#include <vector>

namespace finevox {

namespace {

/// Columns examined per tick; a 32x32-column map cycles about once a second
constexpr int32_t COLUMNS_PER_TICK = 40;

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

void WorldReplica::syncColumn(ColumnPos pos) {
    ChunkColumn* column = source_.getColumn(pos);
    if (!column) return;

    std::vector<std::pair<BlockCoord, BlockTypeId>> changes;
    std::vector<ChunkPos> touched;

    column->forEachSubChunk([&](int32_t chunkY, const SubChunk& subchunk) {
        ChunkPos chunk(pos.x, chunkY, pos.z);
        uint64_t key = subchunkKey(chunk);
        uint64_t version = subchunk.blockVersion();
        auto [it, inserted] = versions_.try_emplace(key, 0);
        if (!inserted && it->second == version) return;
        it->second = version;

        for (int32_t lx = 0; lx < 16; ++lx) {
            for (int32_t lz = 0; lz < 16; ++lz) {
                for (int32_t ly = 0; ly < 16; ++ly) {
                    BlockCoord world(chunk.x * 16 + lx, chunk.y * 16 + ly, chunk.z * 16 + lz);
                    BlockTypeId block = subchunk.getBlock(lx, ly, lz);
                    if (replica_.getBlock(world) != block) changes.emplace_back(world, block);
                }
            }
        }

        if (subchunk.fluidLayer()) {
            for (int32_t lx = 0; lx < 16; ++lx) {
                for (int32_t lz = 0; lz < 16; ++lz) {
                    for (int32_t ly = 0; ly < 16; ++ly) {
                        BlockCoord world(chunk.x * 16 + lx, chunk.y * 16 + ly, chunk.z * 16 + lz);
                        FluidTypeId fluid = source_.getFluid(world);
                        if (!fluid.isEmpty()) {
                            replica_.setFluid(world, fluid, source_.getFluidLevel(world));
                        }
                    }
                }
            }
        }
        touched.push_back(chunk);
    });

    if (changes.empty()) return;
    replica_.setBlocks(changes);

    if (auto* queue = replica_.meshRebuildQueue()) {
        for (const ChunkPos& chunk : touched) {
            queue->push(chunk, MeshRebuildRequest::immediate());
            // Neighbours: their border faces were drawn against empty space
            for (Face face : {Face::PosX, Face::NegX, Face::PosZ, Face::NegZ, Face::PosY, Face::NegY}) {
                auto n = faceNormal(face);
                queue->push(ChunkPos(chunk.x + n[0], chunk.y + n[1], chunk.z + n[2]),
                            MeshRebuildRequest::normal());
            }
        }
    }
}

void WorldReplica::syncAll() {
    const auto& bounds = vision_.bounds();
    for (int32_t cx = bounds.minX >> 4; cx < (bounds.maxX + 15) >> 4; ++cx) {
        for (int32_t cz = bounds.minZ >> 4; cz < (bounds.maxZ + 15) >> 4; ++cz) {
            ColumnPos pos(cx, cz);
            if (columnVisible(pos)) syncColumn(pos);
        }
    }
}

void WorldReplica::tick(float /*dt*/) {
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
