#include <gtest/gtest.h>
#include "finevox/core/tactical_camera.hpp"

#include <cmath>
#include <vector>

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

TEST(EdgeScroll, PushesTowardTheEdgeTheCursorIsAt) {
    const float MARGIN = 4.0f;
    glm::vec2 centre = VIEWPORT * 0.5f;
    EXPECT_EQ(edgeScroll(centre, VIEWPORT, MARGIN, false), glm::vec2(0.0f, 0.0f));

    EXPECT_EQ(edgeScroll({0.0f, centre.y}, VIEWPORT, MARGIN, false), glm::vec2(-1.0f, 0.0f));
    EXPECT_EQ(edgeScroll({VIEWPORT.x, centre.y}, VIEWPORT, MARGIN, false), glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(edgeScroll({centre.x, 0.0f}, VIEWPORT, MARGIN, false), glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(edgeScroll({centre.x, VIEWPORT.y}, VIEWPORT, MARGIN, false), glm::vec2(0.0f, -1.0f));
    EXPECT_EQ(edgeScroll({0.0f, 0.0f}, VIEWPORT, MARGIN, false), glm::vec2(-1.0f, 1.0f));
}

TEST(EdgeScroll, CursorPastAnOrdinaryWindowIsDoingSomethingElse) {
    glm::vec2 centre = VIEWPORT * 0.5f;
    for (glm::vec2 outside : {glm::vec2(centre.x, -30.0f), glm::vec2(-30.0f, centre.y),
                              glm::vec2(centre.x, VIEWPORT.y + 30.0f),
                              glm::vec2(VIEWPORT.x + 30.0f, centre.y)}) {
        EXPECT_EQ(edgeScroll(outside, VIEWPORT, 4.0f, false), glm::vec2(0.0f));
    }
}

// The band above full-screen content, beside a camera notch, is still the top of the screen
TEST(EdgeScroll, CursorPastAWindowSpanningTheScreenStillScrolls) {
    glm::vec2 centre = VIEWPORT * 0.5f;
    EXPECT_EQ(edgeScroll({centre.x, -30.0f}, VIEWPORT, 4.0f, true), glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(edgeScroll({-30.0f, centre.y}, VIEWPORT, 4.0f, true), glm::vec2(-1.0f, 0.0f));
    EXPECT_EQ(edgeScroll({centre.x, VIEWPORT.y + 30.0f}, VIEWPORT, 4.0f, true), glm::vec2(0.0f, -1.0f));
    EXPECT_EQ(edgeScroll({VIEWPORT.x + 30.0f, centre.y}, VIEWPORT, 4.0f, true), glm::vec2(1.0f, 0.0f));
}

namespace {

ViewBasis rigView(const TacticalCamera& cam) {
    return makeViewBasis(cam.position(), cam.forward(), 60.0f, VIEWPORT.x / VIEWPORT.y);
}

}  // namespace

TEST(TacticalCameraGrab, GroundStaysUnderTheCursor) {
    for (float yaw : {0.0f, 0.6f, 2.5f, -1.7f}) {
        TacticalCamera cam;
        cam.setPivot(glm::dvec3(40.0, 64.0, -25.0));
        cam.orbit(yaw, 0.0f);
        cam.update(10.0f);

        for (glm::dvec3 held : {glm::dvec3(40.0, 64.0, -25.0), glm::dvec3(46.0, 64.0, -21.0),
                                glm::dvec3(31.0, 64.0, -30.0)}) {
            auto from = worldToScreen(rigView(cam), held, VIEWPORT);
            ASSERT_TRUE(from.has_value());
            glm::vec2 to = *from + glm::vec2(90.0f, -55.0f);

            cam.grab(rigView(cam), *from, to, VIEWPORT);
            cam.update(10.0f);

            auto landed = worldToScreen(rigView(cam), held, VIEWPORT);
            ASSERT_TRUE(landed.has_value());
            EXPECT_NEAR(landed->x, to.x, 0.5f) << "yaw " << yaw;
            EXPECT_NEAR(landed->y, to.y, 0.5f) << "yaw " << yaw;
        }
    }
}

TEST(TacticalCameraGrab, KeepsToBounds) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(0.0, 64.0, 0.0));
    cam.setBounds(glm::vec2(-5.0f), glm::vec2(5.0f));
    cam.update(10.0f);
    for (int i = 0; i < 50; ++i) {
        cam.grab(rigView(cam), VIEWPORT * 0.5f, VIEWPORT * 0.5f + glm::vec2(200.0f, 0.0f), VIEWPORT);
        cam.update(10.0f);
    }
    EXPECT_LE(std::abs(cam.targetPivot().x), 5.0);
    EXPECT_LE(std::abs(cam.targetPivot().z), 5.0);
}

TEST(TacticalCameraGrab, IgnoresACursorAboveTheHorizon) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(0.0, 64.0, 0.0));
    cam.orbit(0.0f, TacticalCamera::MIN_PITCH - cam.pitch());
    cam.update(10.0f);

    glm::dvec3 before = cam.targetPivot();
    cam.grab(rigView(cam), VIEWPORT * 0.5f, glm::vec2(VIEWPORT.x * 0.5f, 0.0f), VIEWPORT);
    EXPECT_EQ(cam.targetPivot(), before);
}

