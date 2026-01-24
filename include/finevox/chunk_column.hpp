#pragma once

#include "finevox/position.hpp"
#include "finevox/subchunk.hpp"
#include <unordered_map>
#include <memory>
#include <optional>
#include <functional>
#include <array>
#include <cstdint>
#include <limits>

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

    // Get shared pointer to subchunk (for mesh cache weak references)
    // Returns empty shared_ptr if subchunk doesn't exist
    [[nodiscard]] std::shared_ptr<SubChunk> getSubChunkShared(int32_t chunkY);

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

    // ========================================================================
    // Heightmap (for sky light calculation)
    // ========================================================================

    /// Get height of the highest sky-light-blocking block at local (x, z)
    /// Returns the Y coordinate of the highest opaque block + 1, or MIN_HEIGHT if none
    /// This is the Y where sky light starts being blocked
    [[nodiscard]] int32_t getHeight(int32_t localX, int32_t localZ) const;

    /// Update heightmap for a single column after block change
    /// Call this after setting a block that may affect sky light
    /// @param localX Local X within column (0-15)
    /// @param localZ Local Z within column (0-15)
    /// @param blockY World Y coordinate of the changed block
    /// @param blocksSkyLight Whether the new block type blocks sky light
    void updateHeight(int32_t localX, int32_t localZ, int32_t blockY, bool blocksSkyLight);

    /// Recalculate entire heightmap from block data
    /// Call this when loading a chunk or after major modifications
    void recalculateHeightmap();

    /// Get raw heightmap data for serialization (256 entries, one per X,Z column)
    [[nodiscard]] const std::array<int32_t, 256>& heightmapData() const { return heightmap_; }

    /// Set raw heightmap data from serialization
    void setHeightmapData(const std::array<int32_t, 256>& data);

    /// Check if the heightmap needs recalculation
    [[nodiscard]] bool heightmapDirty() const { return heightmapDirty_; }

    /// Mark heightmap as dirty (needs recalculation)
    void markHeightmapDirty() { heightmapDirty_ = true; }

    // ========================================================================
    // Light Initialization (for lazy sky light calculation)
    // ========================================================================

    /// Check if sky light has been initialized for this column
    /// Returns false if the column needs sky light calculation before meshing
    [[nodiscard]] bool isLightInitialized() const { return lightInitialized_; }

    /// Mark sky light as initialized (called after sky light propagation)
    void markLightInitialized() { lightInitialized_ = true; }

    /// Reset light initialization flag (e.g., after major terrain changes)
    void resetLightInitialized() { lightInitialized_ = false; }

private:
    ColumnPos pos_;
    std::unordered_map<int32_t, std::shared_ptr<SubChunk>> subChunks_;

    // Heightmap: Y coordinate of highest sky-light-blocking block + 1 for each (x, z)
    // Index = z * 16 + x
    // Value of INT32_MIN means no opaque blocks in this column
    static constexpr int32_t NO_HEIGHT = std::numeric_limits<int32_t>::min();
    std::array<int32_t, 256> heightmap_;
    bool heightmapDirty_ = true;

    // Light initialization: false until sky light is first calculated
    // Used for lazy initialization - mesher can wait for this before building
    bool lightInitialized_ = false;

    // Convert block Y to subchunk Y (handles negative correctly)
    [[nodiscard]] static int32_t blockYToChunkY(int32_t blockY);

    // Convert block Y to local Y within subchunk
    [[nodiscard]] static int32_t blockYToLocalY(int32_t blockY);

    // Convert local X,Z to heightmap index
    [[nodiscard]] static constexpr int32_t toHeightmapIndex(int32_t localX, int32_t localZ) {
        return localZ * 16 + localX;
    }
};

}  // namespace finevox
