#pragma once

/**
 * @file block_mover.hpp
 * @brief Moving a group of blocks one cell, and working out which blocks go
 *
 * Something that moves a block (a ram, a lift, a sliding wall) moves more than that block. The
 * group is found from two things each block type says (`.model` keys, per state with `set:`):
 *
 * - `move:` (MoveRule): `moves` (default), `fixed`, `breaks` or `no-drag`.
 * - `move-grip: <group>`: the block grips its neighbours. When it moves, every neighbour not in
 *   its way is dragged along, except a gripping block of a different group.
 *
 * A block is *pushed* when a moving block goes into its cell; it is *dragged* when a gripping
 * neighbour takes it along, or when the mover pulls it. A pushed block that is `fixed` stops the
 * whole move; a pushed `breaks` block becomes air. A dragged block goes only if its rule is
 * `moves`; otherwise it stays where it is. Whatever goes pushes what's in front of it, and grips
 * what it grips, so the group grows until it is closed.
 *
 * Docs: docs/systems/world.md, "Block movers".
 */

#include "finevox/core/position.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace finevox {

class World;
enum class MoveRule : uint8_t;

/// `moves`, `fixed`, `breaks`, `no-drag`
[[nodiscard]] std::optional<MoveRule> moveRuleNamed(std::string_view name);

struct MoveRequest {
    BlockCoord start;           ///< The block the mover acts on first
    Face dir = Face::PosX;      ///< Which way everything moves
    bool pull = false;          ///< The mover drags `start` (true) rather than pushing it
    int32_t limit = 64;         ///< Most blocks that may move; more and nothing moves
    /// The mover's own cell, if it is in the world: it never moves or is gripped, and a push
    /// into it stops the move
    std::optional<BlockCoord> anchor;
};

struct MovePlan {
    std::vector<BlockCoord> moves;   ///< Cells whose blocks move one along `dir`
    std::vector<BlockCoord> breaks;  ///< Cells pushed into whose blocks break
};

/// The blocks a move takes, or nullopt if it can't happen (a fixed block or the anchor in the
/// way, or more than `limit` blocks). An empty plan is a move with nothing to do.
[[nodiscard]] std::optional<MovePlan> planMove(const World& world, const MoveRequest& request);

/// Carry out a plan: the `breaks` cells become air, then every `moves` block goes one cell along
/// `dir` together, with its rotation and block data. Raw block writes (no handlers), like
/// World::setBlock. Every cell that changed goes into `changed`, once.
void applyMove(World& world, const MovePlan& plan, Face dir, std::vector<BlockCoord>* changed = nullptr);

}  // namespace finevox
