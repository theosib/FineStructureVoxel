#pragma once

#include "finevox/core/position.hpp"       // BlockPos, Face
#include "finevox/core/string_interner.hpp"  // BlockTypeId
#include "finevox/core/entity_state.hpp"   // EntityId, EntityState

namespace finevox {

/// Abstract command interface for gameplay mutations.
/// All gameplay code routes through this instead of calling World directly.
/// In single-player: delegates to World/UpdateScheduler.
/// In multiplayer: serializes commands to server.
class GameActions {
public:
    virtual ~GameActions() = default;

    /// Break a block. Returns true if the action was accepted.
    virtual bool breakBlock(BlockPos pos) = 0;

    /// Place a block. Returns true if the action was accepted.
    virtual bool placeBlock(BlockPos pos, BlockTypeId type) = 0;

    /// Right-click interaction with a block. Returns true if block had a handler.
    virtual bool useBlock(BlockPos pos, Face face) = 0;

    /// Left-click hit on a block (non-break, e.g. note block). Returns true if handled.
    virtual bool hitBlock(BlockPos pos, Face face) = 0;

    /// Send player state to game thread (position, velocity, look).
    /// Default no-op for subclasses that don't need it.
    virtual void sendPlayerState(EntityId id, const EntityState& state) { (void)id; (void)state; }
};

}  // namespace finevox
