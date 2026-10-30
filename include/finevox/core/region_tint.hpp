#pragma once

/**
 * @file region_tint.hpp
 * @brief A map-wide grid of brightness, separate from the blocks it falls on
 *
 * The renderer blends each pixel toward a colour by a value read from this grid at that
 * point on the map, smoothly between cells. Fog of war is the obvious use, but a grid of
 * numbers doesn't care what they mean: a faction's territory or a blighted region would do
 * as well. A value belongs to a place on the map, so blocks don't have to know about it.
 */

#include "finevox/core/faction_vision.hpp"

#include <cstdint>
#include <vector>

namespace finevox {

class RegionTint {
public:
    /// How far a cell has come toward clear: 0 is the tint colour, 1 is untouched
    static constexpr float HIDDEN = 0.0f;
    static constexpr float CLEAR = 1.0f;

    RegionTint(int32_t columns, int32_t rows);

    [[nodiscard]] int32_t columns() const { return columns_; }
    [[nodiscard]] int32_t rows() const { return rows_; }

    /// Where cells should head, and over time will arrive. Fog of war from a faction's sight:
    /// `explored` is the level for what was seen and has gone out of view. The outermost cells
    /// stay hidden, so the map's edge fades away instead of ending in a wall.
    void setFromVision(const std::vector<uint8_t>& cells, float explored);

    /// Go straight there, as for a view that's only just appearing
    void snapToTargets();

    /// Move toward the targets, covering all of a change in about a third of a second
    void advance(float dt);

    /// Row by row
    [[nodiscard]] const std::vector<float>& cells() const { return current_; }

private:
    int32_t columns_;
    int32_t rows_;
    std::vector<float> target_;
    std::vector<float> current_;
};

}  // namespace finevox
