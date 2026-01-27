#pragma once

#include "finevox/position.hpp"
#include "finevox/palette.hpp"
#include "finevox/rotation.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>

namespace finevox {

// Forward declaration
class DataContainer;

// Callback type for block change notifications
// Parameters: subchunk position, local block position, old block type, new block type
using BlockChangeCallback = std::function<void(ChunkPos pos, LocalBlockPos local,
                                               BlockTypeId oldType, BlockTypeId newType)>;

// A 16x16x16 block volume
// - Uses palette-based storage: each voxel stores a 16-bit local index
// - Maintains reference counts for palette entries to enable automatic removal
// - At save time, can compact the palette and use exact bit-width serialization
// - Also stores per-block light data (4096 bytes)
//
// Index layout: y*256 + z*16 + x (same as BlockPos::toLocalIndex)
// This groups blocks along X axis for better cache locality during horizontal iteration
//
class SubChunk {
public:
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t VOLUME = SIZE * SIZE * SIZE;  // 4096

    // Light constants
    static constexpr uint8_t MAX_LIGHT = 15;
    static constexpr uint8_t NO_LIGHT = 0;

    using LocalIndex = SubChunkPalette::LocalIndex;

    SubChunk();
    ~SubChunk();  // Defined in .cpp for unique_ptr<DataContainer>

    // Non-copyable, non-movable (stored in shared_ptr, has atomic members)
    SubChunk(const SubChunk&) = delete;
    SubChunk& operator=(const SubChunk&) = delete;
    SubChunk(SubChunk&&) = delete;
    SubChunk& operator=(SubChunk&&) = delete;

    // Get block type at local position
    [[nodiscard]] BlockTypeId getBlock(LocalBlockPos pos) const;
    [[nodiscard]] BlockTypeId getBlock(uint16_t index) const;
    // Convenience overload for int32_t coordinates
    [[nodiscard]] BlockTypeId getBlock(int32_t x, int32_t y, int32_t z) const {
        return getBlock(LocalBlockPos{x, y, z});
    }

    // Set block type at local position
    // Handles palette management and reference counting automatically
    void setBlock(LocalBlockPos pos, BlockTypeId type);
    void setBlock(uint16_t index, BlockTypeId type);
    // Convenience overload for int32_t coordinates
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type) {
        setBlock(LocalBlockPos{x, y, z}, type);
    }

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

    // ========================================================================
    // Block Version Tracking (for mesh invalidation)
    // ========================================================================

    // Get current block version (incremented on every block change)
    // Version starts at 1; 0 means "no mesh built yet"
    [[nodiscard]] uint64_t blockVersion() const {
        return blockVersion_.load(std::memory_order_acquire);
    }

    // ========================================================================
    // Light Data Storage
    // ========================================================================
    // Light is stored as 1 byte per block: high nibble = sky light, low nibble = block light
    // Total: 4096 bytes per subchunk

    /// Get sky light level at local coordinates (0-15)
    [[nodiscard]] uint8_t getSkyLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getSkyLight(int32_t index) const;

    /// Get block light level at local coordinates (0-15)
    [[nodiscard]] uint8_t getBlockLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getBlockLight(int32_t index) const;

    /// Get combined light (max of sky and block light)
    [[nodiscard]] uint8_t getCombinedLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getCombinedLight(int32_t index) const;

    /// Get raw packed light value (sky in high nibble, block in low nibble)
    [[nodiscard]] uint8_t getPackedLight(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getPackedLight(int32_t index) const;

    /// Set sky light level at local coordinates
    void setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t level);
    void setSkyLight(int32_t index, uint8_t level);

