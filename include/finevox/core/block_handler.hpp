#pragma once

/**
 * @file block_handler.hpp
 * @brief BlockContext and BlockHandler for event-driven block behavior
 *
 * Design: [24-event-system.md] §24.7 Handlers
 */

#include "finevox/core/string_interner.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/rotation.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/block_type.hpp"
#include <memory>
#include <string_view>
#include <cstdint>

namespace finevox {

// Forward declarations
class World;
class SubChunk;
class BlockContext;
class UpdateScheduler;

// ============================================================================
// TickType - Types of block tick events
// ============================================================================

/**
 * @brief Types of tick events that can be scheduled for blocks
 */
enum class TickType : uint8_t {
    Scheduled = 1,  // One-time scheduled tick (from scheduleTick)
    Repeat = 2,     // Repeating tick (at set interval)
    Random = 4,     // Random tick (for grass growth, etc.)
};

// Allow combining tick types with bitwise OR
inline TickType operator|(TickType a, TickType b) {
    return static_cast<TickType>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(TickType a, TickType b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ============================================================================
// BlockHandler - Stateless block behavior interface
// ============================================================================

/**
 * @brief Interface for block behavior handlers
 *
 * BlockHandlers are stateless - they define behavior but hold no instance data.
 * All state is stored in the SubChunk (rotation, extra data, etc.) and passed
 * to handlers via BlockContext.
 *
 * Handlers are registered with the BlockRegistry and looked up by BlockTypeId.
 * Not all block types need handlers - simple blocks (stone, dirt) may only need
 * BlockType properties (collision shape, opacity, etc.).
 *
 * Thread safety: Handler methods may be called from multiple threads concurrently
 * for different blocks. Implementations must not use mutable instance state.
 */
class BlockHandler {
public:
    virtual ~BlockHandler() = default;

    /**
     * @brief Get the fully-qualified block name this handler is for
     *
     * Must match the name used to register the handler.
     *
     * @return Block name (e.g., "blockgame:redstone_torch")
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    // ========================================================================
    // Lifecycle Events
    // ========================================================================

    /**
     * @brief Called when this block is placed in the world
     *
     * Use for initialization: setting initial rotation, creating extra data,
     * scheduling ticks, notifying neighbors, etc.
     *
     * @param ctx Context providing access to block state and world
     */
    virtual void onPlace(BlockContext& ctx) { (void)ctx; }

    /**
     * @brief Called when this block is broken/removed from the world
     *
     * Use for cleanup: dropping items, notifying neighbors, etc.
     * Note: The block is still present when this is called; it will be
     * removed immediately after.
     *
     * @param ctx Context providing access to block state and world
     */
    virtual void onBreak(BlockContext& ctx) { (void)ctx; }

    // ========================================================================
    // Tick Events
    // ========================================================================

    /**
     * @brief Called when a scheduled or repeating tick fires
     *
     * @param ctx Context providing access to block state and world
     * @param type Which type(s) of tick triggered this call
     */
    virtual void onTick(BlockContext& ctx, TickType type) {
        (void)ctx;
        (void)type;
    }

    // ========================================================================
    // Neighbor Events
    // ========================================================================

    /**
     * @brief Called when a neighboring block changes
     *
     * Use for blocks that react to neighbors: torches falling off walls,
     * redstone updating, sand falling, etc.
     *
     * @param ctx Context providing access to block state and world
     * @param changedFace Which face's neighbor changed (relative to this block)
     */
    virtual void onNeighborChanged(BlockContext& ctx, Face changedFace) {
        (void)ctx;
        (void)changedFace;
    }

    /**
     * @brief Called when a block update event is received
     *
     * Use for redstone-like propagation where a block needs to re-evaluate
     * its state. Unlike onNeighborChanged, this doesn't specify which
     * neighbor triggered the update.
     *
     * Handlers can push BlockUpdate events to the outbox to propagate
     * updates to other blocks.
     *
     * @param ctx Context providing access to block state and world
     */
    virtual void onBlockUpdate(BlockContext& ctx) { (void)ctx; }

    // ========================================================================
    // Interaction Events
    // ========================================================================

    /**
     * @brief Called when a player right-clicks (uses) this block
     *
     * @param ctx Context providing access to block state and world
     * @param face Which face was clicked
     * @return true if the interaction was handled (prevents further processing)
     */
    virtual bool onUse(BlockContext& ctx, Face face) {
        (void)ctx;
        (void)face;
        return false;
    }

    /**
     * @brief Called when a player left-clicks (hits) this block
     *
     * Note: This is for special hit behavior, not mining. Mining is handled
     * separately by the block's hardness property.
     *
     * @param ctx Context providing access to block state and world
     * @param face Which face was clicked
     * @return true if the interaction was handled
     */
    virtual bool onHit(BlockContext& ctx, Face face) {
        (void)ctx;
        (void)face;
        return false;
    }

    // ========================================================================
    // Visual Events
    // ========================================================================

    /**
     * @brief Called when the block's mesh needs updating
     *
     * Use for blocks with dynamic appearance that changes based on state
     * or neighbors (connected textures, directional blocks, etc.).
     *
     * Default implementation does nothing - most blocks use static meshes.
     *
     * @param ctx Context providing access to block state and world
     */
    virtual void onRepaint(BlockContext& ctx) { (void)ctx; }
};

// ============================================================================
// BlockContext - Passed to handler callbacks
// ============================================================================

/**
 * @brief Context providing access to block state for handler callbacks
 *
 * This is an ephemeral object created when invoking a handler method.
 * It provides read/write access to the block's state (rotation, extra data)
 * and the surrounding world.
 *
 * Similar to the old Block struct from voxelgame2, but designed for
 * stateless handlers.
 */
class BlockContext {
public:
    /**
     * @brief Construct context for a block
     * @param world World containing the block
     * @param subChunk SubChunk containing the block
     * @param pos Block position in world coordinates
     * @param localPos Position within subchunk (0-15 on each axis)
     */
    BlockContext(World& world, SubChunk& subChunk,
                 BlockPos pos, LocalBlockPos localPos);

    // ========================================================================
    // Location
    // ========================================================================

    /**
     * @brief Get the world containing this block
     */
    [[nodiscard]] World& world() { return world_; }
    [[nodiscard]] const World& world() const { return world_; }

    /**
     * @brief Get the subchunk containing this block
     */
    [[nodiscard]] SubChunk& subChunk() { return subChunk_; }
    [[nodiscard]] const SubChunk& subChunk() const { return subChunk_; }

    /**
     * @brief Get block position in world coordinates
     */
    [[nodiscard]] BlockPos pos() const { return pos_; }

    /**
     * @brief Get block position within subchunk (0-15 on each axis)
     */
    [[nodiscard]] LocalBlockPos localPos() const { return localPos_; }

    /**
     * @brief Get the block type ID at this position
     */
    [[nodiscard]] BlockTypeId blockType() const;

    /**
     * @brief Get the BlockType definition from the registry
     *
     * Returns raw pointer - the registry outlives all blocks.
     */
    [[nodiscard]] const BlockType* type() const;

    /**
     * @brief Get the subchunk position (ChunkPos)
     */
    [[nodiscard]] ChunkPos chunkPos() const;

    /**
     * @brief Get the local index within the subchunk (0-4095)
     */
    [[nodiscard]] int32_t localIndex() const;

    // ========================================================================
    // Type Convenience Methods
    // ========================================================================

    /**
     * @brief Check if this block is air
     */
    [[nodiscard]] bool isAir() const;

    /**
     * @brief Check if this block is opaque (blocks light)
     */
    [[nodiscard]] bool isOpaque() const;

    /**
     * @brief Check if this block is transparent
     */
    [[nodiscard]] bool isTransparent() const;

    // ========================================================================
    // Block State (Rotation)
    // ========================================================================

    /**
     * @brief Get the block's rotation
     *
     * Each block stores a rotation index (0-23) for one of 24 cube rotations.
     * Default is identity (0).
     */
    [[nodiscard]] Rotation rotation() const;

    /**
     * @brief Set the block's rotation
     *
     * Stores the rotation index in the SubChunk and triggers mesh rebuild.
     */
    void setRotation(Rotation rot);

    /**
     * @brief Get the block's rotation index (0-23)
     */
    [[nodiscard]] uint8_t rotationIndex() const;

    /**
     * @brief Set the block's rotation by index (0-23)
     */
    void setRotationIndex(uint8_t index);

    // ========================================================================
    // Light Access
    // ========================================================================

    /**
     * @brief Get sky light level at this block (0-15)
     */
    [[nodiscard]] uint8_t skyLight() const;

    /**
     * @brief Get block light level at this block (0-15)
     */
    [[nodiscard]] uint8_t blockLight() const;

    /**
     * @brief Get combined light level (max of sky and block light)
     */
    [[nodiscard]] uint8_t combinedLight() const;

    // ========================================================================
    // Extra Data (Phase 9)
    // ========================================================================

    /**
     * @brief Get extra data for this block
     *
     * Note: Extra data storage is not yet implemented (Phase 9).
     * Returns nullptr for now.
     *
     * @return Pointer to DataContainer, or nullptr if no data
     */
    [[nodiscard]] DataContainer* data();

    /**
     * @brief Get or create extra data for this block
     *
     * Note: Extra data storage is not yet implemented (Phase 9).
     * This will assert/throw for now.
     *
     * @return Reference to DataContainer (created if needed)
     */
    DataContainer& getOrCreateData();

    // ========================================================================
    // Tick Scheduling (Phase 9)
    // ========================================================================

    /**
     * @brief Schedule a one-time tick for this block
     *
     * Note: Tick scheduling is not yet implemented (Phase 9).
     * This is a no-op for now.
     *
     * @param ticksFromNow Number of game ticks until the tick fires
     */
    void scheduleTick(int ticksFromNow);

    /**
     * @brief Set repeating tick interval for this block
     *
     * Note: Tick scheduling is not yet implemented (Phase 9).
     * This is a no-op for now.
     *
     * @param interval Ticks between each repeat (0 to disable)
     */
    void setRepeatTickInterval(int interval);

    // ========================================================================
    // Visual Updates
    // ========================================================================

    /**
     * @brief Request mesh rebuild for the subchunk containing this block
     *
     * Call after changing block appearance (rotation, connected textures, etc.).
     */
    void requestMeshRebuild();

    /**
     * @brief Mark the subchunk as dirty (needs saving)
     */
    void markDirty();

    // ========================================================================
    // Neighbor Access
    // ========================================================================

    /**
     * @brief Get the block type of a neighbor
     * @param face Direction to the neighbor
     * @return BlockTypeId of the neighbor (AIR_BLOCK_TYPE if outside world)
     */
    [[nodiscard]] BlockTypeId getNeighbor(Face face) const;

    /**
     * @brief Notify neighbors that this block changed
     *
     * Triggers onNeighborChanged for all 6 adjacent blocks.
     */
    void notifyNeighbors();

    // ========================================================================
    // Previous State (for place/break events)
    // ========================================================================

    /**
     * @brief Get the previous block type (before place/break)
     *
     * Only valid during onPlace/onBreak handlers.
     */
    [[nodiscard]] BlockTypeId previousType() const { return previousType_; }

    /**
     * @brief Get the previous block's extra data
     *
     * Only valid during onPlace handler when replacing a block that had data.
     * The data is moved to the context, so this can only be accessed once.
     *
     * @return Pointer to DataContainer, or nullptr if no previous data
     */
    [[nodiscard]] const DataContainer* previousData() const { return previousData_.get(); }

    /**
     * @brief Take ownership of previous data (for restoring on undo)
     * @return Unique pointer to the previous data, may be null
     */
    [[nodiscard]] std::unique_ptr<DataContainer> takePreviousData();

    /**
     * @brief Set the previous block type (called by EventProcessor)
     */
    void setPreviousType(BlockTypeId type) { previousType_ = type; }

    /**
     * @brief Set the previous block's extra data (called by EventProcessor)
     */
    void setPreviousData(std::unique_ptr<DataContainer> data);

    /**
     * @brief Set the scheduler for tick scheduling (called by UpdateScheduler)
     *
     * When set, scheduleTick() and setRepeatTickInterval() will work.
     * When nullptr, those methods are no-ops.
     */
    void setScheduler(UpdateScheduler* scheduler) { scheduler_ = scheduler; }

    // ========================================================================
    // Block Modification (for handlers to alter/undo placement)
    // ========================================================================

    /**
     * @brief Change the block at this position
     *
     * Used by handlers to modify or undo a placement.
     * Example: torch placement fails validation, set back to previousType.
     *
     * @param type New block type to set
     */
    void setBlock(BlockTypeId type);

private:
    World& world_;
    SubChunk& subChunk_;
    BlockPos pos_;
    LocalBlockPos localPos_;

    // Previous state (set by EventProcessor for place/break events)
    BlockTypeId previousType_;
    std::unique_ptr<DataContainer> previousData_;

    // Scheduler for tick scheduling (optional, set by UpdateScheduler)
    UpdateScheduler* scheduler_ = nullptr;
};

}  // namespace finevox
