#include "finevox/core/tactical_camera.hpp"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace finevox {

ViewBasis makeViewBasis(const glm::dvec3& position, const glm::vec3& forward, float fovYDegrees,
                        float aspect) {
    ViewBasis view;
    view.position = position;
    view.forward = glm::normalize(forward);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(view.forward, worldUp)) > 0.999f) {
        worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    view.right = glm::normalize(glm::cross(view.forward, worldUp));
    view.up = glm::cross(view.right, view.forward);
    view.fovYDegrees = fovYDegrees;
    view.aspect = aspect;
    return view;
}

WorldRay screenRayToWorld(const ViewBasis& view, glm::vec2 cursor, glm::vec2 viewport) {
    float ndcX = 2.0f * cursor.x / viewport.x - 1.0f;
    float ndcY = 1.0f - 2.0f * cursor.y / viewport.y;
    float tanHalf = std::tan(glm::radians(view.fovYDegrees) * 0.5f);

    glm::vec3 direction = view.forward + view.right * (ndcX * tanHalf * view.aspect) +
                          view.up * (ndcY * tanHalf);
    return WorldRay{view.position, glm::normalize(direction)};
}

std::optional<glm::vec2> worldToScreen(const ViewBasis& view, const glm::dvec3& world,
                                       glm::vec2 viewport) {
    glm::vec3 relative(world - view.position);
    float depth = glm::dot(relative, view.forward);
    if (depth <= 0.01f) return std::nullopt;

    float tanHalf = std::tan(glm::radians(view.fovYDegrees) * 0.5f);
    float ndcX = glm::dot(relative, view.right) / (depth * tanHalf * view.aspect);
    float ndcY = glm::dot(relative, view.up) / (depth * tanHalf);
    return glm::vec2((ndcX + 1.0f) * 0.5f * viewport.x, (1.0f - ndcY) * 0.5f * viewport.y);
}

void TacticalCamera::setPivot(const glm::dvec3& pivot) {
    pivot_ = pivot;
    targetPivot_ = pivot;
}

void TacticalCamera::pan(float right, float forward) {
    float sinYaw = std::sin(yaw_);
    float cosYaw = std::cos(yaw_);
    // Ground-plane axes: looking along -position offset from the pivot
    glm::vec2 groundForward(-sinYaw, -cosYaw);
    glm::vec2 groundRight(cosYaw, -sinYaw);
    glm::vec2 move = groundForward * forward + groundRight * right;
    targetPivot_.x += move.x;
    targetPivot_.z += move.y;
    if (hasBounds_) {
        targetPivot_.x = std::clamp(targetPivot_.x, double(boundsMin_.x), double(boundsMax_.x));
        targetPivot_.z = std::clamp(targetPivot_.z, double(boundsMin_.y), double(boundsMax_.y));
    }
}

void TacticalCamera::orbit(float yawRadians, float pitchRadians) {
    yaw_ += yawRadians;
    pitch_ = std::clamp(pitch_ + pitchRadians, MIN_PITCH, MAX_PITCH);
}

void TacticalCamera::zoom(float factor) {
    targetDistance_ = std::clamp(targetDistance_ * factor, MIN_DISTANCE, MAX_DISTANCE);
}

void TacticalCamera::setDistance(float d) {
    distance_ = std::clamp(d, MIN_DISTANCE, MAX_DISTANCE);
    targetDistance_ = distance_;
}

void TacticalCamera::setBounds(const glm::vec2& min, const glm::vec2& max) {
    hasBounds_ = true;
    boundsMin_ = min;
    boundsMax_ = max;
}

void TacticalCamera::update(float dt) {
    // Exponential smoothing: frame-rate independent, no overshoot
    float blend = 1.0f - std::exp(-dt * 12.0f);
    pivot_ += (targetPivot_ - pivot_) * double(blend);
    distance_ += (targetDistance_ - distance_) * blend;
}

glm::dvec3 TacticalCamera::position() const {
    double horizontal = distance_ * std::cos(pitch_);
    return glm::dvec3(pivot_.x + horizontal * std::sin(yaw_), pivot_.y + distance_ * std::sin(pitch_),
                      pivot_.z + horizontal * std::cos(yaw_));
}

glm::vec3 TacticalCamera::forward() const {
    return glm::normalize(glm::vec3(pivot_ - position()));
}

}  // namespace finevox
