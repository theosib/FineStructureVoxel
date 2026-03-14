#pragma once

#include "finevox/core/position.hpp"       // BlockCoord, Face
#include "finevox/core/string_interner.hpp"  // BlockTypeId
#include "finevox/core/entity_state.hpp"   // EntityId, EntityState
#include "finevox/core/fluid_type_id.hpp"  // FluidTypeId
#include "finevox/core/recipe.hpp"         // RecipeId
#include <finescript/value.h>

namespace finevox {

/// Abstract command interface for gameplay mutations.
/// All gameplay code routes through this instead of calling World directly.
/// In single-player: delegates to World/UpdateScheduler.
/// In multiplayer: serializes commands to server.
///
/// sendAction() is the primary interface — all commands are finescript::Value maps.
/// Typed convenience methods have default implementations that build Values and
/// call sendAction(). Subclasses may override them for validation or eager effects.
class GameActions {
public:
    virtual ~GameActions() = default;

    /// Send a raw action Value to the game thread. Subclasses implement routing.
    virtual void sendAction(finescript::Value action) = 0;

    /// Break a block. Returns true if the action was accepted.
    virtual bool breakBlock(BlockCoord pos);

    /// Place a block. Returns true if the action was accepted.
    virtual bool placeBlock(BlockCoord pos, BlockTypeId type);

    /// Right-click interaction with a block. Returns true if block had a handler.
    virtual bool useBlock(BlockCoord pos, Face face);

    /// Left-click hit on a block (non-break, e.g. note block). Returns true if handled.
    virtual bool hitBlock(BlockCoord pos, Face face);

    /// Place fluid at a position. Returns true if the action was accepted.
    virtual bool placeFluid(BlockCoord pos, FluidTypeId type, uint8_t level = 15);

    /// Remove fluid at a position. Returns true if the action was accepted.
    virtual bool removeFluid(BlockCoord pos);

    /// Send player state to game thread (position, velocity, look).
    virtual void sendPlayerState(EntityId id, const EntityState& state);

    /// Set world time to an absolute tick value.
    virtual void setWorldTime(int64_t ticks);

    /// Attempt to craft a recipe at a station position.
    virtual bool craftItem(BlockCoord stationPos, RecipeId recipe);
};

}  // namespace finevox
