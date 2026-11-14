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

    /// Set fluid query callback so physics movement can swim and float in fluids.
    void setFluidQuery(FluidQueryProvider query) { fluidQuery_ = std::move(query); }

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

    /// A jump key was pressed: jumps on the next update if on the ground, and counts towards a
    /// double tap that starts or stops flight. Forgotten after that update, so a press made in
    /// the air doesn't wait to jump on landing.
    void requestJump();

    /// The jump key is held: jumps again on every landing, swims up, flies up
    void setJumpHeld(bool held) { jumpHeld_ = held; }

    /// Sneaking: crouched, slower, won't walk off an edge, flies down
    void setSneak(bool sneak) { sneak_ = sneak; }
    [[nodiscard]] bool sneaking() const { return sneak_; }

    /// Crouched: the body shorter by crouchShrink. Sneaking on foot crouches; letting go stands
    /// up only where there's room, so a body under a low ceiling stays crouched (and slow).
    [[nodiscard]] bool crouched() const { return crouched_; }
    void setCrouchShrink(float blocks) { crouchShrink_ = blocks; }

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

    /// Walking speed while sneaking, as a fraction of moveSpeed
    void setSneakSpeed(float fraction) { sneakSpeed_ = fraction; }
    [[nodiscard]] float sneakSpeed() const { return sneakSpeed_; }

    // ========================================================================
    // Flight with a body: no gravity, still collides
    // ========================================================================

    /// Whether a double tap of jump may start flight. Turning it off ends any flight.
    void setFlightAllowed(bool allowed);
    [[nodiscard]] bool flightAllowed() const { return flightAllowed_; }

    void setFlying(bool flying);
    [[nodiscard]] bool flying() const { return flying_; }

    /// Horizontal speed in flight, as a multiple of moveSpeed
    void setFlySpeed(float multiple) { flySpeed_ = multiple; }
    /// Rise and fall speed in flight, blocks per second
    void setFlyClimb(float speed) { flyClimb_ = speed; }
    /// Most seconds between two jump presses that make a double tap
    void setDoubleTapTime(float seconds) { doubleTapTime_ = seconds; }

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
    bool jumpHeld_ = false;
    bool sneak_ = false;
    bool crouched_ = false;
    float crouchShrink_ = 0.25f;

    // Look state
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;

    // Configuration
    float moveSpeed_ = 10.0f;
    float lookSensitivity_ = 0.002f;
    float jumpVelocity_ = 8.0f;
    float eyeHeight_ = 1.65f;
    float sneakSpeed_ = 0.4f;

    // Flight with a body
    bool flightAllowed_ = false;
    bool flying_ = false;
    float flySpeed_ = 2.0f;
    float flyClimb_ = 6.0f;
    float doubleTapTime_ = 0.3f;
    float clock_ = 0.0f;
    float lastJumpPress_ = -1.0f;

    // Mode
    bool flyMode_ = true;

    // Physics (optional)
    PhysicsBody* body_ = nullptr;
    PhysicsSystem* physics_ = nullptr;
    FluidQueryProvider fluidQuery_;

    // Fly mode state
    glm::dvec3 flyPosition_{0.0, 0.0, 0.0};
    glm::dvec3 flyPositionDelta_{0.0, 0.0, 0.0};

    void updatePhysicsMovement(float dt);
    /// Crouch or stand to match the sneak key, where there's room
    void updateCrouch();
    void updateBodyFlight(float dt);
    /// Cut a horizontal move short where it would leave the body with nothing under it
    [[nodiscard]] Vec3 guardEdge(Vec3 move) const;
    void updateFlyMovement(float dt);
};

}  // namespace finevox
