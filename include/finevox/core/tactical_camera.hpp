#pragma once

/**
 * @file tactical_camera.hpp
 * @brief Overhead orbit camera and screen/world projection
 *
 * Pure math with no graphics dependency. The same ViewBasis describes any camera
 * (overhead or first-person), so screen picking works identically in both.
 */

#include <glm/glm.hpp>
#include <optional>

namespace finevox {

/// Everything needed to map between screen and world for one camera pose
struct ViewBasis {
    glm::dvec3 position{0.0};
    glm::vec3 forward{0.0f, 0.0f, -1.0f};
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    float fovYDegrees = 70.0f;
    float aspect = 16.0f / 9.0f;
};

/// Build a basis from a pose; `forward` need not be normalised
[[nodiscard]] ViewBasis makeViewBasis(const glm::dvec3& position, const glm::vec3& forward,
                                      float fovYDegrees, float aspect);

struct WorldRay {
    glm::dvec3 origin{0.0};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};  ///< Unit length
};

/// Ray through a cursor position. Cursor origin is top-left, in the same units as viewport.
[[nodiscard]] WorldRay screenRayToWorld(const ViewBasis& view, glm::vec2 cursor, glm::vec2 viewport);

/// Project a world point to the screen. nullopt if it's behind the camera.
[[nodiscard]] std::optional<glm::vec2> worldToScreen(const ViewBasis& view, const glm::dvec3& world,
                                                     glm::vec2 viewport);

/// Orbit camera around a ground pivot: pan moves the pivot, zoom changes the arm.
class TacticalCamera {
public:
    void setPivot(const glm::dvec3& pivot);
    void setPivotHeight(double y) { targetPivot_.y = y; }

    /// Pan along the ground, in the camera's own forward/right axes. Units are
    /// world blocks, scaled by the caller for zoom-independent feel if wanted.
    void pan(float right, float forward);

    /// Rotate around the pivot
    void orbit(float yawRadians, float pitchRadians);

    /// Multiply arm length (below 1 zooms in)
    void zoom(float factor);

    /// Advance smoothing
    void update(float dt);

    /// Keep the pivot inside an axis-aligned XZ rectangle
    void setBounds(const glm::vec2& min, const glm::vec2& max);

    [[nodiscard]] glm::dvec3 pivot() const { return pivot_; }
    [[nodiscard]] glm::dvec3 targetPivot() const { return targetPivot_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] float distance() const { return distance_; }
    void setDistance(float d);

    [[nodiscard]] glm::dvec3 position() const;
    [[nodiscard]] glm::vec3 forward() const;

    static constexpr float MIN_DISTANCE = 8.0f;
    static constexpr float MAX_DISTANCE = 160.0f;
    static constexpr float MIN_PITCH = 0.35f;   ///< Radians below horizontal
    static constexpr float MAX_PITCH = 1.45f;

private:
    glm::dvec3 pivot_{0.0};
    glm::dvec3 targetPivot_{0.0};
    float yaw_ = 0.6f;
    float pitch_ = 0.95f;
    float distance_ = 60.0f;
    float targetDistance_ = 60.0f;
    bool hasBounds_ = false;
    glm::vec2 boundsMin_{0.0f};
    glm::vec2 boundsMax_{0.0f};
};

}  // namespace finevox
