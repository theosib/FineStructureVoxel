#pragma once

/**
 * @file player_controller.hpp
 * @brief First-person player controller with movement and look
 *
 * Design: [10-input.md] §10.2 PlayerController
 *
 * Extracts CameraInput from render_demo into a testable core class.
 * finevk-independent: caller bridges input events and camera output.
 */

#include "finevox/core/physics.hpp"
#include <glm/glm.hpp>

namespace finevox {

class PlayerController {
public:
    PlayerController();
    ~PlayerController() = default;

    PlayerController(const PlayerController&) = delete;
    PlayerController& operator=(const PlayerController&) = delete;

    // ========================================================================
    // Physics Binding (optional — nullptr for fly-only mode)
    // ========================================================================

    /// Set physics body and system for physics-mode movement.
    /// Both must be non-null to enable physics mode, or both null.
    /// Caller must ensure body and system outlive this controller.
    void setPhysics(PhysicsBody* body, PhysicsSystem* system);

    /// Get current physics body (may be nullptr)
    [[nodiscard]] PhysicsBody* physicsBody() const { return body_; }

    // ========================================================================
    // Input (called each frame by the input bridge)
    // ========================================================================

    void setMoveForward(bool active) { moveForward_ = active; }
    void setMoveBack(bool active) { moveBack_ = active; }
    void setMoveLeft(bool active) { moveLeft_ = active; }
    void setMoveRight(bool active) { moveRight_ = active; }

    void setMoveUp(bool active) { moveUp_ = active; }
    void setMoveDown(bool active) { moveDown_ = active; }

    /// Request a jump (consumed on next update if on ground)
    void requestJump();

    /// Apply mouse look delta (raw pixel delta)
    void look(float dx, float dy);

    /// Clear all movement input
    void clearInput();

    // ========================================================================
    // Mode
    // ========================================================================

    /// Toggle fly mode. Handles position sync on transitions.
    void setFlyMode(bool fly);
    [[nodiscard]] bool flyMode() const { return flyMode_; }

    // ========================================================================
    // Configuration
    // ========================================================================

    void setMoveSpeed(float speed) { moveSpeed_ = speed; }
    [[nodiscard]] float moveSpeed() const { return moveSpeed_; }

    void setLookSensitivity(float sens) { lookSensitivity_ = sens; }
    [[nodiscard]] float lookSensitivity() const { return lookSensitivity_; }

    void setJumpVelocity(float vel) { jumpVelocity_ = vel; }
    [[nodiscard]] float jumpVelocity() const { return jumpVelocity_; }

    void setEyeHeight(float height) { eyeHeight_ = height; }
    [[nodiscard]] float eyeHeight() const { return eyeHeight_; }

    // ========================================================================
    // Update
    // ========================================================================

    /// Process one frame of movement.
    /// Physics mode: sets body velocity, calls physics.update().
    /// Fly mode: updates flyPosition_ and flyPositionDelta_.
    void update(float dt);

    // ========================================================================
    // Output (read after update)
    // ========================================================================

    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }

    void setYaw(float y) { yaw_ = y; }
    void setPitch(float p) { pitch_ = glm::clamp(p, -1.5f, 1.5f); }

    /// Forward direction unit vector (from yaw/pitch)
    [[nodiscard]] glm::vec3 forwardVector() const;

    /// Horizontal forward (yaw only, Y=0)
    [[nodiscard]] glm::vec3 horizontalForward() const;

    /// Eye position in world space (double-precision).
    /// Physics mode: body.position() + (0, eyeHeight, 0).
    /// Fly mode: flyPosition_.
    [[nodiscard]] glm::dvec3 eyePosition() const;

    /// Position delta for fly mode (caller applies to camera).
    /// Only meaningful when flyMode() is true.
    [[nodiscard]] glm::dvec3 flyPositionDelta() const { return flyPositionDelta_; }

    /// Horizontal move direction (normalized XZ, zero if no input)
    [[nodiscard]] Vec3 getMoveDirection() const;

    /// Whether the physics body is on the ground
    [[nodiscard]] bool isOnGround() const;

    // ========================================================================
    // Fly mode position tracking
    // ========================================================================

    /// Set fly position (call when switching to fly mode to sync with camera)
    void setFlyPosition(const glm::dvec3& pos) { flyPosition_ = pos; }

    /// Get fly position
    [[nodiscard]] const glm::dvec3& flyPosition() const { return flyPosition_; }

private:
    // Input state
    bool moveForward_ = false;
    bool moveBack_ = false;
    bool moveLeft_ = false;
    bool moveRight_ = false;
    bool moveUp_ = false;
    bool moveDown_ = false;
    bool jumpRequested_ = false;

    // Look state
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    // Configuration
    float moveSpeed_ = 10.0f;
    float lookSensitivity_ = 0.002f;
    float jumpVelocity_ = 8.0f;
    float eyeHeight_ = 1.62f;

    // Mode
    bool flyMode_ = true;

    // Physics (optional)
    PhysicsBody* body_ = nullptr;
    PhysicsSystem* physics_ = nullptr;

    // Fly mode state
    glm::dvec3 flyPosition_{0.0, 0.0, 0.0};
    glm::dvec3 flyPositionDelta_{0.0, 0.0, 0.0};

    void updatePhysicsMovement(float dt);
    void updateFlyMovement(float dt);
};

}  // namespace finevox
