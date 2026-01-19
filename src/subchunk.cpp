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
    LocalIndex oldIndex = blocks_[toIndex(x, y, z)];
    BlockTypeId oldType = palette_.getGlobalId(oldIndex);

    // No change needed
    if (oldType == type) {
        return;
    }

    // Perform the actual block change
    setBlockInternal(toIndex(x, y, z), type, oldType);

    // Increment block version (signals mesh needs rebuild)
    blockVersion_.fetch_add(1, std::memory_order_release);

    // Notify callback if set
    if (blockChangeCallback_) {
        blockChangeCallback_(position_, x, y, z, oldType, type);
    }
}

void SubChunk::setBlock(int32_t index, BlockTypeId type) {
    LocalIndex oldIndex = blocks_[index];
    BlockTypeId oldType = palette_.getGlobalId(oldIndex);

    // No change needed
    if (oldType == type) {
        return;
    }

    // Perform the actual block change
    setBlockInternal(index, type, oldType);

    // Increment block version (signals mesh needs rebuild)
    blockVersion_.fetch_add(1, std::memory_order_release);

    // Notify callback if set (convert index to coordinates)
    if (blockChangeCallback_) {
        int32_t x = index % SIZE;
        int32_t z = (index / SIZE) % SIZE;
        int32_t y = index / (SIZE * SIZE);
        blockChangeCallback_(position_, x, y, z, oldType, type);
    }
}

void SubChunk::setBlockInternal(int32_t index, BlockTypeId type, BlockTypeId oldType) {
    LocalIndex oldIndex = blocks_[index];

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
    bool wasNotEmpty = nonAirCount_ > 0;

    blocks_.fill(0);
    palette_.clear();
    usageCounts_.clear();
    usageCounts_.push_back(VOLUME);  // Air has all blocks
    nonAirCount_ = 0;

    // Increment block version if there was any content
    if (wasNotEmpty) {
        blockVersion_.fetch_add(1, std::memory_order_release);
    }
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

    // Increment block version
    blockVersion_.fetch_add(1, std::memory_order_release);
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
