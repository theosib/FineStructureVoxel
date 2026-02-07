#pragma once

/**
 * @file chunk_column.hpp
 * @brief Vertical column of SubChunks at a given (X, Z) position
 *
 * Design: [05-world-management.md] §5.1, §5.2 ChunkColumn
 * Heightmap: [09-lighting.md] §9.1 Sky Light
 * Activity timer: [24-event-system.md] Cross-chunk update protection
 */

#include "finevox/core/position.hpp"
#include "finevox/core/subchunk.hpp"
#include <unordered_map>
#include <memory>
#include <optional>
#include <functional>
#include <array>
#include <cstdint>
#include <limits>
#include <chrono>

namespace finevox {

// Forward declaration
class DataContainer;

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
    ~ChunkColumn();  // Defined in .cpp for unique_ptr<DataContainer>

    // Allow move, prevent copy
    ChunkColumn(ChunkColumn&&) noexcept;
    ChunkColumn& operator=(ChunkColumn&&) noexcept;
    ChunkColumn(const ChunkColumn&) = delete;
    ChunkColumn& operator=(const ChunkColumn&) = delete;

    // Column position
    [[nodiscard]] ColumnPos position() const { return pos_; }

    // ========================================================================
    // Coordinate Conversion
    // ========================================================================

    // Get ChunkPos for a subchunk at the given Y level (subchunk Y coordinate)
    [[nodiscard]] ChunkPos toChunkPos(int32_t chunkY) const {
        return ChunkPos{pos_.x, chunkY, pos_.z};
    }

    // Convert world block Y to subchunk Y coordinate
    [[nodiscard]] static int32_t worldYToChunkY(int32_t blockY) {
        // Arithmetic right shift handles negative coordinates correctly
        return blockY >> 4;
    }

    // Convert world block Y to local Y within subchunk (0-15)
    [[nodiscard]] static int32_t worldYToLocalY(int32_t blockY) {
        return blockY & 0xF;
    }

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

    // ========================================================================
    // Column Extra Data (per-column game state)
    // ========================================================================
    // Used for column-level state like pending block events when unloading
    // mid-update, biome data, etc. Game modules define the format.

    /// Get column-level extra data
    /// @return Pointer to DataContainer, or nullptr if no data exists
    [[nodiscard]] DataContainer* data();
    [[nodiscard]] const DataContainer* data() const;

    /// Get or create column-level extra data
    DataContainer& getOrCreateData();

    /// Check if column has extra data
    [[nodiscard]] bool hasData() const;

    /// Remove column-level extra data
    void removeData();

    // ========================================================================
    // Game Tick Registry (called after loading)
    // ========================================================================

    /// Rebuild game tick registries for all subchunks in this column
    /// Call this after loading a column from disk so that blocks with
    /// wantsGameTicks() are properly registered.
    /// Requires that all block type modules are already loaded.
    void rebuildGameTickRegistries();

    // ========================================================================
    // Activity Timer (for cross-chunk update unload protection)
    // ========================================================================
    // When a block update event is delivered to this column, the activity
    // timer is touched. The chunk loading system respects this timer and
    // won't unload the column until the timer expires, preventing premature
    // unload during redstone-like propagation across chunk boundaries.

    /// Touch the activity timer (call when delivering BlockUpdate events)
    void touchActivity();

    /// Get time since last activity in milliseconds
    /// Returns a very large value if never touched
    [[nodiscard]] int64_t activityAgeMs() const;

    /// Check if activity timer has expired
    /// @param timeoutMs Timeout in milliseconds (default 5000 = 5 seconds)
    [[nodiscard]] bool activityExpired(int64_t timeoutMs = 5000) const;

    /// Get the last activity time point (for debugging/testing)
    [[nodiscard]] std::chrono::steady_clock::time_point lastActivityTime() const {
        return lastActivityTime_;
    }

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

    // Column-level extra data (pending events, biome data, etc.)
    std::unique_ptr<DataContainer> data_;

    // Activity timer for cross-chunk update protection
    // Initialized to epoch (very old) so activityExpired() returns true initially
    std::chrono::steady_clock::time_point lastActivityTime_{};

    // Convert local X,Z to heightmap index
    [[nodiscard]] static constexpr int32_t toHeightmapIndex(int32_t localX, int32_t localZ) {
        return localZ * 16 + localX;
    }
};

}  // namespace finevox
