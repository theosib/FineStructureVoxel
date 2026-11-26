#pragma once

/**
 * @file ground_surface.hpp
 * @brief Smoothed ground made solid: bodies stand on the slopes as they're drawn
 *
 * Where a world has one (World::setGroundSurface), the open faces of every `smooth` block, its top
 * and its underside, collide as its smoothed surface (ground_smoothing.hpp) rather than as the cube:
 * a grid of columns, each as high as the surface over its middle. Where the surface goes into the
 * block, its cell is cut away; where it comes out, the open cell beside gets the rest. Picking
 * (RaycastMode::Interaction) still hits the cubes, so placing and breaking keep to the grid.
 *
 * A face's columns are worked out the first time something collides with it and kept until a
 * cell it reads changes. Changes come in from any thread (cellChanged and the rest, which World
 * and ClientWorld call); queries come from one thread, the one that runs physics. See
 * docs/systems/physics.md, "Solid slopes".
 */

#include "finevox/core/ground_smoothing.hpp"
#include "finevox/core/physics.hpp"
#include "finevox/core/position.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace finevox {

class World;

/// Columns each way across a face
constexpr int GROUND_COLUMNS = 4;
/// Column heights are kept in steps of 1/GROUND_QUANTUM of a block
constexpr int GROUND_QUANTUM = 16;
/// How far a body walking on the ground is put down onto it each move (PhysicsSystem::setGroundSnap):
/// more than the steps between columns on the steepest slope, less than a block
constexpr float GROUND_SNAP = 0.5f;
/// How much higher than on cubes a path-following body steps. Where ground meets something built
/// its edge stays level, but its corners can sit up to half a block lower, which makes a step of a
/// block up onto it a step and a half from there.
constexpr float GROUND_STEP_EXTRA = 0.5f;

class GroundSurface {
public:
    /// The surface of a world's ground, with `smooth` saying which block types smooth
    GroundSurface(const World& world, std::function<bool(BlockTypeId)> smooth);

    // Changes: any thread, cheap. They're acted on at the next query.

    /// A block, or whether a cell has fluid, changed
    void cellChanged(BlockCoord pos);
    /// Anything in this box (inclusive) may have changed
    void regionChanged(BlockCoord low, BlockCoord high);
    /// A column came or went
    void columnChanged(ColumnPos pos);

    // Queries: one thread

    /// What a cell collides as, where the surface changes it from its block's own shape. `type` is
    /// the block there. Null where it doesn't: the cell is what its block is. Points at storage of
    /// this thread's own that the next call reuses.
    [[nodiscard]] const CollisionShape* shape(BlockCoord pos, BlockTypeId type);

    /// Whether a point is under the surface, or nullopt where there's no surface in its cell
    [[nodiscard]] std::optional<bool> solidAt(const glm::dvec3& point);

    /// Faces worked out and kept
    [[nodiscard]] size_t facesKept() const;

private:
    /// A face's column heights out from it (up from a top, down from an underside), in
    /// GROUND_QUANTUMs, [x][z]
    struct Face {
        std::array<std::array<int8_t, GROUND_COLUMNS>, GROUND_COLUMNS> height{};
        bool intoBlock = false;  ///< Some column goes into the face's own block
        bool intoOpen = false;   ///< Some column comes out into the open cell it looks into
    };
    using Faces = std::unordered_map<uint32_t, Face>;
    struct Column {
        Faces tops;
        Faces undersides;
    };

    /// What's solid up each column of a cell, in the cell's own heights: one run in a ground
    /// block, cut from above or below, and up to two in an open cell, ground rising into it from
    /// below and hanging into it from above
    struct Solid {
        std::array<std::array<std::array<glm::vec2, 2>, GROUND_COLUMNS>, GROUND_COLUMNS> runs{};
        std::array<std::array<uint8_t, GROUND_COLUMNS>, GROUND_COLUMNS> count{};
    };

    /// The cells to forget the faces of, inclusive, by the block under each face
    struct Range {
        BlockCoord low;
        BlockCoord high;
    };

    [[nodiscard]] GroundCell cellAt(const BlockCoord& pos);
    [[nodiscard]] GroundCell typeClass(BlockTypeId type);
    /// The top (up 1) or underside (up -1) of a Ground block that's open that way
    [[nodiscard]] const Face& face(const BlockCoord& block, int up);
    /// Where the surface changes a cell from its block's own shape, what's solid in it
    [[nodiscard]] std::optional<Solid> solidIn(BlockCoord pos, BlockTypeId type);
    void catchUp();
    void forget(const Range& range);

    const World& world_;
    std::function<bool(BlockTypeId)> smooth_;
    GroundCellProvider cells_;
    /// What each block type is (typeClass), a few at a time: a world has a handful near any body
    struct TypeSlot {
        uint32_t type = UINT32_MAX;
        GroundCell cell = GroundCell::Open;
    };
    std::array<TypeSlot, 64> types_{};
    std::unordered_map<uint64_t, Column> columns_;

    std::mutex pendingMutex_;
    std::vector<Range> pending_;
    std::atomic<bool> hasPending_{false};
};

}  // namespace finevox
