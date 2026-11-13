// A cloud layer from a picture

#include <gtest/gtest.h>

#include <algorithm>

#include "finevox/render/scene_geometry.hpp"

using namespace finevox;

// ---------------------------------------------------------------------------
// Cloud layer
// ---------------------------------------------------------------------------


TEST(CloudLayer, RowsOfCellsMergeAndThePictureRepeats) {
    render::CloudMask mask;
    mask.width = 4;
    mask.height = 1;
    mask.solid = {1, 1, 0, 1};  // a run of two, a gap, a single
    EXPECT_TRUE(mask.at(5, 3));   // wraps: column 1
    EXPECT_FALSE(mask.at(-2, 0)); // wraps: column 2
    std::vector<render::SceneVertex> out;
    // One cell either way of the camera's cell, cells 10 blocks across, no drift
    render::appendCloudLayer(out, mask, glm::dvec3(5, 0, 5), 100.0, 10.0, 0.0, 1, glm::vec4(1));
    // Rows z = -1, 0, 1 each see columns -1..1, which wrap to 3, 0 and 1: all solid, one run, so
    // one quad a row, three cells (30 blocks) across
    ASSERT_EQ(out.size(), 3u * 6u);
    float minX = 1e9f, maxX = -1e9f;
    for (const auto& v : out) {
        EXPECT_FLOAT_EQ(v.position.y, 100.0f);
        minX = std::min(minX, v.position.x);
        maxX = std::max(maxX, v.position.x);
    }
    EXPECT_FLOAT_EQ(maxX - minX, 30.0f);

    // From x = 15 the row sees columns 0..2: solid, solid, clear: a run of two
    out.clear();
    render::appendCloudLayer(out, mask, glm::dvec3(15, 0, 5), 100.0, 10.0, 0.0, 1, glm::vec4(1));
    ASSERT_EQ(out.size(), 3u * 6u);
    minX = 1e9f;
    maxX = -1e9f;
    for (const auto& v : out) {
        minX = std::min(minX, v.position.x);
        maxX = std::max(maxX, v.position.x);
    }
    EXPECT_FLOAT_EQ(maxX - minX, 20.0f);
}

TEST(CloudLayer, DriftMovesTheCells) {
    render::CloudMask mask;
    mask.width = 1;
    mask.height = 1;
    mask.solid = {1};
    std::vector<render::SceneVertex> still, moved;
    render::appendCloudLayer(still, mask, glm::dvec3(0.5, 0, 0.5), 50.0, 1.0, 0.0, 0, glm::vec4(1));
    render::appendCloudLayer(moved, mask, glm::dvec3(0.5, 0, 0.5), 50.0, 1.0, 0.25, 0, glm::vec4(1));
    ASSERT_EQ(still.size(), moved.size());
    EXPECT_FLOAT_EQ(moved[0].position.x - still[0].position.x, 0.25f);
}
