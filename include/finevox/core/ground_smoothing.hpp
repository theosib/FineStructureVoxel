#pragma once

/**
 * @file ground_smoothing.hpp
 * @brief Ground drawn as a smooth surface over its block steps
 *
 * Blocks marked `smooth: true` have their open tops (and undersides) drawn as a curved surface:
 * a quadratic B-spline over the heights of the columns around, so a staircase is a 45 degree
 * slope and a single step an S-curve two blocks long. A step of two or more is a break: each side
 * rounds off toward it by half a block, and a wall stands between. Only drawing changes; collision
 * stays with the blocks. See docs/systems/rendering.md, "Smooth ground".
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"
#include <glm/glm.hpp>
#include <functional>
#include <optional>

namespace finevox {

class World;

/// The most pieces a curved face is drawn in along each way across it
constexpr int GROUND_CURVED_STEPS = 3;

/// How far from a ground block's faces, in cells, the cells that shape them can be: across, and
/// up or down. A change there changes how they're drawn.
constexpr int GROUND_REACH_ACROSS = 3;
constexpr int GROUND_REACH_UP_DOWN = 6;

/// What a cell is to the smoothing
enum class GroundCell : uint8_t {
    Open,    ///< Air, or a plant: nothing that fills the cell or stops a body
    Wet,     ///< Open, with fluid in it: shaped as Open, but where water is drawn
    Ground,  ///< A block that smooths
    Solid,   ///< Anything else
};

using GroundCellProvider = std::function<GroundCell(const BlockCoord&)>;

/// What a block type is to the smoothing, with Open standing for Open or Wet as its cell has fluid
[[nodiscard]] GroundCell groundCellOf(BlockTypeId type, const std::function<bool(BlockTypeId)>& smooth);

/// The cells of a world, with `smooth` saying which block types smooth
[[nodiscard]] GroundCellProvider groundCells(const World& world, std::function<bool(BlockTypeId)> smooth);

/// What lies past one edge of a smoothed face
enum class GroundEdge : uint8_t {
    Link,  ///< Another smoothed face up to a block further out or in: the surface runs on
    Up,    ///< A wall of ground two or more high: the face curves up half a block to meet it
    Down,  ///< A drop of two or more: the face rounds off half a block toward it
    Pin,   ///< Something built: the face meets it level, with a wall where it doesn't
};

/// The edges of a face, in this order: -x, +x, -z, +z
constexpr int GROUND_EDGE_X_LOW = 0;
constexpr int GROUND_EDGE_X_HIGH = 1;
constexpr int GROUND_EDGE_Z_LOW = 2;
constexpr int GROUND_EDGE_Z_HIGH = 3;

/// The smoothed top or underside of one ground block. Heights are outward from the face, in
/// blocks: up from a top, down from an underside.
struct GroundPatch {
    BlockCoord block;
    int up = 1;                     ///< +1 for a top, open above; -1 for an underside, open below
    float control[3][3] = {};       ///< Bezier control heights, [x][z], 0 on the low side
    GroundEdge edge[4] = {};
    int8_t link[4] = {};            ///< For a Link edge: how many cells further out the next face is
    int steps[2] = {1, 1};          ///< How many pieces it's drawn in along x and z
    /// The surface stays between these. A block a cell thick, with faces both ways, keeps each
    /// on its own half, and so does a gap a cell high with ground both sides, so they never cross.
    float lowest = -1e9f;
    float highest = 1e9f;

    /// The y of the cube face it replaces
    [[nodiscard]] int faceLevel() const { return up > 0 ? block.y + 1 : block.y; }
    [[nodiscard]] bool flat() const;
    /// Height at (u, v) across the face, u along x and v along z, both 0 to 1
    [[nodiscard]] float height(float u, float v) const;
    /// d(height)/du and d(height)/dv
    [[nodiscard]] glm::vec2 slope(float u, float v) const;
    /// Height along an edge, s along it (z for the x edges, x for the z edges)
    [[nodiscard]] float edgeHeight(int edge, float s) const;
    /// How many pieces an edge is drawn in
    [[nodiscard]] int edgeSteps(int edge) const { return steps[edge < 2 ? 1 : 0]; }
    /// The world y of a point on the surface
    [[nodiscard]] float worldY(float u, float v) const { return float(faceLevel()) + float(up) * height(u, v); }
    /// Outward surface normal at (u, v)
    [[nodiscard]] glm::vec3 normal(float u, float v) const;
};

/// The smoothed face of `block` on its `up` side (+1 top, -1 underside), or nullopt where it
/// has none: it isn't ground, or the cell that face looks into isn't open
[[nodiscard]] std::optional<GroundPatch> groundPatch(const GroundCellProvider& cells, BlockCoord block, int up);

/// Where the smoothed ground under a body standing at `feet` is drawn, or nullopt where there's
/// no smoothed ground under it. Looks down two cells, so a body that has just stepped up onto a
/// block's edge still finds the slope it's over.
[[nodiscard]] std::optional<double> groundSurfaceY(const GroundCellProvider& cells, const glm::dvec3& feet);

}  // namespace finevox
