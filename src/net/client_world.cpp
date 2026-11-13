#include "finevox/net/client_world.hpp"

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

void ClientWorld::remesh(ChunkPos chunk, bool neighbours) {
    auto* queue = world_.meshRebuildQueue();
    if (!queue) return;
    queue->push(chunk, MeshRebuildRequest::immediate());
    if (!neighbours) return;
    // Their border faces were drawn against what used to be here
    for (Face face : {Face::PosX, Face::NegX, Face::PosZ, Face::NegZ, Face::PosY, Face::NegY}) {
        auto n = faceNormal(face);
        queue->push(ChunkPos(chunk.x + n[0], chunk.y + n[1], chunk.z + n[2]), MeshRebuildRequest::normal());
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

    std::vector<std::pair<BlockCoord, BlockTypeId>> changes;
    {
        std::lock_guard lock(predictionMutex_);
        for (int32_t index = 0; index < static_cast<int32_t>(SubchunkData::CELLS); ++index) {
            int32_t lx = index & 0xF, lz = (index >> 4) & 0xF, ly = (index >> 8) & 0xF;
            BlockCoord world(chunk.x * 16 + lx, chunk.y * 16 + ly, chunk.z * 16 + lz);
            BlockTypeId block = palette[data.indices.empty() ? 0 : data.indices[index]];

            if (!predictions_.empty()) {
                auto predicted = predictions_.find(world);
                if (predicted != predictions_.end()) {
                    predicted->second.serverSays = block;
                    continue;
                }
            }
            if (world_.getBlock(world) != block) changes.emplace_back(world, block);
        }
    }
    if (!changes.empty()) world_.setBlocks(changes);

    SubChunk& subchunk = world_.getOrCreateColumn(ColumnPos(chunk.x, chunk.z)).getOrCreateSubChunk(chunk.y);
    subchunk.setLightData(data.light);
    subchunk.setBiomeData(data.biomes);

    bool hadFluid = subchunk.fluidLayer() != nullptr;
    if (hadFluid || !data.fluids.empty()) {
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

    remesh(chunk, true);
}

void ClientWorld::predict(BlockCoord pos, BlockTypeId block, std::chrono::milliseconds hold) {
    BlockTypeId before = world_.getBlock(pos);
    {
        std::lock_guard lock(predictionMutex_);
        auto [it, inserted] = predictions_.try_emplace(pos);
        // A second prediction on a cell is still measured against what the server last said
        if (inserted) it->second.before = before;
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
