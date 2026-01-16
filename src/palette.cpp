#include "finevox/palette.hpp"
#include <algorithm>

namespace finevox {

SubChunkPalette::SubChunkPalette() {
    // Air is always at index 0
    palette_.push_back(AIR_BLOCK_TYPE);
    reverse_[AIR_BLOCK_TYPE] = 0;
    maxIndex_ = 0;
}

SubChunkPalette::LocalIndex SubChunkPalette::addType(BlockTypeId globalId) {
    // Check if already in palette
    auto it = reverse_.find(globalId);
    if (it != reverse_.end()) {
        return it->second;
    }

    LocalIndex index;

    // Try to reuse a freed slot first
    if (!freeList_.empty()) {
        index = freeList_.back();
        freeList_.pop_back();
        palette_[index] = globalId;
    } else {
        // Allocate new slot
        index = static_cast<LocalIndex>(palette_.size());
        palette_.push_back(globalId);
    }

    reverse_[globalId] = index;

    // Update max index if needed
    if (index > maxIndex_) {
        maxIndex_ = index;
    }

    return index;
}

bool SubChunkPalette::removeType(BlockTypeId globalId) {
    // Can't remove air
    if (globalId == AIR_BLOCK_TYPE) {
        return false;
    }

    auto it = reverse_.find(globalId);
    if (it == reverse_.end()) {
        return false;
    }

    LocalIndex index = it->second;

    // Mark slot as empty and add to free list
    palette_[index] = AIR_BLOCK_TYPE;  // Use air as "empty" marker
    reverse_.erase(it);
    freeList_.push_back(index);

    // Note: we don't update maxIndex_ here - it would require scanning
    // The compact() function will properly reset it

    return true;
}

BlockTypeId SubChunkPalette::getGlobalId(LocalIndex localIndex) const {
    if (localIndex >= palette_.size()) {
        return AIR_BLOCK_TYPE;
    }
    return palette_[localIndex];
}

SubChunkPalette::LocalIndex SubChunkPalette::getLocalIndex(BlockTypeId globalId) const {
    auto it = reverse_.find(globalId);
    if (it != reverse_.end()) {
        return it->second;
    }
    return INVALID_LOCAL_INDEX;
}

bool SubChunkPalette::contains(BlockTypeId globalId) const {
    return reverse_.contains(globalId);
}

int SubChunkPalette::bitsForSerialization() const {
    // Use maxIndex + 1 since we need to represent values 0 to maxIndex
    return ceilLog2(static_cast<uint32_t>(maxIndex_ + 1));
}

void SubChunkPalette::clear() {
    palette_.clear();
    reverse_.clear();
    freeList_.clear();
    // Air is always at index 0
    palette_.push_back(AIR_BLOCK_TYPE);
    reverse_[AIR_BLOCK_TYPE] = 0;
    maxIndex_ = 0;
}

std::vector<SubChunkPalette::LocalIndex> SubChunkPalette::compact(
    const std::vector<uint32_t>& usageCounts
) {
    std::vector<LocalIndex> mapping(palette_.size(), INVALID_LOCAL_INDEX);

    // Build new palette with only used entries, contiguously assigned
    std::vector<BlockTypeId> newPalette;
    std::unordered_map<BlockTypeId, LocalIndex> newReverse;

    // Air is always kept at index 0
    newPalette.push_back(AIR_BLOCK_TYPE);
    newReverse[AIR_BLOCK_TYPE] = 0;
    mapping[0] = 0;

    // Add other used entries contiguously
    for (size_t i = 1; i < palette_.size(); ++i) {
        BlockTypeId type = palette_[i];
        // Skip empty slots (air markers from removeType) and unused entries
        if (type != AIR_BLOCK_TYPE && i < usageCounts.size() && usageCounts[i] > 0) {
            LocalIndex newIndex = static_cast<LocalIndex>(newPalette.size());
            newPalette.push_back(type);
            newReverse[type] = newIndex;
            mapping[i] = newIndex;
        }
    }

    palette_ = std::move(newPalette);
    reverse_ = std::move(newReverse);
    freeList_.clear();  // All slots are now contiguous
    maxIndex_ = palette_.empty() ? 0 : static_cast<LocalIndex>(palette_.size() - 1);

    return mapping;
}

}  // namespace finevox
