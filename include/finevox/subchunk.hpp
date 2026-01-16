#pragma once

#include "finevox/position.hpp"
#include "finevox/palette.hpp"
#include <array>
#include <cstdint>

namespace finevox {

// A 16x16x16 block volume
// - Uses palette-based storage: each voxel stores a 16-bit local index
// - Maintains reference counts for palette entries to enable automatic removal
// - At save time, can compact the palette and use exact bit-width serialization
//
// Index layout: y*256 + z*16 + x (same as BlockPos::toLocalIndex)
// This groups blocks along X axis for better cache locality during horizontal iteration
//
class SubChunk {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE;  // 4096

    using LocalIndex = SubChunkPalette::LocalIndex;

    SubChunk();

    // Get block type at local coordinates (0-15 each)
    [[nodiscard]] BlockTypeId getBlock(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] BlockTypeId getBlock(int32_t index) const;

    // Set block type at local coordinates
    // Handles palette management and reference counting automatically
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type);
    void setBlock(int32_t index, BlockTypeId type);

    // Check if subchunk is entirely air (for optimization/culling)
    [[nodiscard]] bool isEmpty() const { return nonAirCount_ == 0; }

    // Count of non-air blocks
    [[nodiscard]] int32_t nonAirCount() const { return nonAirCount_; }

    // Access to palette for serialization
    [[nodiscard]] const SubChunkPalette& palette() const { return palette_; }
    [[nodiscard]] SubChunkPalette& palette() { return palette_; }

    // Access to raw block data for serialization
    [[nodiscard]] const std::array<LocalIndex, VOLUME>& blocks() const { return blocks_; }

    // Prepare for serialization by compacting the palette
    // Returns mapping from old indices to new indices
    // After this call, bitsForSerialization() returns minimum bits needed
    [[nodiscard]] std::vector<LocalIndex> compactPalette();

    // Check if the palette has unused entries that could be compacted
    [[nodiscard]] bool needsCompaction() const { return palette_.needsCompaction(); }

    // Clear all blocks to air
    void clear();

    // Fill entire subchunk with a single block type
    void fill(BlockTypeId type);

    // Get usage counts for each palette entry (for compaction)
    [[nodiscard]] std::vector<uint32_t> getUsageCounts() const { return usageCounts_; }

private:
    SubChunkPalette palette_;
    std::array<LocalIndex, VOLUME> blocks_;
    std::vector<uint32_t> usageCounts_;  // Reference count per local index
    int32_t nonAirCount_ = 0;

    // Convert local coordinates to array index
    [[nodiscard]] static constexpr int32_t toIndex(int32_t x, int32_t y, int32_t z) {
        return y * 256 + z * 16 + x;
    }

    // Update reference counts when changing a block
    void decrementUsage(LocalIndex oldIndex);
    void incrementUsage(LocalIndex newIndex);
};

}  // namespace finevox
