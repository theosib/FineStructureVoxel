#include "finevox/core/palette.hpp"
#include <algorithm>

namespace finevox {

SubChunkPalette::SubChunkPalette() {
    for (Entry& entry : inlineEntries_) entry.store(AIR_BLOCK_TYPE, std::memory_order_relaxed);
    readEntries_.store(inlineEntries_.data(), std::memory_order_relaxed);

    // Air is always at index 0
    palette_.push_back(AIR_BLOCK_TYPE);
    reverse_[AIR_BLOCK_TYPE] = 0;
    maxIndex_ = 0;
}

void SubChunkPalette::publish(LocalIndex index, BlockTypeId globalId) {
    uint32_t capacity = readCapacity_.load(std::memory_order_relaxed);
    Entry* entries = readEntries_.load(std::memory_order_relaxed);

    if (index >= capacity) {
        uint32_t bigger = capacity;
        while (index >= bigger) bigger *= 2;

        auto grown = std::make_unique<Entry[]>(bigger);
        for (uint32_t i = 0; i < capacity; ++i) {
            grown[i].store(entries[i].load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
        for (uint32_t i = capacity; i < bigger; ++i) {
            grown[i].store(AIR_BLOCK_TYPE, std::memory_order_relaxed);
        }

        entries = grown.get();
        spilledEntries_.push_back(std::move(grown));
        // Pointer first: a reader must never pair a capacity with a smaller buffer
        readEntries_.store(entries, std::memory_order_release);
        readCapacity_.store(bigger, std::memory_order_release);
    }

    entries[index].store(globalId, std::memory_order_release);
}

void SubChunkPalette::publishAll() {
    uint32_t capacity = readCapacity_.load(std::memory_order_relaxed);
    Entry* entries = readEntries_.load(std::memory_order_relaxed);
    for (size_t i = palette_.size(); i < capacity; ++i) {
        entries[i].store(AIR_BLOCK_TYPE, std::memory_order_release);
    }
    for (size_t i = 0; i < palette_.size(); ++i) {
        publish(static_cast<LocalIndex>(i), palette_[i]);
    }
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

    publish(index, globalId);
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
    publish(index, AIR_BLOCK_TYPE);
    reverse_.erase(it);
    freeList_.push_back(index);

    // Note: we don't update maxIndex_ here - it would require scanning
    // The compact() function will properly reset it

    return true;
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
    publishAll();
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
    publishAll();

    return mapping;
}

}  // namespace finevox
