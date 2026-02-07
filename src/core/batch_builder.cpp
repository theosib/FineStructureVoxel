#include "finevox/core/batch_builder.hpp"
#include "finevox/core/world.hpp"
#include <unordered_set>
#include <limits>

namespace finevox {

void BatchBuilder::setBlock(BlockPos pos, BlockTypeId type) {
    changes_[pos.pack()] = type;
}

void BatchBuilder::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    setBlock(BlockPos(x, y, z), type);
}

void BatchBuilder::cancel(BlockPos pos) {
    changes_.erase(pos.pack());
}

void BatchBuilder::clear() {
    changes_.clear();
}

std::optional<BlockTypeId> BatchBuilder::getChange(BlockPos pos) const {
    auto it = changes_.find(pos.pack());
    if (it != changes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool BatchBuilder::hasChange(BlockPos pos) const {
    return changes_.contains(pos.pack());
}

std::optional<BatchBuilder::Bounds> BatchBuilder::getBounds() const {
    if (changes_.empty()) {
        return std::nullopt;
    }

    Bounds bounds;
    bounds.min.x = std::numeric_limits<int32_t>::max();
    bounds.min.y = std::numeric_limits<int32_t>::max();
    bounds.min.z = std::numeric_limits<int32_t>::max();
    bounds.max.x = std::numeric_limits<int32_t>::min();
    bounds.max.y = std::numeric_limits<int32_t>::min();
    bounds.max.z = std::numeric_limits<int32_t>::min();

    for (const auto& [packed, type] : changes_) {
        BlockPos pos = BlockPos::unpack(packed);
        bounds.min.x = std::min(bounds.min.x, pos.x);
        bounds.min.y = std::min(bounds.min.y, pos.y);
        bounds.min.z = std::min(bounds.min.z, pos.z);
        bounds.max.x = std::max(bounds.max.x, pos.x);
        bounds.max.y = std::max(bounds.max.y, pos.y);
        bounds.max.z = std::max(bounds.max.z, pos.z);
    }

    return bounds;
}

std::vector<ColumnPos> BatchBuilder::getAffectedColumns() const {
    std::unordered_set<uint64_t> columnSet;

    for (const auto& [packed, type] : changes_) {
        BlockPos pos = BlockPos::unpack(packed);
        ColumnPos colPos = ColumnPos::fromBlock(pos);
        columnSet.insert(colPos.pack());
    }

    std::vector<ColumnPos> result;
    result.reserve(columnSet.size());
    for (uint64_t packed : columnSet) {
        result.push_back(ColumnPos::unpack(packed));
    }
    return result;
}

size_t BatchBuilder::commit(World& world) {
    size_t changed = 0;

    for (const auto& [packed, newType] : changes_) {
        BlockPos pos = BlockPos::unpack(packed);
        BlockTypeId oldType = world.getBlock(pos);

        if (oldType != newType) {
            world.setBlock(pos, newType);
            ++changed;
        }
    }

    clear();
    return changed;
}

std::vector<BlockPos> BatchBuilder::commitAndGetChanged(World& world) {
    std::vector<BlockPos> changedPositions;

    for (const auto& [packed, newType] : changes_) {
        BlockPos pos = BlockPos::unpack(packed);
        BlockTypeId oldType = world.getBlock(pos);

        if (oldType != newType) {
            world.setBlock(pos, newType);
            changedPositions.push_back(pos);
        }
    }

    clear();
    return changedPositions;
}

void BatchBuilder::forEach(const std::function<ChangeCallback>& callback) const {
    for (const auto& [packed, type] : changes_) {
        callback(BlockPos::unpack(packed), type);
    }
}

void BatchBuilder::merge(const BatchBuilder& other) {
    for (const auto& [packed, type] : other.changes_) {
        changes_[packed] = type;
    }
}

BatchResult commitBatchWithHistory(BatchBuilder& batch, World& world) {
    BatchResult result;
    result.bounds = batch.getBounds();

    batch.forEach([&](BlockPos pos, BlockTypeId newType) {
        BlockTypeId oldType = world.getBlock(pos);

        if (oldType != newType) {
            world.setBlock(pos, newType);
            result.changes.push_back({pos, oldType, newType});
            ++result.blocksChanged;
        }
    });

    batch.clear();
    return result;
}

}  // namespace finevox
