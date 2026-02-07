#pragma once

/**
 * @file world.hpp
 * @brief Main world interface for block access and force-loading
 *
 * Design: [05-world-management.md] §5.2 World
 * Force-loading: [23-distance-and-loading.md] §23.3
 */

#include "finevox/core/position.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <functional>
#include <optional>
#include <vector>

namespace finevox {

// Forward declarations
class ColumnManager;
class LightEngine;
class UpdateScheduler;
struct LightingUpdate;
struct BlockChange;  // Defined in batch_builder.hpp

// World contains all chunk columns and provides block access
// Thread-safe for concurrent read access; writes require exclusive access
//
// Design notes:
// - Columns are loaded/unloaded as units (full height 16x16 columns)
// - SubChunks within columns are created lazily when blocks are set
// - World provides the main interface for block manipulation
//
class World {
public:
    World();
    ~World();

    // ========================================================================
    // Block Access
    // ========================================================================

    // Returns AIR_BLOCK_TYPE if position not loaded
    [[nodiscard]] BlockTypeId getBlock(BlockPos pos) const;
    [[nodiscard]] BlockTypeId getBlock(int32_t x, int32_t y, int32_t z) const;

    // ========================================================================
    // Internal Block API (Direct, No Events)
    // ========================================================================
    // Use for: terrain generation, chunk loading, bulk initialization
    // Does NOT trigger: handlers, lighting updates, neighbor notifications

    /// Set block directly without triggering events
    /// Creates column and subchunk if needed
    void setBlock(BlockPos pos, BlockTypeId type);
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type);

    // ========================================================================
    // External Block API (Event-Driven)
    // ========================================================================
    // Use for: player actions, game logic block changes
    // Triggers: handlers, lighting updates, neighbor notifications
    // Requires: UpdateScheduler to be set via setUpdateScheduler()

    /// Place a block through the event system
    /// @return true if event was queued, false if no scheduler set
    bool placeBlock(BlockPos pos, BlockTypeId type);

    /// Break a block through the event system
    /// @return true if event was queued, false if no scheduler set
    bool breakBlock(BlockPos pos);

    /// Place multiple blocks through the event system (bulk)
    /// More efficient than individual placeBlock calls
    /// @return number of events queued (0 if no scheduler set)
    size_t placeBlocks(const std::vector<BlockChange>& changes);

    /// Break multiple blocks through the event system (bulk)
    /// @return number of events queued (0 if no scheduler set)
    size_t breakBlocks(const std::vector<BlockPos>& positions);

    // Column access
    [[nodiscard]] ChunkColumn* getColumn(ColumnPos pos);
    [[nodiscard]] const ChunkColumn* getColumn(ColumnPos pos) const;

    // Get or create column (for generation/loading)
    [[nodiscard]] ChunkColumn& getOrCreateColumn(ColumnPos pos);

    // Check if column exists
    [[nodiscard]] bool hasColumn(ColumnPos pos) const;

    // Remove a column (for unloading)
    bool removeColumn(ColumnPos pos);

    // Column iteration
    void forEachColumn(const std::function<void(ColumnPos, ChunkColumn&)>& callback);
    void forEachColumn(const std::function<void(ColumnPos, const ChunkColumn&)>& callback) const;

    // Statistics
    [[nodiscard]] size_t columnCount() const;
    [[nodiscard]] int64_t totalNonAirBlocks() const;

    // Column generator callback (called when new columns are created)
    using ColumnGenerator = std::function<void(ChunkColumn&)>;
    void setColumnGenerator(ColumnGenerator generator);

    // Subchunk access (derived from columns)
    [[nodiscard]] SubChunk* getSubChunk(ChunkPos pos);
    [[nodiscard]] const SubChunk* getSubChunk(ChunkPos pos) const;

    // Get shared pointer to subchunk (for mesh cache weak references)
    // Returns empty shared_ptr if column or subchunk doesn't exist
    [[nodiscard]] std::shared_ptr<SubChunk> getSubChunkShared(ChunkPos pos);

    // Get all subchunk positions that have data
    [[nodiscard]] std::vector<ChunkPos> getAllSubChunkPositions() const;

    // Clear entire world
    void clear();

    // ========================================================================
    // Mesh Utilities
    // ========================================================================

    // Get subchunks that would be affected by a block change at the given position.
    // Includes the containing subchunk and any adjacent subchunks
    // affected if the block is at a boundary (useful for mesh rebuild scheduling).
    [[nodiscard]] std::vector<ChunkPos> getAffectedSubChunks(BlockPos blockPos) const;

    // ========================================================================
    // Force-Loading
    // ========================================================================
    // Blocks can register to force-load chunks around them.
    // This prevents chunks from being unloaded even when no players are nearby.
    // Used for chunk loaders, spawn chunks, etc.

