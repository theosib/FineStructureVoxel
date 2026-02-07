#pragma once

/**
 * @file batch_builder.hpp
 * @brief Block operation batching and coalescing
 *
 * Design: [13-batch-builder.md] §13.1 BatchBuilder
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"
#include <unordered_map>
#include <vector>
#include <optional>

namespace finevox {

// Forward declaration
class World;

// BatchBuilder collects block changes and applies them atomically
// Features:
// - Coalescing: multiple changes to same position keep only latest
// - Atomic commit: all changes applied together
// - Bounds tracking: know affected area before commit
//
// Usage:
//   BatchBuilder batch;
//   batch.setBlock(pos1, stone);
//   batch.setBlock(pos2, dirt);
//   batch.setBlock(pos1, air);  // Overwrites previous change to pos1
//   batch.commit(world);        // Applies air at pos1, dirt at pos2
//
// TODO: Future optimization - hierarchical batch commit:
//   Currently commit() loops over all changes calling world.setBlock() individually.
//   For large batches, this could be optimized by:
//   1. World splits batch by column
//   2. Each column splits by subchunk
//   3. SubChunk applies its sub-batch in single pass (avoid repeated lookups)
//   This would reduce overhead for bulk operations like structure placement.
//
class BatchBuilder {
public:
    BatchBuilder() = default;

    // Queue a block change
    void setBlock(BlockPos pos, BlockTypeId type);
    void setBlock(int32_t x, int32_t y, int32_t z, BlockTypeId type);

    // Remove a pending change (set back to no-op for that position)
    void cancel(BlockPos pos);

    // Check if there are any pending changes
    [[nodiscard]] bool empty() const { return changes_.empty(); }

    // Number of pending changes
    [[nodiscard]] size_t size() const { return changes_.size(); }

    // Clear all pending changes without applying
    void clear();

    // Get the change for a specific position (nullopt if no change)
    [[nodiscard]] std::optional<BlockTypeId> getChange(BlockPos pos) const;

    // Check if a position has a pending change
    [[nodiscard]] bool hasChange(BlockPos pos) const;

    // Get bounding box of all changes (nullopt if empty)
    struct Bounds {
        BlockPos min;
        BlockPos max;
    };
    [[nodiscard]] std::optional<Bounds> getBounds() const;

    // Get all affected column positions
    [[nodiscard]] std::vector<ColumnPos> getAffectedColumns() const;

    // Apply all changes to a world
    // Returns number of blocks actually changed (excludes no-ops like air->air)
    size_t commit(World& world);

    // Apply changes and get list of positions that actually changed
    std::vector<BlockPos> commitAndGetChanged(World& world);

    // Iterate over pending changes
    using ChangeCallback = void(BlockPos pos, BlockTypeId type);
    void forEach(const std::function<ChangeCallback>& callback) const;

    // Merge another batch into this one (other's changes override)
    void merge(const BatchBuilder& other);

private:
    std::unordered_map<uint64_t, BlockTypeId> changes_;  // packed pos -> type
};

// BlockChange represents a single block change for events/undo
struct BlockChange {
    BlockPos pos;
    BlockTypeId oldType;
    BlockTypeId newType;
};

// BatchResult contains information about a committed batch
struct BatchResult {
    std::vector<BlockChange> changes;
    size_t blocksChanged = 0;
    std::optional<BatchBuilder::Bounds> bounds;
};

// Extended commit that returns full change information
BatchResult commitBatchWithHistory(BatchBuilder& batch, World& world);

}  // namespace finevox