TEST(GroundLevel, IgnoresTheOddTallOrLowColumn) {
    std::vector<int32_t> field(49, 64);
    EXPECT_EQ(groundLevel(field), 64.0);

    // A tree, a tower, and a pit in the sampled area
    field[3] = 72;
    field[20] = 90;
    field[41] = 50;
    EXPECT_EQ(groundLevel(field), 64.0);
}

TEST(GroundLevel, FollowsWhatMostOfTheAreaIs) {
    std::vector<int32_t> field(49, 64);
    for (int i = 0; i < 30; ++i) field[i] = 70;
    EXPECT_EQ(groundLevel(field), 70.0);
}

TEST(GroundLevel, NothingSampledIsNoAnswer) {
    EXPECT_FALSE(groundLevel({}).has_value());
}

TEST(TacticalCameraGround, SmallChangesDoNotMoveTheCamera) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(0.0, 64.0, 0.0));
    for (double y : {65.0, 63.0, 66.0, 62.0}) {
        cam.followGround(y);
        cam.update(10.0f);
        EXPECT_EQ(cam.pivot().y, 64.0) << "ground at " << y;
    }
}

TEST(TacticalCameraGround, RealChangesAreGlidedTo) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(0.0, 64.0, 0.0));
    cam.followGround(80.0);

    cam.update(1.0f / 60.0f);
    double oneFrame = cam.pivot().y - 64.0;
    EXPECT_GT(oneFrame, 0.0);
    EXPECT_LT(oneFrame, 1.0) << "a sudden sixteen block rise must not lurch";

    for (int i = 0; i < 240; ++i) cam.update(1.0f / 60.0f);
    EXPECT_NEAR(cam.pivot().y, 80.0, 0.01);
}

TEST(TacticalCameraGround, FollowingHeightLeavesPanningAtFullSpeed) {
    TacticalCamera cam;
    cam.setPivot(glm::dvec3(0.0, 64.0, 0.0));
    cam.followGround(80.0);
    cam.pan(20.0f, 0.0f);
    for (int i = 0; i < 30; ++i) cam.update(1.0f / 60.0f);

    double panned = glm::length(glm::dvec2(cam.pivot().x, cam.pivot().z));
    double climbed = cam.pivot().y - 64.0;
    EXPECT_GT(panned / 20.0, 0.95);
    EXPECT_LT(climbed / 16.0, 0.85);
}

