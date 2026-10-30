#include <gtest/gtest.h>
#include "finevox/core/region_tint.hpp"

using namespace finevox;

namespace {

constexpr int32_t SIZE = 5;
const uint8_t UNKNOWN = static_cast<uint8_t>(VisionState::Unknown);
const uint8_t EXPLORED = static_cast<uint8_t>(VisionState::Explored);
const uint8_t VISIBLE = static_cast<uint8_t>(VisionState::Visible);

std::vector<uint8_t> field(uint8_t state) {
    return std::vector<uint8_t>(SIZE * SIZE, state);
}

float at(const RegionTint& tint, int x, int z) {
    return tint.cells()[size_t(z) * SIZE + x];
}

}  // namespace

TEST(RegionTint, VisionStatesSetLevels) {
    RegionTint tint(SIZE, SIZE);
    auto cells = field(UNKNOWN);
    cells[2 * SIZE + 1] = EXPLORED;
    cells[2 * SIZE + 2] = VISIBLE;
    tint.setFromVision(cells, 0.5f);
    tint.snapToTargets();

    EXPECT_EQ(at(tint, 1, 2), 0.5f);
    EXPECT_EQ(at(tint, 2, 2), RegionTint::CLEAR);
    EXPECT_EQ(at(tint, 3, 2), RegionTint::HIDDEN);
}

TEST(RegionTint, TheMapsEdgeStaysHiddenEvenInPlainSight) {
    RegionTint tint(SIZE, SIZE);
    tint.setFromVision(field(VISIBLE), 0.5f);
    tint.snapToTargets();

    for (int i = 0; i < SIZE; ++i) {
        EXPECT_EQ(at(tint, i, 0), RegionTint::HIDDEN);
        EXPECT_EQ(at(tint, i, SIZE - 1), RegionTint::HIDDEN);
        EXPECT_EQ(at(tint, 0, i), RegionTint::HIDDEN);
        EXPECT_EQ(at(tint, SIZE - 1, i), RegionTint::HIDDEN);
    }
    EXPECT_EQ(at(tint, 2, 2), RegionTint::CLEAR);
}

TEST(RegionTint, ChangesFadeRatherThanPop) {
    RegionTint tint(SIZE, SIZE);
    tint.setFromVision(field(VISIBLE), 0.5f);

    tint.advance(1.0f / 60.0f);
    EXPECT_GT(at(tint, 2, 2), RegionTint::HIDDEN);
    EXPECT_LT(at(tint, 2, 2), 0.2f);

    for (int i = 0; i < 60; ++i) tint.advance(1.0f / 60.0f);
    EXPECT_EQ(at(tint, 2, 2), RegionTint::CLEAR);
}

TEST(RegionTint, FadesBothWaysAndStopsAtTheTarget) {
    RegionTint tint(SIZE, SIZE);
    tint.setFromVision(field(VISIBLE), 0.5f);
    tint.snapToTargets();
    tint.setFromVision(field(EXPLORED), 0.5f);
    for (int i = 0; i < 120; ++i) tint.advance(1.0f / 60.0f);
    EXPECT_EQ(at(tint, 2, 2), 0.5f);
}

TEST(RegionTint, AWrongSizedSnapshotIsIgnored) {
    RegionTint tint(SIZE, SIZE);
    tint.setFromVision(std::vector<uint8_t>(3, VISIBLE), 0.5f);
    tint.snapToTargets();
    EXPECT_EQ(at(tint, 2, 2), RegionTint::HIDDEN);
}
