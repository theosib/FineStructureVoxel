#include "finevox/net/client_world.hpp"

#include <algorithm>
#include <cstdlib>

#include "finevox/core/chunk_column.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include "finevox/core/subchunk.hpp"

namespace finevox::net {

void ClientWorld::nameBlock(uint32_t wireId, std::string_view name) {
    blockNames_[wireId] = BlockTypeId::fromName(name);
}

void ClientWorld::nameFluid(uint32_t wireId, std::string_view name) {
    fluidNames_[wireId] = FluidTypeId::fromName(name);
}

void ClientWorld::Changed::add(int32_t index) {
    const int at[3] = {index & 0xF, (index >> 8) & 0xF, (index >> 4) & 0xF};
    for (int axis = 0; axis < 3; ++axis) {
        low[axis] = std::min(low[axis], at[axis]);
        high[axis] = std::max(high[axis], at[axis]);
    }
}

void ClientWorld::Changed::all() {
    for (int axis = 0; axis < 3; ++axis) {
        low[axis] = 0;
        high[axis] = 15;
    }
}

void ClientWorld::remesh(ChunkPos chunk, const Changed* changed) {
    auto* queue = world_.meshRebuildQueue();
    if (!queue) return;
    queue->push(chunk, MeshRebuildRequest::immediate());
    if (!changed) return;
    // Their border faces were drawn against what used to be here, light included
    for (Face face : {Face::PosX, Face::NegX, Face::PosZ, Face::NegZ, Face::PosY, Face::NegY}) {
        auto n = faceNormal(face);
        queue->push(ChunkPos(chunk.x + n[0], chunk.y + n[1], chunk.z + n[2]), MeshRebuildRequest(1));
    }
    // And past them, any the changed cells reach: x, y, z
    const int reach[3] = {reachAcross_.load(), reachUpDown_.load(), reachAcross_.load()};
    if (reach[0] <= 1 && reach[1] <= 1) return;
    auto touches = [&](int axis, int side) {
        if (side < 0) return changed->low[axis] - reach[axis] < 0;
        if (side > 0) return changed->high[axis] + reach[axis] > 15;
        return true;
    };
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dz = -1; dz <= 1; ++dz) {
                if (std::abs(dx) + std::abs(dy) + std::abs(dz) < 2) continue;
                if (touches(0, dx) && touches(1, dy) && touches(2, dz)) {
                    queue->push(ChunkPos(chunk.x + dx, chunk.y + dy, chunk.z + dz), MeshRebuildRequest(1));
                }
            }
        }
    }
}

