#include <gtest/gtest.h>
#include "finevox/core/tactical_camera.hpp"

#include <cmath>

using namespace finevox;

namespace {

const glm::vec2 VIEWPORT(1280.0f, 720.0f);

ViewBasis obliqueView() {
    return makeViewBasis(glm::dvec3(10.0, 50.0, -20.0), glm::vec3(0.3f, -0.8f, 0.5f), 60.0f,
                         VIEWPORT.x / VIEWPORT.y);
}

}  // namespace

TEST(ViewBasis, IsOrthonormalEvenLookingStraightDown) {
    for (glm::vec3 forward : {glm::vec3(0, -1, 0), glm::vec3(0.3f, -0.8f, 0.5f), glm::vec3(1, 0, 0)}) {
        ViewBasis v = makeViewBasis(glm::dvec3(0.0), forward, 60.0f, 1.0f);
        EXPECT_NEAR(glm::length(v.forward), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(v.right), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::length(v.up), 1.0f, 1e-5f);
        EXPECT_NEAR(glm::dot(v.forward, v.right), 0.0f, 1e-5f);
        EXPECT_NEAR(glm::dot(v.forward, v.up), 0.0f, 1e-5f);
    }
}

TEST(ScreenRay, CentreOfScreenIsTheViewDirection) {
    ViewBasis v = obliqueView();
    WorldRay ray = screenRayToWorld(v, VIEWPORT * 0.5f, VIEWPORT);
    EXPECT_NEAR(glm::dot(ray.direction, v.forward), 1.0f, 1e-5f);
}

TEST(ScreenRay, EdgesMatchTheFieldOfView) {
    ViewBasis v = obliqueView();
    WorldRay top = screenRayToWorld(v, glm::vec2(VIEWPORT.x * 0.5f, 0.0f), VIEWPORT);
    float angle = std::acos(glm::dot(top.direction, v.forward));
    EXPECT_NEAR(glm::degrees(angle), 30.0f, 0.01f);
    EXPECT_GT(glm::dot(top.direction, v.up), 0.0f) << "screen top must be camera up";

    WorldRay left = screenRayToWorld(v, glm::vec2(0.0f, VIEWPORT.y * 0.5f), VIEWPORT);
    EXPECT_LT(glm::dot(left.direction, v.right), 0.0f) << "screen left must be camera left";
}

TEST(WorldToScreen, RoundTripsWithScreenRayAcrossTheViewport) {
    ViewBasis v = obliqueView();
    for (float x : {0.0f, 200.0f, 640.0f, 1100.0f, 1280.0f}) {
        for (float y : {0.0f, 100.0f, 360.0f, 720.0f}) {
            WorldRay ray = screenRayToWorld(v, glm::vec2(x, y), VIEWPORT);
            glm::dvec3 point = ray.origin + glm::dvec3(ray.direction) * 40.0;
            auto screen = worldToScreen(v, point, VIEWPORT);
            ASSERT_TRUE(screen.has_value());
            EXPECT_NEAR(screen->x, x, 0.05f);
            EXPECT_NEAR(screen->y, y, 0.05f);
        }
    }
}

TEST(WorldToScreen, PointsBehindTheCameraHaveNoProjection) {
    ViewBasis v = obliqueView();
    EXPECT_FALSE(worldToScreen(v, v.position - glm::dvec3(v.forward) * 5.0, VIEWPORT).has_value());
}

TEST(WorldToScreen, HoldsPrecisionFarFromTheOrigin) {
    ViewBasis v = makeViewBasis(glm::dvec3(5000000.0, 80.0, 5000000.0), glm::vec3(0.2f, -0.9f, 0.3f),
                                60.0f, VIEWPORT.x / VIEWPORT.y);
    auto screen = worldToScreen(v, v.position + glm::dvec3(v.forward) * 30.0, VIEWPORT);
    ASSERT_TRUE(screen.has_value());
    EXPECT_NEAR(screen->x, 640.0f, 0.1f);
    EXPECT_NEAR(screen->y, 360.0f, 0.1f);
}

TEST(TacticalCameraRig, LooksAtItsPivot) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(100.0, 70.0, -40.0));
    cam.orbit(0.7f, 0.2f);
    glm::dvec3 pos = cam.position();
    EXPECT_NEAR(glm::length(pos - cam.pivot()), cam.distance(), 1e-3);
    glm::vec3 toPivot = glm::normalize(glm::vec3(cam.pivot() - pos));
    EXPECT_NEAR(glm::dot(toPivot, cam.forward()), 1.0f, 1e-5f);
}

TEST(TacticalCameraRig, PanMovesInScreenDirectionsWhateverTheYaw) {
    for (float yaw : {0.0f, 0.9f, 2.4f, -1.7f}) {
        TacticalCamera cam;
        cam.setPivot(glm::dvec3(0.0));
        cam.orbit(yaw - cam.yaw(), 0.0f);
        ViewBasis view = makeViewBasis(cam.position(), cam.forward(), 60.0f, 1.6f);

        cam.pan(10.0f, 0.0f);
        glm::vec2 moved(cam.targetPivot().x, cam.targetPivot().z);
        glm::vec2 screenRight = glm::normalize(glm::vec2(view.right.x, view.right.z));
        EXPECT_NEAR(glm::dot(moved, screenRight), 10.0f, 1e-3f);

        cam.setPivot(glm::dvec3(0.0));
        cam.pan(0.0f, 10.0f);
        moved = glm::vec2(cam.targetPivot().x, cam.targetPivot().z);
        glm::vec2 screenUp = glm::normalize(glm::vec2(view.forward.x, view.forward.z));
        EXPECT_NEAR(glm::dot(moved, screenUp), 10.0f, 1e-3f);
    }
}

TEST(TacticalCameraRig, RespectsLimits) {
    TacticalCamera cam;
    cam.orbit(0.0f, 100.0f);
    EXPECT_FLOAT_EQ(cam.pitch(), TacticalCamera::MAX_PITCH);
    cam.orbit(0.0f, -100.0f);
    EXPECT_FLOAT_EQ(cam.pitch(), TacticalCamera::MIN_PITCH);

    cam.zoom(0.0001f);
    for (int i = 0; i < 200; ++i) cam.update(1.0f / 60.0f);
    EXPECT_NEAR(cam.distance(), TacticalCamera::MIN_DISTANCE, 0.01f);

    cam.setBounds(glm::vec2(-50.0f), glm::vec2(50.0f));
    cam.pan(1000.0f, 1000.0f);
    EXPECT_LE(std::abs(cam.targetPivot().x), 50.0);
    EXPECT_LE(std::abs(cam.targetPivot().z), 50.0);
}

TEST(TacticalCameraRig, SmoothingIsFrameRateIndependent) {
    auto settleAfterOneSecond = [](int fps) {
        TacticalCamera cam;
        cam.setPivot(glm::dvec3(0.0));
        cam.pan(100.0f, 0.0f);
        for (int i = 0; i < fps; ++i) cam.update(1.0f / fps);
        return glm::length(cam.pivot());
    };
    EXPECT_NEAR(settleAfterOneSecond(30), settleAfterOneSecond(240), 0.01);
}
