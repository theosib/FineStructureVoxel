#pragma once

#include "finevox/position.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/subchunk.hpp"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <functional>
#include <optional>
#include <vector>

namespace finevox {

// Forward declarations
class SubChunkManager;

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

    // Block access - the primary interface
    // Returns AIR_BLOCK_TYPE if position not loaded
    [[nodiscard]] BlockTypeId getBlock(BlockPos pos) const;
    [[nodiscard]] BlockTypeId getBlock(int32_t x, int32_t y, int32_t z) const;

    // Set block at position
    // Creates column and subchunk if needed
    void setBlock(BlockPos pos, BlockTypeId type);
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type);

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

    /// Check if a position is a registered force-loader
    [[nodiscard]] bool isForceLoader(BlockPos pos) const;

    /// Get all registered force-loaders (for serialization)
    [[nodiscard]] const std::unordered_map<BlockPos, int32_t>& forceLoaders() const;

    /// Set force-loaders from deserialization
    /// Replaces any existing force-loaders
    void setForceLoaders(std::unordered_map<BlockPos, int32_t> loaders);

private:
    mutable std::shared_mutex columnMutex_;
    std::unordered_map<uint64_t, std::unique_ptr<ChunkColumn>> columns_;
    ColumnGenerator columnGenerator_;

    // Force-loader registry: block position -> chunk radius
    mutable std::shared_mutex forceLoaderMutex_;
    std::unordered_map<BlockPos, int32_t> forceLoaders_;

    // Helper to convert block position to column position
    [[nodiscard]] static ColumnPos blockToColumn(BlockPos pos);
};

}  // namespace finevox
