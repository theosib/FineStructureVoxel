#include "finevox/subchunk.hpp"

namespace finevox {

SubChunk::SubChunk() {
    // Initialize all blocks to air (index 0)
    blocks_.fill(0);
    // Air starts with count equal to volume
    usageCounts_.push_back(VOLUME);
}

BlockTypeId SubChunk::getBlock(int32_t x, int32_t y, int32_t z) const {
    return getBlock(toIndex(x, y, z));
}

BlockTypeId SubChunk::getBlock(int32_t index) const {
    LocalIndex localIdx = blocks_[index];
    return palette_.getGlobalId(localIdx);
}

void SubChunk::setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
    setBlock(toIndex(x, y, z), type);
}

void SubChunk::setBlock(int32_t index, BlockTypeId type) {
    LocalIndex oldIndex = blocks_[index];
    BlockTypeId oldType = palette_.getGlobalId(oldIndex);

    // No change needed
    if (oldType == type) {
        return;
    }

    // Get or create local index for new type
    LocalIndex newIndex = palette_.addType(type);

    // Ensure usageCounts_ is large enough
    if (newIndex >= usageCounts_.size()) {
        usageCounts_.resize(newIndex + 1, 0);
    }

    // Update reference counts
    decrementUsage(oldIndex);
    incrementUsage(newIndex);

    // Update the block array
    blocks_[index] = newIndex;

    // Track non-air count
    if (oldType.isAir() && !type.isAir()) {
        ++nonAirCount_;
    } else if (!oldType.isAir() && type.isAir()) {
        --nonAirCount_;
    }
}

void SubChunk::decrementUsage(LocalIndex index) {
    if (index < usageCounts_.size() && usageCounts_[index] > 0) {
        --usageCounts_[index];
        // If usage drops to zero and it's not air, remove from palette
        if (usageCounts_[index] == 0 && index != 0) {
            BlockTypeId type = palette_.getGlobalId(index);
            if (!type.isAir()) {
                palette_.removeType(type);
            }
        }
    }
}

void SubChunk::incrementUsage(LocalIndex index) {
    if (index >= usageCounts_.size()) {
        usageCounts_.resize(index + 1, 0);
    }
    ++usageCounts_[index];
}

void SubChunk::clear() {
    blocks_.fill(0);
    palette_.clear();
    usageCounts_.clear();
    usageCounts_.push_back(VOLUME);  // Air has all blocks
    nonAirCount_ = 0;
}

void SubChunk::fill(BlockTypeId type) {
    if (type.isAir()) {
        clear();
        return;
    }

    // Get or create local index for the type
    LocalIndex index = palette_.addType(type);

    // Fill all blocks with this index
    blocks_.fill(index);

    // Reset usage counts
    usageCounts_.clear();
    usageCounts_.resize(index + 1, 0);
    usageCounts_[index] = VOLUME;

    // Clear palette of unused entries (everything except air and the fill type)
    palette_.clear();
    (void)palette_.addType(type);  // Re-add the type (index will be 1)

    // Update blocks to use new index after palette clear
    blocks_.fill(1);
    usageCounts_.clear();
    usageCounts_.resize(2, 0);
    usageCounts_[0] = 0;  // Air
    usageCounts_[1] = VOLUME;

    nonAirCount_ = VOLUME;
}

std::vector<SubChunk::LocalIndex> SubChunk::compactPalette() {
    auto mapping = palette_.compact(usageCounts_);

    // Remap all block indices
    for (auto& blockIndex : blocks_) {
        LocalIndex newIndex = mapping[blockIndex];
        if (newIndex != SubChunkPalette::INVALID_LOCAL_INDEX) {
            blockIndex = newIndex;
        } else {
            // This shouldn't happen if usageCounts_ is accurate
            blockIndex = 0;  // Default to air
        }
    }

    // Rebuild usage counts for the compacted palette
    usageCounts_.clear();
    usageCounts_.resize(palette_.entries().size(), 0);
    for (auto blockIndex : blocks_) {
        ++usageCounts_[blockIndex];
    }

    return mapping;
}

}  // namespace finevox