void ClientWorld::apply(const SubchunkData& data) {
    ++subchunksReceived_;
    const ChunkPos& chunk = data.pos;

    std::vector<BlockTypeId> palette;
    palette.reserve(data.palette.size());
    for (uint32_t id : data.palette) {
        auto name = blockNames_.find(id);
        palette.push_back(name == blockNames_.end() ? BlockTypeId() : name->second);
    }

    SubChunk& subchunk = world_.getOrCreateColumn(ColumnPos(chunk.x, chunk.z)).getOrCreateSubChunk(chunk.y);
    Changed changed;
    {
        std::lock_guard lock(predictionMutex_);
        if (predictions_.empty()) {
            if (data.indices.empty()) {
                BlockTypeId fill = palette.empty() ? BlockTypeId() : palette[0];
                if (!fill.isAir() || !subchunk.isAirOnly()) {
                    subchunk.fill(fill);
                    changed.all();
                }
            } else if (subchunk.isAirOnly()) {
                for (uint16_t index = 0; index < static_cast<uint16_t>(SubchunkData::CELLS); ++index) {
                    BlockTypeId block = palette[data.indices[index]];
                    if (!block.isAir()) {
                        subchunk.setBlock(index, block);
                        changed.add(index);
                    }
                }
            } else {
                for (uint16_t index = 0; index < static_cast<uint16_t>(SubchunkData::CELLS); ++index) {
                    BlockTypeId block = palette[data.indices[index]];
                    if (subchunk.getBlock(index) != block) {
                        subchunk.setBlock(index, block);
                        changed.add(index);
                    }
                }
            }
        } else {
            for (int32_t index = 0; index < static_cast<int32_t>(SubchunkData::CELLS); ++index) {
                int32_t lx = index & 0xF, lz = (index >> 4) & 0xF, ly = (index >> 8) & 0xF;
                BlockCoord world(chunk.x * 16 + lx, chunk.y * 16 + ly, chunk.z * 16 + lz);
                BlockTypeId block = palette[data.indices.empty() ? 0 : data.indices[index]];

                auto predicted = predictions_.find(world);
                if (predicted != predictions_.end()) {
                    if (!predicted->second.answeredBy(block)) {
                        predicted->second.serverSays = block;
                        continue;
                    }
                    // The server has done it: its word now, not when the hold runs out
                    predictions_.erase(predicted);
                }
                if (subchunk.getBlock(static_cast<uint16_t>(index)) != block) {
                    subchunk.setBlock(static_cast<uint16_t>(index), block);
                    changed.add(index);
                }
            }
        }
    }
    subchunk.setLightData(data.light);
    subchunk.setBiomeData(data.biomes);

    bool hadFluid = subchunk.fluidLayer() != nullptr;
    if (hadFluid || !data.fluids.empty()) {
        changed.all();  // Not worth finding which: it's how far smoothing reaches that cares
        std::array<bool, SubchunkData::CELLS> wet{};
        for (const auto& fluid : data.fluids) {
            wet[fluid.index] = true;
            auto name = fluidNames_.find(fluid.id);
            if (name != fluidNames_.end()) subchunk.setFluid(fluid.index, name->second, fluid.level);
        }
        if (hadFluid) {
            for (int32_t index = 0; index < static_cast<int32_t>(SubchunkData::CELLS); ++index) {
                if (!wet[index]) subchunk.removeFluid(index);
            }
        }
    }

    remesh(chunk, &changed);
}

void ClientWorld::predict(BlockCoord pos, BlockTypeId block, std::chrono::milliseconds hold) {
    BlockTypeId before = world_.getBlock(pos);
    {
        std::lock_guard lock(predictionMutex_);
        auto [it, inserted] = predictions_.try_emplace(pos);
        // A second prediction on a cell is still measured against what the server last said
        if (inserted) it->second.before = before;
        it->second.predicted = block;
        ++it->second.made;
        it->second.until = Clock::now() + hold;
    }
    world_.setBlock(pos, block);
    if (auto* queue = world_.meshRebuildQueue()) {
        for (const ChunkPos& affected : world_.getAffectedSubChunks(pos)) {
            queue->push(affected, MeshRebuildRequest::immediate());
        }
    }
}

void ClientWorld::settle(Clock::time_point now) {
    std::vector<std::pair<BlockCoord, BlockTypeId>> ended;
    {
        std::lock_guard lock(predictionMutex_);
        for (auto it = predictions_.begin(); it != predictions_.end();) {
            if (it->second.until > now) {
                ++it;
                continue;
            }
            ended.emplace_back(it->first, it->second.serverSays.value_or(it->second.before));
            it = predictions_.erase(it);
        }
    }
    for (const auto& [pos, block] : ended) {
        if (world_.getBlock(pos) == block) continue;
        world_.setBlock(pos, block);
        if (auto* queue = world_.meshRebuildQueue()) {
            for (const ChunkPos& affected : world_.getAffectedSubChunks(pos)) {
                queue->push(affected, MeshRebuildRequest::immediate());
            }
        }
    }
}

uint8_t ClientWorld::packedLight(BlockCoord pos) const {
    constexpr uint8_t OPEN_SKY = 0xF0;
    const SubChunk* subchunk = world_.getSubChunk(ChunkPos(pos.x >> 4, pos.y >> 4, pos.z >> 4));
    if (!subchunk) return OPEN_SKY;
    int32_t lx = pos.x & 0xF, ly = pos.y & 0xF, lz = pos.z & 0xF;
    return static_cast<uint8_t>((subchunk->getSkyLight(lx, ly, lz) << 4) | subchunk->getBlockLight(lx, ly, lz));
}

}  // namespace finevox::net
