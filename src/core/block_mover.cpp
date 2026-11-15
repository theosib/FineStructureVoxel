#include "finevox/core/block_mover.hpp"

#include "finevox/core/block_type.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

#include <deque>
#include <memory>
#include <unordered_set>

namespace finevox {

std::optional<MoveRule> moveRuleNamed(std::string_view name) {
    if (name == "moves") return MoveRule::Moves;
    if (name == "fixed") return MoveRule::Fixed;
    if (name == "breaks") return MoveRule::Breaks;
    if (name == "no-drag") return MoveRule::NoDrag;
    return std::nullopt;
}

std::optional<MovePlan> planMove(const World& world, const MoveRequest& request) {
    const auto& registry = BlockRegistry::global();
    struct Step {
        BlockCoord pos;
        bool pushed;
    };
    std::deque<Step> work{{request.start, !request.pull}};
    std::unordered_set<BlockCoord> going, breaking;
    MovePlan plan;

    while (!work.empty()) {
        const Step step = work.front();
        work.pop_front();
        if (request.anchor && step.pos == *request.anchor) {
            if (step.pushed) return std::nullopt;
            continue;
        }
        if (going.contains(step.pos)) continue;
        const BlockTypeId id = world.getBlock(step.pos);
        if (id.isAir()) continue;
        const BlockType& type = registry.getType(id);
        switch (type.moveRule()) {
            case MoveRule::Fixed:
                if (step.pushed) return std::nullopt;
                continue;
            case MoveRule::Breaks:
                if (step.pushed && breaking.insert(step.pos).second) plan.breaks.push_back(step.pos);
                continue;
            case MoveRule::NoDrag:
                if (!step.pushed) continue;
                break;
            case MoveRule::Moves:
                break;
        }

        going.insert(step.pos);
        plan.moves.push_back(step.pos);
        if (static_cast<int32_t>(plan.moves.size()) > request.limit) return std::nullopt;

        work.push_back({step.pos.neighbor(request.dir), true});
        if (const uint32_t grip = type.gripGroup()) {
            for (size_t f = 0; f < FACE_COUNT; ++f) {
                const Face face = static_cast<Face>(f);
                if (face == request.dir) continue;  // what's ahead is pushed, not dragged
                const BlockCoord next = step.pos.neighbor(face);
                const BlockTypeId nextId = world.getBlock(next);
                if (nextId.isAir()) continue;
                const uint32_t other = registry.getType(nextId).gripGroup();
                if (other != 0 && other != grip) continue;
                work.push_back({next, false});
            }
        }
    }
    return plan;
}

void applyMove(World& world, const MovePlan& plan, Face dir, std::vector<BlockCoord>* changed) {
    struct Carried {
        BlockCoord to;
        BlockTypeId type;
        uint8_t rotation = 0;
        std::unique_ptr<DataContainer> data;
    };

    // Lift everything out first, so no block is written over before it moves
    std::vector<Carried> carried;
    carried.reserve(plan.moves.size());
    for (const BlockCoord& from : plan.moves) {
        Carried c{from.neighbor(dir), world.getBlock(from)};
        if (SubChunk* sub = world.getSubChunk(ChunkPos::fromBlock(from))) {
            const int32_t index = from.localIndex();
            c.rotation = sub->getRotationIndex(index);
            auto& data = sub->allBlockData();
            if (auto it = data.find(index); it != data.end()) {
                c.data = std::move(it->second);
                data.erase(it);
            }
        }
        carried.push_back(std::move(c));
    }

    auto clear = [&](const BlockCoord& pos) {
        world.setBlock(pos, AIR_BLOCK_TYPE);
        if (SubChunk* sub = world.getSubChunk(ChunkPos::fromBlock(pos))) {
            sub->setRotationIndex(pos.localIndex(), 0);
            sub->removeBlockData(pos.localIndex());
        }
    };
    for (const BlockCoord& pos : plan.breaks) clear(pos);
    for (const BlockCoord& pos : plan.moves) clear(pos);

    for (Carried& c : carried) {
        world.setBlock(c.to, c.type);
        SubChunk* sub = world.getSubChunk(ChunkPos::fromBlock(c.to));
        if (!sub) continue;
        const int32_t index = c.to.localIndex();
        sub->setRotationIndex(index, c.rotation);
        if (c.data) sub->allBlockData()[index] = std::move(c.data);
    }

    std::vector<BlockMove> moves;
    moves.reserve(carried.size());
    for (const BlockCoord& from : plan.moves) moves.push_back({from, from.neighbor(dir)});
    world.notifyBlocksMoved(moves);

    if (changed) {
        std::unordered_set<BlockCoord> seen;
        auto note = [&](const BlockCoord& pos) {
            if (seen.insert(pos).second) changed->push_back(pos);
        };
        for (const BlockCoord& pos : plan.breaks) note(pos);
        for (const BlockCoord& pos : plan.moves) {
            note(pos);
            note(pos.neighbor(dir));
        }
    }
}

}  // namespace finevox
