#pragma once

/**
 * @file block_events.hpp
 * @brief Typed event structs for blocks, fluids, players, and world events
 *
 * These replace the monolithic BlockEvent struct. Each event type carries
 * only the data it needs, reducing memory and improving type safety.
 *
 * All structs are trivially movable and fit within GameEventHolder's
 * 96-byte SBO (no heap allocation).
 */

#include "finevox/core/game_event.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/rotation.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/block_handler.hpp"  // TickType

#include <cstdint>

namespace finevox {

// ============================================================================
// Block lifecycle events
// ============================================================================

/// Block was placed or replaced in the world
struct BlockPlacedEvent {
    BlockCoord pos;
    BlockTypeId newType;
    BlockTypeId oldType;
    Rotation rotation{};
};

/// Block was broken/removed
struct BlockBrokenEvent {
    BlockCoord pos;
    BlockTypeId oldType;
};

/// Block state changed (rotation, data)
struct BlockChangedEvent {
    BlockCoord pos;
    BlockTypeId blockType;
};

// ============================================================================
// Block tick events
// ============================================================================

/// Tick event for a block (scheduled, repeat, random, or game tick)
struct BlockTickEvent {
    BlockCoord pos;
    TickType tickType;
};

// ============================================================================
// Block neighbor events
// ============================================================================

/// Neighbor of a block changed (supports face-mask consolidation)
struct NeighborUpdatedEvent {
    BlockCoord pos;
    uint8_t faceMask = 0;  // Bitmask: (1 << static_cast<uint8_t>(Face))
};

/// Signal-like propagation (block re-evaluates state)
struct BlockUpdateEvent {
    BlockCoord pos;
};

// ============================================================================
// Block interaction events
// ============================================================================

/// Right-click interaction with a block
struct BlockInteractEvent {
    BlockCoord pos;
    Face face;
};

/// Left-click hit on a block (non-break, e.g. note block)
struct BlockStrikeEvent {
    BlockCoord pos;
    Face face;
};

/// Request to repaint a block's visual state
struct RepaintEvent {
    BlockCoord pos;
};

// ============================================================================
// Fluid events
// ============================================================================

/// Fluid placed at a position
struct FluidPlacedEvent {
    BlockCoord pos;
    FluidTypeId type;
    uint8_t level = 15;
};

/// Fluid removed from a position
struct FluidRemovedEvent {
    BlockCoord pos;
    FluidTypeId previousType;
};

// ============================================================================
// Player/entity events
// ============================================================================

/// Player position/velocity/look update from graphics thread
struct PlayerPositionEvent {
    EntityId entityId;
    EntityState state;
};

/// Player look direction changed
struct PlayerLookEvent {
    EntityId entityId;
    EntityState state;
};

/// Player jumped
struct PlayerJumpEvent {
    EntityId entityId;
};

/// Player sprint state changed
struct PlayerSprintEvent {
    EntityId entityId;
    bool starting;
};

/// Player sneak state changed
struct PlayerSneakEvent {
    EntityId entityId;
    bool starting;
};

// ============================================================================
// World events
// ============================================================================

/// Chunk column loaded
struct ChunkLoadedEvent {
    ColumnPos pos;
};

/// Chunk column unloaded
struct ChunkUnloadedEvent {
    ColumnPos pos;
};

/// Set world time to absolute tick value
struct SetWorldTimeEvent {
    int64_t ticks;
};

// ============================================================================
// Event consolidation specializations
// ============================================================================

/// NeighborUpdatedEvent: merge face masks when same position
template<>
struct EventConsolidation<NeighborUpdatedEvent> {
    static constexpr bool supported = true;

    static BlockCoord key(const NeighborUpdatedEvent& e) { return e.pos; }

    static NeighborUpdatedEvent merge(
        const NeighborUpdatedEvent& existing,
        const NeighborUpdatedEvent& incoming
    ) {
        return { incoming.pos,
                 static_cast<uint8_t>(existing.faceMask | incoming.faceMask) };
    }
};

} // namespace finevox
