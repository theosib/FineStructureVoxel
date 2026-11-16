// The edges of a box as lines: thin strips facing the camera, as thick on screen near as far

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "finevox/render/scene_geometry.hpp"

using namespace finevox;
using render::SceneVertex;

namespace {

constexpr int VERTS_PER_STRIP = 6;  // Two triangles: a-s, b-s, b+s, a-s, b+s, a+s

/// One strip's two ends: the middles of its short sides
struct Strip {
    glm::vec3 a, b;      // centre line
    float widthA, widthB;
    glm::vec3 across;    // unit, from one long side to the other
};

std::vector<Strip> strips(const std::vector<SceneVertex>& v) {
    std::vector<Strip> out;
    for (size_t i = 0; i + VERTS_PER_STRIP <= v.size(); i += VERTS_PER_STRIP) {
        glm::vec3 aLo = v[i].position, bLo = v[i + 1].position, bHi = v[i + 2].position, aHi = v[i + 5].position;
        out.push_back({(aLo + aHi) * 0.5f, (bLo + bHi) * 0.5f, glm::length(aHi - aLo), glm::length(bHi - bLo),
                       glm::normalize(aHi - aLo)});
    }
    return out;
}

}  // namespace

TEST(BoxEdges, TwelveEdgesOfABox) {
    std::vector<SceneVertex> out;
    render::appendBoxEdges(out, glm::vec3(-0.5f, -2.0f, -5.0f), glm::vec3(0.5f, -1.0f, -4.0f), 0.003f,
                           glm::vec4(0, 0, 0, 0.4f));
    ASSERT_EQ(out.size(), 12u * VERTS_PER_STRIP);
    for (const SceneVertex& v : out) {
        EXPECT_EQ(v.normal, glm::vec3(0.0f)) << "unlit";
        EXPECT_FLOAT_EQ(v.color.a, 0.4f);
    }
}

TEST(BoxEdges, AsThickOnScreenNearAsFar) {
    const float width = 0.003f;
    for (float distance : {2.0f, 20.0f, 200.0f}) {
        std::vector<SceneVertex> out;
        render::appendBoxEdges(out, glm::vec3(-0.5f, -0.5f, -distance - 1.0f), glm::vec3(0.5f, 0.5f, -distance),
                               width, glm::vec4(1.0f));
        for (const Strip& s : strips(out)) {
            // Width over distance from the eye (at the origin) is the angle it takes up
            EXPECT_NEAR(s.widthA / glm::length(s.a), width, width * 0.01f) << distance;
            EXPECT_NEAR(s.widthB / glm::length(s.b), width, width * 0.01f) << distance;
        }
    }
}

TEST(BoxEdges, EachStripFacesTheEye) {
    std::vector<SceneVertex> out;
    render::appendBoxEdges(out, glm::vec3(1.0f, -2.0f, -6.0f), glm::vec3(2.0f, -1.0f, -5.0f), 0.003f,
                           glm::vec4(1.0f));
    for (const Strip& s : strips(out)) {
        glm::vec3 along = glm::normalize(s.b - s.a);
        glm::vec3 sight = glm::normalize((s.a + s.b) * 0.5f);
        EXPECT_NEAR(glm::dot(s.across, along), 0.0f, 1e-4f) << "across the line";
        EXPECT_NEAR(glm::dot(s.across, sight), 0.0f, 1e-4f) << "and square to the line of sight";
    }
}

TEST(BoxEdges, DrawnAHairNearerTheEyeThanTheBox) {
    glm::vec3 lo(-0.5f, -2.0f, -5.0f), hi(0.5f, -1.0f, -4.0f);
    std::vector<SceneVertex> out;
    render::appendBoxEdges(out, lo, hi, 0.003f, glm::vec4(1.0f));
    for (const Strip& s : strips(out)) {
        for (glm::vec3 end : {s.a, s.b}) {
            // Each end is a corner of the box, pulled toward the eye by EDGE_LIFT of its distance
            glm::vec3 corner = end / (1.0f - render::EDGE_LIFT);
            for (int axis = 0; axis < 3; ++axis) {
                bool onCorner = std::abs(corner[axis] - lo[axis]) < 1e-4f || std::abs(corner[axis] - hi[axis]) < 1e-4f;
                EXPECT_TRUE(onCorner) << "axis " << axis << " = " << corner[axis];
            }
            EXPECT_LT(glm::length(end), glm::length(corner));
            EXPECT_GT(glm::length(end), glm::length(corner) * 0.99f) << "but not so you'd see";
        }
    }
}

TEST(BoxEdges, AFlatBoxIsItsFourEdgesOnce) {
    std::vector<SceneVertex> out;
    render::appendBoxEdges(out, glm::vec3(-0.5f, -1.6f, -3.0f), glm::vec3(0.5f, -1.6f, -2.0f), 0.003f,
                           glm::vec4(1.0f));
    EXPECT_EQ(out.size(), 4u * VERTS_PER_STRIP);
}