    /// Register a force-loader at the given position
    /// @param pos Block position of the force-loader
    /// @param radius Chunk radius to keep loaded (0 = just this chunk, 1 = 3x3, etc.)
    void registerForceLoader(BlockPos pos, int32_t radius = 0);

    /// Unregister a force-loader
    /// No-op if position wasn't registered
    void unregisterForceLoader(BlockPos pos);

    /// Check if a chunk can be unloaded
    /// Returns false if any force-loader is keeping this chunk loaded
    [[nodiscard]] bool canUnloadChunk(ChunkPos pos) const;

    /// Check if a column can be unloaded
    /// Returns false if any subchunk in the column is kept loaded by a force-loader
    /// Use this for ColumnManager's canUnloadCallback
    [[nodiscard]] bool canUnloadColumn(ColumnPos pos) const;

    /// Check if a position is a registered force-loader
    [[nodiscard]] bool isForceLoader(BlockPos pos) const;

    /// Get all registered force-loaders (for serialization)
    [[nodiscard]] const std::unordered_map<BlockPos, int32_t>& forceLoaders() const;

    /// Set force-loaders from deserialization
    /// Replaces any existing force-loaders
    void setForceLoaders(std::unordered_map<BlockPos, int32_t> loaders);

    // ========================================================================
    // Lighting Integration
    // ========================================================================
    // Optional lighting system integration.
    // Design: [24-event-system.md] §24.10

    /// Set the light engine for this world
    /// World does not take ownership; caller must ensure lifetime
    void setLightEngine(LightEngine* engine);

    /// Get the light engine (may be null if not set)
    [[nodiscard]] LightEngine* lightEngine() { return lightEngine_; }
    [[nodiscard]] const LightEngine* lightEngine() const { return lightEngine_; }

    /// Enqueue a lighting update to async thread (for bulk/batch operations)
    /// No-op if light engine is not set
    void enqueueLightingUpdate(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);

    /// Process a lighting update synchronously (for immediate visual feedback)
    /// Use this for player-driven block changes that need instant response
    /// No-op if light engine is not set
    void processLightingUpdateSync(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);

    /// Set the mesh rebuild queue for deferred mesh generation
    /// World does not take ownership; caller must ensure lifetime
    void setMeshRebuildQueue(MeshRebuildQueue* queue);

    /// Get the mesh rebuild queue (may be null if not set)
    [[nodiscard]] MeshRebuildQueue* meshRebuildQueue() { return meshRebuildQueue_; }

    /// Enqueue a lighting update with automatic remesh deferral
    ///
    /// This implements the smart deferral logic:
    /// - If lighting queue is empty: defer remesh to lighting thread (triggerMeshRebuild=true)
    /// - If lighting queue not empty: push remesh immediately, lighting handles additional
    ///
    /// Use this for player-driven block changes in the event system.
    /// No-op if light engine is not set.
    void enqueueLightingUpdateWithRemesh(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);

    /// Set whether to always defer mesh rebuilds to the lighting thread
    /// When true, bypasses the "queue empty" check and always defers.
    /// Useful for testing to avoid visual blinks from premature mesh rebuilds.
    void setAlwaysDeferMeshRebuild(bool defer) { alwaysDeferMeshRebuild_ = defer; }
    [[nodiscard]] bool alwaysDeferMeshRebuild() const { return alwaysDeferMeshRebuild_; }

    // ========================================================================
    // Event System Integration
    // ========================================================================
    // Optional update scheduler for the external block API.
    // Design: [24-event-system.md]

    /// Set the update scheduler for this world
    /// World does not take ownership; caller must ensure lifetime
    void setUpdateScheduler(UpdateScheduler* scheduler);

    /// Get the update scheduler (may be null if not set)
    [[nodiscard]] UpdateScheduler* updateScheduler() { return updateScheduler_; }
    [[nodiscard]] const UpdateScheduler* updateScheduler() const { return updateScheduler_; }

private:
    mutable std::shared_mutex columnMutex_;
    std::unordered_map<uint64_t, std::unique_ptr<ChunkColumn>> columns_;
    ColumnGenerator columnGenerator_;

    // Force-loader registry: block position -> chunk radius
    mutable std::shared_mutex forceLoaderMutex_;
    std::unordered_map<BlockPos, int32_t> forceLoaders_;

    // Helper to convert block position to column position
    [[nodiscard]] static ColumnPos blockToColumn(BlockPos pos);

    // Optional light engine (not owned)
    LightEngine* lightEngine_ = nullptr;

    // Optional mesh rebuild queue for push-based meshing (not owned)
    MeshRebuildQueue* meshRebuildQueue_ = nullptr;

    // Optional update scheduler for external API (not owned)
    UpdateScheduler* updateScheduler_ = nullptr;

    // Config: always defer mesh rebuilds to lighting thread (for testing)
    bool alwaysDeferMeshRebuild_ = false;
};

}  // namespace finevox
