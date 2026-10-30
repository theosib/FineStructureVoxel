#include "finevox/core/region_tint.hpp"

#include <algorithm>

namespace finevox {

namespace {

constexpr float CELLS_PER_SECOND = 3.0f;    ///< Full range of a cell, hidden to clear

}  // namespace

RegionTint::RegionTint(int32_t columns, int32_t rows)
    : columns_(columns), rows_(rows), target_(size_t(columns) * rows, HIDDEN),
      current_(size_t(columns) * rows, HIDDEN) {}

void RegionTint::setFromVision(const std::vector<uint8_t>& cells, float explored) {
    if (cells.size() != target_.size()) return;

    for (int32_t z = 0; z < rows_; ++z) {
        for (int32_t x = 0; x < columns_; ++x) {
            size_t index = size_t(z) * columns_ + x;
            bool edge = x == 0 || z == 0 || x == columns_ - 1 || z == rows_ - 1;
            auto state = static_cast<VisionState>(cells[index]);
            target_[index] = edge || state == VisionState::Unknown ? HIDDEN
                             : state == VisionState::Explored      ? explored
                                                                   : CLEAR;
        }
    }
}

void RegionTint::snapToTargets() {
    current_ = target_;
}

void RegionTint::advance(float dt) {
    float step = CELLS_PER_SECOND * dt;
    for (size_t i = 0; i < current_.size(); ++i) {
        current_[i] += std::clamp(target_[i] - current_[i], -step, step);
    }
}

}  // namespace finevox
