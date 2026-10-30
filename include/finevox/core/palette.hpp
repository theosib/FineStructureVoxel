#pragma once

/**
 * @file palette.hpp
 * @brief Per-subchunk block type mapping
 *
 * Design: [04-core-data-structures.md] §4.4 SubChunkPalette
 */

#include <cstdint>
#include <vector>
#include <array>
#include <atomic>
#include <memory>
#include <unordered_map>
#include "finevox/core/string_interner.hpp"

namespace finevox {

// Per-subchunk palette for compact block storage
// Maps global BlockTypeId to local indices (0-N where N is number of unique types)
//
// Design:
// - Runtime: Uses 16-bit indices uniformly for simplicity (no repacking on palette growth)
// - Disk: Uses exact bit width based on max index after compaction (1-16 bits)
// - Air is always at index 0
// - Reuses freed IDs to prevent counter wrap (free list)
//
// At save time:
// 1. Call compact() to remove unused entries and reassign IDs contiguously
// 2. Call bitsForSerialization() to get exact bit width needed
// 3. Pack block array using that bit width
//
class SubChunkPalette {
public:
    // Local index type - 16 bits at runtime for simplicity
    using LocalIndex = uint16_t;
    static constexpr LocalIndex INVALID_LOCAL_INDEX = UINT16_MAX;

    SubChunkPalette();

    SubChunkPalette(const SubChunkPalette&) = delete;
    SubChunkPalette& operator=(const SubChunkPalette&) = delete;

    // Add a block type to palette, returning its local index
    // Returns existing index if already in palette
    // Reuses freed IDs from removeType() before allocating new ones
    [[nodiscard]] LocalIndex addType(BlockTypeId globalId);

    // Remove a block type from the palette (when usage drops to 0)
    // The ID becomes available for reuse by future addType() calls
    // Returns true if the type was in the palette
    bool removeType(BlockTypeId globalId);

    // Get global ID from local index
    // Returns AIR_BLOCK_TYPE if index invalid or slot is empty
    // Safe from any thread, during writes. Everything else here belongs to the writer.
    [[nodiscard]] BlockTypeId getGlobalId(LocalIndex localIndex) const {
        uint32_t capacity = readCapacity_.load(std::memory_order_acquire);
        if (localIndex >= capacity) return AIR_BLOCK_TYPE;
        return readEntries_.load(std::memory_order_acquire)[localIndex].load(std::memory_order_relaxed);
    }

    // Get local index for a global ID
    // Returns INVALID_LOCAL_INDEX if not in palette
    [[nodiscard]] LocalIndex getLocalIndex(BlockTypeId globalId) const;

    // Check if a global ID is in the palette
    [[nodiscard]] bool contains(BlockTypeId globalId) const;

    // Number of active entries in palette (not including freed slots)
    [[nodiscard]] size_t activeCount() const { return reverse_.size(); }

    // Highest index currently in use (for bitsForSerialization)
    // This is what matters for bit width calculation
    [[nodiscard]] LocalIndex maxIndex() const { return maxIndex_; }

    // Exact bits needed to represent max index for serialization
    // Uses ceil(log2(maxIndex + 1))
    // After compaction, this gives the minimum bits needed on disk
    [[nodiscard]] int bitsForSerialization() const;

    // Get all entries (for serialization) - may contain empty slots
    [[nodiscard]] const std::vector<BlockTypeId>& entries() const { return palette_; }

    // Clear and reset palette (keeps air at index 0)
    void clear();

    // Shrink palette by removing gaps and reassigning IDs contiguously
    // Takes a usage count for each local index
    // Returns mapping from old index -> new index (INVALID_LOCAL_INDEX if removed)
    // After this, bitsForSerialization() returns the minimum bits needed
    // Also clears the free list since all IDs are now contiguous
    [[nodiscard]] std::vector<LocalIndex> compact(const std::vector<uint32_t>& usageCounts);

    // Check if compaction would be beneficial
    // Returns true if there are freed slots that could be reclaimed
    [[nodiscard]] bool needsCompaction() const { return !freeList_.empty(); }

private:
    // What readers see: palette_ again, in storage that stays put. A vector can't do this,
    // since growing frees the buffer a reader may be in. Past the inline entries, each
    // buffer is kept until the palette goes, at no more than double the final size.
    // These come first and together: most subchunks read one cache line here.
    static constexpr uint32_t INLINE_ENTRIES = 12;
    using Entry = std::atomic<BlockTypeId>;

    alignas(64) std::atomic<Entry*> readEntries_{nullptr};
    std::atomic<uint32_t> readCapacity_{INLINE_ENTRIES};  // Never above readEntries_'s real size
    std::array<Entry, INLINE_ENTRIES> inlineEntries_;

    std::vector<BlockTypeId> palette_;  // Index -> global ID (may have empty slots)
    std::unordered_map<BlockTypeId, LocalIndex> reverse_;  // Global ID -> index
    std::vector<LocalIndex> freeList_;  // Freed indices available for reuse
    LocalIndex maxIndex_ = 0;  // Highest index currently in use
    std::vector<std::unique_ptr<Entry[]>> spilledEntries_;

    void publish(LocalIndex index, BlockTypeId globalId);
    void publishAll();
};

// Utility: compute ceil(log2(n)) - bits needed to represent values 0 to n-1
// Returns 0 for n <= 1
[[nodiscard]] constexpr int ceilLog2(uint32_t n) {
    if (n <= 1) return 0;
    // Count leading zeros, subtract from 32, but need ceil not floor
    // For n=2: need 1 bit. For n=3: need 2 bits. For n=4: need 2 bits.
    int bits = 32 - __builtin_clz(n - 1);
    return bits;
}

}  // namespace finevox
