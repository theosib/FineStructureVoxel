#pragma once

#include "finevox/string_interner.hpp"
#include "finevox/position.hpp"
#include "finevox/rotation.hpp"
#include <string_view>
#include <cstdint>

namespace finevox {

// Forward declarations
class World;
class SubChunk;
class DataContainer;
class BlockContext;

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
                 BlockPos pos, BlockPos localPos);

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
    [[nodiscard]] BlockPos localPos() const { return localPos_; }

    /**
     * @brief Get the block type at this position
     */
    [[nodiscard]] BlockTypeId blockType() const;

    // ========================================================================
    // Block State (Rotation)
    // ========================================================================

    /**
     * @brief Get the block's rotation
     *
     * Note: Rotation storage is not yet implemented in SubChunk.
     * Returns IDENTITY for now.
     */
    [[nodiscard]] Rotation rotation() const;

    /**
     * @brief Set the block's rotation
     *
     * Note: Rotation storage is not yet implemented in SubChunk.
     * This is a no-op for now.
     */
    void setRotation(Rotation rot);

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

private:
    World& world_;
    SubChunk& subChunk_;
    BlockPos pos_;
    BlockPos localPos_;
};

}  // namespace finevox