    /// Set block light level at local coordinates
    void setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t level);
    void setBlockLight(int32_t index, uint8_t level);

    /// Set both sky and block light at once
    void setLight(int32_t x, int32_t y, int32_t z, uint8_t skyLight, uint8_t blockLight);
    void setLight(int32_t index, uint8_t skyLight, uint8_t blockLight);

    /// Set raw packed light value
    void setPackedLight(int32_t x, int32_t y, int32_t z, uint8_t packed);
    void setPackedLight(int32_t index, uint8_t packed);

    /// Clear all light to zero
    void clearLight();

    /// Fill all sky light to a value (e.g., MAX_LIGHT for above-ground exposed chunks)
    void fillSkyLight(uint8_t level);

    /// Fill all block light to a value
    void fillBlockLight(uint8_t level);

    /// Check if all light values are zero (completely dark)
    [[nodiscard]] bool isLightDark() const;

    /// Check if all sky light values are maximum (fully exposed to sky)
    [[nodiscard]] bool isFullSkyLight() const;

    /// Get raw light data for serialization (4096 bytes)
    [[nodiscard]] const std::array<uint8_t, VOLUME>& lightData() const { return light_; }

    /// Set raw light data from serialization
    void setLightData(const std::array<uint8_t, VOLUME>& data);

    /// Get light version (incremented on any light change)
    /// Used to detect when mesh needs rebuilding for smooth lighting
    [[nodiscard]] uint64_t lightVersion() const {
        return lightVersion_.load(std::memory_order_acquire);
    }

    // ========================================================================
    // Block Rotation Storage
    // ========================================================================
    // Each block stores a rotation index (0-23) representing one of 24 cube rotations.
    // Default is 0 (identity = no rotation).
    // Used for oriented blocks like stairs, logs, pistons, etc.

    /// Get rotation for block at local coordinates
    [[nodiscard]] Rotation getRotation(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] Rotation getRotation(int32_t index) const;
    [[nodiscard]] Rotation getRotation(LocalBlockPos pos) const;

    /// Get raw rotation index (0-23) for block at local coordinates
    [[nodiscard]] uint8_t getRotationIndex(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] uint8_t getRotationIndex(int32_t index) const;
    [[nodiscard]] uint8_t getRotationIndex(LocalBlockPos pos) const;

    /// Set rotation for block at local coordinates
    void setRotation(int32_t x, int32_t y, int32_t z, const Rotation& rotation);
    void setRotation(int32_t index, const Rotation& rotation);
    void setRotation(LocalBlockPos pos, const Rotation& rotation);

    /// Set rotation by index (0-23) for block at local coordinates
    void setRotationIndex(int32_t x, int32_t y, int32_t z, uint8_t rotationIndex);
    void setRotationIndex(int32_t index, uint8_t rotationIndex);
    void setRotationIndex(LocalBlockPos pos, uint8_t rotationIndex);

    /// Clear all rotations to identity (0)
    void clearRotations();

    /// Get raw rotation data for serialization (4096 bytes)
    [[nodiscard]] const std::array<uint8_t, VOLUME>& rotationData() const;

    /// Set raw rotation data from serialization
    void setRotationData(const std::array<uint8_t, VOLUME>& data);

    /// Check if all rotations are identity (useful for serialization optimization)
    [[nodiscard]] bool hasNonIdentityRotations() const;

    // ========================================================================
    // Block Extra Data (tile entity data - chests, signs, etc.)
    // ========================================================================
    // Most blocks have no extra data. Storage is sparse - only blocks with data
    // have entries in the map. Data uses standard hierarchy:
    //   "inventory" - for blocks with inventory (chests, hoppers)
    //   "geometry"  - for blocks with dynamic geometry based on neighbors
    //   "display"   - for blocks with custom display (signs, banners)
    //   Block-specific keys as needed

    /// Get extra data for a block at local index
    /// @return Pointer to DataContainer, or nullptr if no data exists
    [[nodiscard]] DataContainer* blockData(int32_t index);
    [[nodiscard]] const DataContainer* blockData(int32_t index) const;
    [[nodiscard]] DataContainer* blockData(int32_t x, int32_t y, int32_t z);
    [[nodiscard]] const DataContainer* blockData(int32_t x, int32_t y, int32_t z) const;

    /// Get or create extra data for a block at local index
    /// Creates a new DataContainer if none exists
    DataContainer& getOrCreateBlockData(int32_t index);
    DataContainer& getOrCreateBlockData(int32_t x, int32_t y, int32_t z);

    /// Check if a block has extra data
    [[nodiscard]] bool hasBlockData(int32_t index) const;
    [[nodiscard]] bool hasBlockData(int32_t x, int32_t y, int32_t z) const;

    /// Remove extra data for a block (no-op if none exists)
    void removeBlockData(int32_t index);
    void removeBlockData(int32_t x, int32_t y, int32_t z);

    /// Get count of blocks with extra data
    [[nodiscard]] size_t blockDataCount() const;

    /// Get access to block data map for iteration
    /// Prefer using this in .cpp files rather than templates for cleaner compilation
    [[nodiscard]] const std::unordered_map<int32_t, std::unique_ptr<DataContainer>>& allBlockData() const;
    [[nodiscard]] std::unordered_map<int32_t, std::unique_ptr<DataContainer>>& allBlockData();

    // ========================================================================
    // SubChunk Extra Data (per-subchunk game state)
    // ========================================================================
    // Used for subchunk-level state like lighting cache, biome blends, etc.
    // Game modules define the format.

    /// Get subchunk-level extra data
    /// @return Pointer to DataContainer, or nullptr if no data exists
    [[nodiscard]] DataContainer* data();
    [[nodiscard]] const DataContainer* data() const;

    /// Get or create subchunk-level extra data
    DataContainer& getOrCreateData();

    /// Check if subchunk has extra data
    [[nodiscard]] bool hasData() const;

    /// Remove subchunk-level extra data
    void removeData();

    // ========================================================================
    // Game Tick Registry
    // ========================================================================
    // Blocks that want game tick events register here (by local index).
    // Registry is auto-populated on chunk load based on BlockType::wantsGameTicks().
    // Registry is updated on block place/break.

    /// Get set of local block indices that want game ticks
    [[nodiscard]] const std::unordered_set<uint16_t>& gameTickBlocks() const { return gameTickBlocks_; }

    /// Register a block index to receive game ticks
    /// No-op if already registered
    void registerForGameTicks(int32_t index);

    /// Unregister a block index from game ticks
    /// No-op if not registered
    void unregisterFromGameTicks(int32_t index);

    /// Rebuild game tick registry by scanning all blocks
    /// Called on chunk load; checks each block's type against BlockRegistry
    void rebuildGameTickRegistry();

    /// Check if a specific block is registered for game ticks
    [[nodiscard]] bool isRegisteredForGameTicks(int32_t index) const;

    // ========================================================================
    // Change Notifications
    // ========================================================================

    // Set position (needed for change callbacks)
    void setPosition(ChunkPos pos) { position_ = pos; }
    [[nodiscard]] ChunkPos position() const { return position_; }

    // ========================================================================
    // Coordinate Conversion
    // ========================================================================

    // Convert local block position to world block position
    [[nodiscard]] BlockPos toWorld(LocalBlockPos local) const {
        return position_.toWorld(local);
    }

    // Convert local block index to world block position
    [[nodiscard]] BlockPos toWorld(uint16_t localIndex) const {
        return position_.toWorld(localIndex);
    }

    // Set callback for block changes (called on every setBlock that changes a block)
    // The callback receives the subchunk position and local coordinates
    void setBlockChangeCallback(BlockChangeCallback callback) { blockChangeCallback_ = std::move(callback); }

    // Clear the block change callback
    void clearBlockChangeCallback() { blockChangeCallback_ = nullptr; }

