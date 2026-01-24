#pragma once

#include "finevox/position.hpp"
#include "finevox/light_data.hpp"  // Keep for utility functions (packLightValue, etc.)
#include "finevox/string_interner.hpp"
#include <functional>
#include <queue>
#include <vector>
#include <unordered_set>

namespace finevox {

// Forward declarations
class World;
class SubChunk;
class ChunkColumn;

/**
 * @brief Custom light attenuation callback for special materials
 *
 * Used for materials like water that have non-linear light falloff.
 *
 * @param blockType The block type light is passing through
 * @param incomingLight Light level entering the block (0-15)
 * @param depthInMaterial Number of blocks traveled through this material type
 * @return Resulting light level after attenuation
 */
using LightAttenuationCallback = std::function<uint8_t(
    BlockTypeId blockType,
    uint8_t incomingLight,
    int depthInMaterial
)>;

/**
 * @brief Light propagation engine for block and sky lighting
 *
 * Implements BFS-based light propagation with support for:
 * - Block light (emitted by torches, lava, etc.)
 * - Sky light (propagates down from exposed sky)
 * - Light attenuation through transparent blocks
 * - Custom attenuation callbacks for special materials (water)
 *
 * Light values range from 0 (complete darkness) to 15 (maximum brightness).
 *
 * Thread safety: Not thread-safe. Use one LightEngine per thread or synchronize externally.
 */
class LightEngine {
public:
    /**
     * @brief Construct light engine for a world
     * @param world World to operate on
     */
    explicit LightEngine(World& world);

    // ========================================================================
    // Light Access
    // ========================================================================

    /// Get sky light at world position (0-15)
    [[nodiscard]] uint8_t getSkyLight(const BlockPos& pos) const;

    /// Get block light at world position (0-15)
    [[nodiscard]] uint8_t getBlockLight(const BlockPos& pos) const;

    /// Get combined light (max of sky and block) at world position
    [[nodiscard]] uint8_t getCombinedLight(const BlockPos& pos) const;

    /// Get subchunk containing light data at position (may be null if chunk not loaded)
    [[nodiscard]] SubChunk* getSubChunkForLight(const ChunkPos& chunkPos);
    [[nodiscard]] const SubChunk* getSubChunkForLight(const ChunkPos& chunkPos) const;

    // ========================================================================
    // Block Light Updates
    // ========================================================================

    /// Update lighting after a block is placed
    /// @param pos Position where block was placed
    /// @param oldType Previous block type (for light removal)
    /// @param newType New block type (for light emission)
    void onBlockPlaced(const BlockPos& pos, BlockTypeId oldType, BlockTypeId newType);

    /// Update lighting after a block is removed (set to air)
    /// @param pos Position where block was removed
    /// @param oldType Previous block type
    void onBlockRemoved(const BlockPos& pos, BlockTypeId oldType);

    /// Propagate block light from a light source
    /// @param pos Position of the light source
    /// @param lightLevel Light level to emit (0-15)
    void propagateBlockLight(const BlockPos& pos, uint8_t lightLevel);

    /// Remove block light from a position and update surrounding area
    /// @param pos Position where light source was removed
    /// @param oldLevel Previous light level at this position
    void removeBlockLight(const BlockPos& pos, uint8_t oldLevel);

    // ========================================================================
    // Sky Light Updates
    // ========================================================================

    /// Initialize sky light for a chunk column
    /// Call after loading a chunk or generating terrain
    /// @param columnPos Column position
    void initializeSkyLight(const ColumnPos& columnPos);

    /// Update sky light after heightmap change
    /// @param pos Position where block was placed/removed affecting sky
    /// @param oldHeight Previous height at this X,Z
    /// @param newHeight New height at this X,Z
    void updateSkyLight(const BlockPos& pos, int32_t oldHeight, int32_t newHeight);

    /// Propagate sky light from a position
    /// @param pos Starting position
    /// @param lightLevel Light level to propagate
    void propagateSkyLight(const BlockPos& pos, uint8_t lightLevel);

    // ========================================================================
    // Batch Operations
    // ========================================================================

    /// Recalculate all lighting for a subchunk
    /// @param chunkPos Subchunk position
    void recalculateSubChunk(const ChunkPos& chunkPos);

    /// Recalculate all lighting for a column
    /// @param columnPos Column position
    void recalculateColumn(const ColumnPos& columnPos);

    /// Mark a region as needing light recalculation
    /// Light updates will be processed on next processUpdates() call
    void markDirty(const BlockPos& pos);

    /// Process pending light updates
    /// Call periodically to batch light updates for efficiency
    void processUpdates();

    // ========================================================================
    // Custom Attenuation
    // ========================================================================

    /// Set custom attenuation callback for a block type
    /// Use for materials like water with non-linear light falloff
    void setAttenuationCallback(BlockTypeId blockType, LightAttenuationCallback callback);

    /// Remove custom attenuation callback
    void clearAttenuationCallback(BlockTypeId blockType);

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Get maximum propagation distance per update (for limiting work)
    [[nodiscard]] int32_t maxPropagationDistance() const { return maxPropagationDistance_; }

    /// Set maximum propagation distance per update
    void setMaxPropagationDistance(int32_t distance) { maxPropagationDistance_ = distance; }

private:
    World& world_;

    // Custom attenuation callbacks
    std::unordered_map<BlockTypeId, LightAttenuationCallback> attenuationCallbacks_;

    // Pending updates for batch processing
    std::unordered_set<BlockPos> pendingUpdates_;

    // Configuration
    int32_t maxPropagationDistance_ = 256;  // Max blocks to propagate per update

    // BFS queue entry for light propagation
    struct LightNode {
        BlockPos pos;
        uint8_t light;

        bool operator<(const LightNode& other) const {
            // Lower light values have lower priority (process higher light first)
            return light < other.light;
        }
    };

    // Get light attenuation for a block type
    [[nodiscard]] uint8_t getAttenuation(BlockTypeId blockType) const;

    // Check if a block type blocks sky light
    [[nodiscard]] bool blocksSkyLight(BlockTypeId blockType) const;

    // Get light emission for a block type
    [[nodiscard]] uint8_t getLightEmission(BlockTypeId blockType) const;

    // Get or create subchunk for light storage (creates empty subchunk if needed)
    SubChunk* getOrCreateSubChunkForLight(const ChunkPos& chunkPos);

    // Convert world position to chunk position and local position
    [[nodiscard]] static ChunkPos toChunkPos(const BlockPos& pos);
    [[nodiscard]] static int32_t toLocalIndex(const BlockPos& pos);

    // BFS light propagation implementation
    void propagateLightBFS(const BlockPos& start, uint8_t startLevel, bool isSkyLight);

    // Light removal with re-propagation
    void removeLightBFS(const BlockPos& start, uint8_t startLevel, bool isSkyLight);
};

}  // namespace finevox
