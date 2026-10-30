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

glm::vec2 edgeScroll(glm::vec2 cursor, glm::vec2 viewport, float margin, bool spansScreen) {
    bool outside = cursor.x < 0.0f || cursor.y < 0.0f || cursor.x > viewport.x || cursor.y > viewport.y;
    if (outside && !spansScreen) return glm::vec2(0.0f);

    glm::vec2 direction(0.0f);
    if (cursor.x < margin) direction.x = -1.0f;
    if (cursor.x > viewport.x - margin) direction.x = 1.0f;
    if (cursor.y < margin) direction.y = 1.0f;
    if (cursor.y > viewport.y - margin) direction.y = -1.0f;
    return direction;
}

std::optional<double> groundLevel(std::vector<int32_t> heights) {
    if (heights.empty()) return std::nullopt;
    auto middle = heights.begin() + static_cast<std::ptrdiff_t>(heights.size() / 2);
    std::nth_element(heights.begin(), middle, heights.end());
    return static_cast<double>(*middle);
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
    moveTarget(groundForward * forward + groundRight * right);
}

void TacticalCamera::grab(const ViewBasis& view, glm::vec2 from, glm::vec2 to, glm::vec2 viewport) {
    // A ray this close to level meets the ground absurdly far away, and the view would leap
    constexpr float MIN_DESCENT = 0.02f;

    auto groundUnder = [&](glm::vec2 cursor) -> std::optional<glm::dvec3> {
        WorldRay ray = screenRayToWorld(view, cursor, viewport);
        if (ray.direction.y > -MIN_DESCENT) return std::nullopt;
        double t = (pivot_.y - ray.origin.y) / ray.direction.y;
        return ray.origin + glm::dvec3(ray.direction) * t;
    };

    auto held = groundUnder(from);
    auto now = groundUnder(to);
    if (!held || !now) return;
    moveTarget(glm::vec2(static_cast<float>(held->x - now->x), static_cast<float>(held->z - now->z)));
}

void TacticalCamera::moveTarget(const glm::vec2& move) {
    targetPivot_.x += move.x;
    targetPivot_.z += move.y;
    if (hasBounds_) {
        targetPivot_.x = std::clamp(targetPivot_.x, double(boundsMin_.x), double(boundsMax_.x));
        targetPivot_.z = std::clamp(targetPivot_.z, double(boundsMin_.y), double(boundsMax_.y));
    }
}

void TacticalCamera::followGround(double y) {
    if (std::abs(y - targetPivot_.y) > GROUND_DEADBAND) targetPivot_.y = y;
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
    float climb = 1.0f - std::exp(-dt * 3.0f);    // Slower: a pan is the player's, a hill isn't
    pivot_.x += (targetPivot_.x - pivot_.x) * blend;
    pivot_.z += (targetPivot_.z - pivot_.z) * blend;
    pivot_.y += (targetPivot_.y - pivot_.y) * climb;
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