private:
    SubChunkPalette palette_;
    std::array<LocalIndex, VOLUME> blocks_;
    std::vector<uint32_t> usageCounts_;  // Reference count per local index
    int32_t nonAirCount_ = 0;

    // Block version for mesh invalidation (starts at 1, incremented on each change)
    std::atomic<uint64_t> blockVersion_{1};

    // Light data: packed sky (high nibble) + block (low nibble), 4096 bytes
    std::array<uint8_t, VOLUME> light_{};  // Zero-initialized (dark)

    // Light version for mesh invalidation (starts at 1, incremented on each light change)
    std::atomic<uint64_t> lightVersion_{1};

    // Block rotation indices (0-23 for each of 24 cube rotations), 4096 bytes
    // 0 = identity (no rotation), default value
    std::array<uint8_t, VOLUME> rotations_{};  // Zero-initialized (identity)

    // Position (for change callbacks)
    ChunkPos position_{0, 0, 0};

    // Optional callback for block changes
    BlockChangeCallback blockChangeCallback_;

    // Block extra data: sparse map from local index to DataContainer
    // Most blocks have no data, so this is typically empty or very small
    std::unordered_map<int32_t, std::unique_ptr<DataContainer>> blockData_;

    // SubChunk-level extra data (game state, caches, etc.)
    std::unique_ptr<DataContainer> data_;

    // Game tick registry: local indices of blocks that want game ticks
    // O(1) insert/remove/lookup via hash set
    std::unordered_set<uint16_t> gameTickBlocks_;

    // Convert local coordinates to array index
    [[nodiscard]] static constexpr int32_t toIndex(int32_t x, int32_t y, int32_t z) {
        return y * 256 + z * 16 + x;
    }

    // Pack/unpack light values
    [[nodiscard]] static constexpr uint8_t packLight(uint8_t sky, uint8_t block) {
        return ((sky & 0x0F) << 4) | (block & 0x0F);
    }

    [[nodiscard]] static constexpr uint8_t unpackSkyLight(uint8_t packed) {
        return (packed >> 4) & 0x0F;
    }

    [[nodiscard]] static constexpr uint8_t unpackBlockLight(uint8_t packed) {
        return packed & 0x0F;
    }

    // Increment light version
    void bumpLightVersion() {
        lightVersion_.fetch_add(1, std::memory_order_release);
    }

    // Update reference counts when changing a block
    void decrementUsage(LocalIndex oldIndex);
    void incrementUsage(LocalIndex newIndex);

    // Internal setBlock implementation (no dirty tracking or callbacks)
    void setBlockInternal(int32_t index, BlockTypeId type, BlockTypeId oldType);
};

}  // namespace finevox
