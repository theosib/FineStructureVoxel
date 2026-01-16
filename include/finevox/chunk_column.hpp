#pragma once

#include "finevox/position.hpp"
#include "finevox/subchunk.hpp"
#include <unordered_map>
#include <memory>
#include <optional>
#include <functional>

namespace finevox {

// A vertical column of SubChunks at a given (X, Z) position
// - Uses sparse storage: SubChunks only exist when they contain non-air blocks
// - Automatically creates SubChunks when blocks are set
// - Automatically removes SubChunks when they become all air
//
// Y range: supports the full Y range from position.hpp (±2048 blocks = ±128 subchunks)
//
class ChunkColumn {
public:
    explicit ChunkColumn(ColumnPos pos);

    // Column position
    [[nodiscard]] ColumnPos position() const { return pos_; }

    // Get block at absolute world coordinates
    // Returns AIR_BLOCK_TYPE if position is outside loaded subchunks
    [[nodiscard]] BlockTypeId getBlock(BlockPos pos) const;
    [[nodiscard]] BlockTypeId getBlock(int32_t x, int32_t y, int32_t z) const;

    // Set block at absolute world coordinates
    // Creates SubChunk if needed, removes it if it becomes all air
    void setBlock(BlockPos pos, BlockTypeId type);
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type);

    // Check if a subchunk exists at the given chunk Y coordinate
    [[nodiscard]] bool hasSubChunk(int32_t chunkY) const;

    // Get subchunk at the given chunk Y coordinate (nullptr if doesn't exist)
    [[nodiscard]] SubChunk* getSubChunk(int32_t chunkY);
    [[nodiscard]] const SubChunk* getSubChunk(int32_t chunkY) const;

    // Get or create subchunk at the given chunk Y coordinate
    [[nodiscard]] SubChunk& getOrCreateSubChunk(int32_t chunkY);

    // Remove empty subchunks (called periodically or before save)
    void pruneEmptySubChunks();

    // Number of subchunks currently allocated
    [[nodiscard]] size_t subChunkCount() const { return subChunks_.size(); }

    // Check if entire column is empty (no non-air blocks)
    [[nodiscard]] bool isEmpty() const { return subChunks_.empty(); }

    // Total non-air block count across all subchunks
    [[nodiscard]] int64_t nonAirCount() const;

    // Iterate over all existing subchunks
    // Callback receives (chunkY, SubChunk&)
    void forEachSubChunk(const std::function<void(int32_t, SubChunk&)>& callback);
    void forEachSubChunk(const std::function<void(int32_t, const SubChunk&)>& callback) const;

    // Get Y bounds of non-empty subchunks (nullopt if column is empty)
    [[nodiscard]] std::optional<std::pair<int32_t, int32_t>> getYBounds() const;

    // Compact all subchunk palettes (for serialization)
    void compactAll();

private:
    ColumnPos pos_;
    std::unordered_map<int32_t, std::unique_ptr<SubChunk>> subChunks_;

    // Convert block Y to subchunk Y (handles negative correctly)
    [[nodiscard]] static int32_t blockYToChunkY(int32_t blockY);

    // Convert block Y to local Y within subchunk
    [[nodiscard]] static int32_t blockYToLocalY(int32_t blockY);
};

}  // namespace finevox