// A long pan over a gentle ridge with a tree every few columns, as the camera used to see it
// and as it does now. The ridge is 12 blocks high, so that's the travel an ideal camera does.
TEST(TacticalCameraGround, PanningOverTreesDoesNotBob) {
    auto terrain = [](int x) { return 64 + static_cast<int>(6.0 + 6.0 * std::sin(x * 0.02)); };
    auto topBlock = [&](int x) { return terrain(x) + (x % 7 == 0 ? 7 : 0); };

    auto travel = [&](auto&& followAt) {
        TacticalCamera cam;
        cam.setPivot(glm::dvec3(0.0, terrain(0), 0.0));
        double total = 0.0;
        double last = cam.pivot().y;
        for (int x = 0; x < 300; ++x) {
            followAt(cam, x);
            cam.update(1.0f / 30.0f);
            total += std::abs(cam.pivot().y - last);
            last = cam.pivot().y;
        }
        return total;
    };

    double sampled = travel([&](TacticalCamera& cam, int x) {
        std::vector<int32_t> heights;
        for (int dz = 0; dz < 7; ++dz) {
            for (int dx = -3; dx <= 3; ++dx) {
                // Trees scattered in z as well as x, so each row sees different ones
                int cx = x + dx * 6;
                heights.push_back(terrain(cx) + ((cx + dz * 3) % 7 == 0 ? 7 : 0));
            }
        }
        cam.followGround(*groundLevel(std::move(heights)));
    });
    double singleColumn = travel([&](TacticalCamera& cam, int x) { cam.followGround(topBlock(x)); });

    EXPECT_LT(sampled, 40.0) << "the land itself accounts for at most ~24 of this";
    EXPECT_LT(sampled, singleColumn * 0.5);
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

TEST(BodyInScreenRect, AClickAnywhereOnABodyHitsItAtAnyZoom) {
    // Looking down at the feet from further and further away, as the player zooms out
    for (float distance : {6.0f, 12.0f, 24.0f, 60.0f}) {
        glm::dvec3 feet(0.0, 0.0, 0.0);
        glm::vec3 forward = glm::normalize(glm::vec3(0.0f, -1.0f, 1.0f));
        ViewBasis view = makeViewBasis(feet - glm::dvec3(forward) * double(distance), forward, 55.0f,
                                       VIEWPORT.x / VIEWPORT.y);

        // The very edge of the shoulder, worked out from the world and not the helper
        auto edge = worldToScreen(view, feet + glm::dvec3(0.0, 0.9, 0.0) + glm::dvec3(view.right) * 0.4,
                                  VIEWPORT);
        ASSERT_TRUE(edge.has_value());
        EXPECT_TRUE(bodyInScreenRect(view, feet, 1.9f, 0.45f, VIEWPORT, *edge, *edge)) << distance;

        // A body's width beyond that is a miss
        auto away = worldToScreen(view, feet + glm::dvec3(0.0, 0.9, 0.0) + glm::dvec3(view.right) * 1.2,
                                  VIEWPORT);
        ASSERT_TRUE(away.has_value());
        EXPECT_FALSE(bodyInScreenRect(view, feet, 1.9f, 0.45f, VIEWPORT, *away, *away)) << distance;
    }
}

TEST(BodyInScreenRect, LookingStraightDownABodyStillHasAnArea) {
    glm::dvec3 feet(0.0, 0.0, 0.0);
    ViewBasis view = makeViewBasis(feet + glm::dvec3(0.0, 12.0, 0.0), glm::vec3(0.0f, -1.0f, 0.0f), 55.0f,
                                   VIEWPORT.x / VIEWPORT.y);
    auto beside = worldToScreen(view, feet + glm::dvec3(view.right) * 0.4, VIEWPORT);
    ASSERT_TRUE(beside.has_value());
    EXPECT_TRUE(bodyInScreenRect(view, feet, 1.9f, 0.45f, VIEWPORT, *beside, *beside));
}

TEST(BodyInScreenRect, BehindTheCameraIsNeverHit) {
    ViewBasis view = makeViewBasis(glm::dvec3(0.0), glm::vec3(0.0f, 0.0f, 1.0f), 55.0f,
                                   VIEWPORT.x / VIEWPORT.y);
    EXPECT_FALSE(bodyInScreenRect(view, glm::dvec3(0.0, 0.0, -10.0), 1.9f, 0.45f, VIEWPORT,
                                  glm::vec2(0.0f), VIEWPORT));
}
