#include <gtest/gtest.h>

#include "finevox/core/physics.hpp"

#include <finevk/engine/camera.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>

using namespace finevox;

namespace {

// The client's FOV and near and far planes, and the widest screen worth planning for
constexpr float CLIENT_FOV = 55.0f;
constexpr float CLIENT_NEAR = 0.2f;
constexpr float CLIENT_FAR = 900.0f;
constexpr float WIDEST_ASPECT = 21.0f / 9.0f;

// Where a point this far ahead of the camera lands in Vulkan's depth range, in which
// only 0 to 1 is drawn. Nothing here knows how the projection was built.
float depthAt(const finevk::Camera& camera, float distance) {
    glm::vec4 clip = camera.state().viewProjection * glm::vec4(0.0f, 0.0f, -distance, 1.0f);
    return clip.z / clip.w;
}

finevk::Camera perspective(float aspect, float nearPlane, float farPlane) {
    finevk::Camera camera;
    camera.setPerspective(CLIENT_FOV, aspect, nearPlane, farPlane);
    camera.moveTo(glm::vec3(0.0f));
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();
    return camera;
}

}  // namespace

TEST(CameraDepth, PerspectiveDrawsFromTheNearPlaneToTheFarPlane) {
    for (float nearPlane : {0.05f, 0.1f, 0.2f, 0.5f, 2.0f}) {
        for (float farPlane : {50.0f, 100.0f, 900.0f, 5000.0f}) {
            finevk::Camera camera = perspective(16.0f / 9.0f, nearPlane, farPlane);

            EXPECT_NEAR(depthAt(camera, nearPlane), 0.0f, 1e-4f) << nearPlane << " " << farPlane;
            EXPECT_NEAR(depthAt(camera, farPlane), 1.0f, 1e-4f) << nearPlane << " " << farPlane;
            EXPECT_LT(depthAt(camera, nearPlane * 0.9f), 0.0f) << nearPlane << " " << farPlane;
            EXPECT_GT(depthAt(camera, nearPlane * 1.1f), 0.0f) << nearPlane << " " << farPlane;
        }
    }
}

TEST(CameraDepth, OrthographicDrawsFromTheNearPlaneToTheFarPlane) {
    finevk::Camera camera;
    camera.setOrthographic(-10.0f, 10.0f, -10.0f, 10.0f, 1.0f, 100.0f);
    camera.moveTo(glm::vec3(0.0f));
    camera.setOrientation(glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.updateState();

    EXPECT_NEAR(depthAt(camera, 1.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(depthAt(camera, 100.0f), 1.0f, 1e-5f);
}

TEST(CameraDepth, FrustumNearPlaneIsTheNearPlane) {
    finevk::Camera camera = perspective(16.0f / 9.0f, CLIENT_NEAR, CLIENT_FAR);
    for (const auto& planes : {camera.state().frustumPlanes, camera.state().viewRelativeFrustumPlanes}) {
        const glm::vec4& plane = planes[4];
        auto signedDistance = [&](float ahead) {
            return glm::dot(glm::vec3(plane), glm::vec3(0.0f, 0.0f, -ahead)) + plane.w;
        };
        EXPECT_NEAR(signedDistance(CLIENT_NEAR), 0.0f, 1e-4f);
        EXPECT_NEAR(signedDistance(CLIENT_NEAR + 1.0f), 1.0f, 1e-4f);
    }
}

// The corners of what's actually drawn have to stay inside the distance the camera keeps from
// walls, or a wall gets sliced open. Measured from the matrix, since it's the matrix that clips.
TEST(CameraDepth, NearPlaneCornersStayInsideTheCameraClearance) {
    for (float aspect : {1.0f, 4.0f / 3.0f, 16.0f / 10.0f, 16.0f / 9.0f, WIDEST_ASPECT}) {
        finevk::Camera camera = perspective(aspect, CLIENT_NEAR, CLIENT_FAR);
        glm::mat4 unproject = glm::inverse(camera.state().viewProjection);

        float reach = 0.0f;
        for (float x : {-1.0f, 1.0f}) {
            for (float y : {-1.0f, 1.0f}) {
                glm::vec4 corner = unproject * glm::vec4(x, y, 0.0f, 1.0f);
                reach = std::max(reach, glm::length(glm::vec3(corner) / corner.w));
            }
        }
        EXPECT_LT(reach, CAMERA_COLLISION_RADIUS) << "aspect " << aspect;
    }
}
