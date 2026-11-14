#include "finevox/core/player_controller.hpp"
#include <glm/gtc/constants.hpp>
#include <cmath>

namespace finevox {

PlayerController::PlayerController() = default;

void PlayerController::setPhysics(PhysicsBody* body, PhysicsSystem* system) {
    body_ = body;
    physics_ = system;
}

void PlayerController::requestJump() {
    jumpRequested_ = true;
    // Two presses close together start or stop flight, where it's allowed
    if (flightAllowed_ && body_ && lastJumpPress_ >= 0.0f && clock_ - lastJumpPress_ <= doubleTapTime_) {
        setFlying(!flying_);
        lastJumpPress_ = -1.0f;
        jumpRequested_ = false;
        return;
    }
    lastJumpPress_ = clock_;
}

void PlayerController::setFlightAllowed(bool allowed) {
    flightAllowed_ = allowed;
    if (!allowed) setFlying(false);
}

void PlayerController::setFlying(bool flying) {
    if (flying && !flightAllowed_) return;
    if (flying == flying_) return;
    flying_ = flying;
    if (body_) {
        Vec3 vel = body_->velocity();
        vel.y = 0.0f;
        body_->setVelocity(vel);
    }
}

void PlayerController::look(float dx, float dy) {
    yaw_ -= dx * lookSensitivity_;
    pitch_ -= dy * lookSensitivity_;
    pitch_ = glm::clamp(pitch_, -1.5f, 1.5f);
}

void PlayerController::clearInput() {
    moveForward_ = false;
    moveBack_ = false;
    moveLeft_ = false;
    moveRight_ = false;
    moveUp_ = false;
    moveDown_ = false;
    jumpRequested_ = false;
    jumpHeld_ = false;
    sneak_ = false;
}

void PlayerController::setFlyMode(bool fly) {
    if (fly == flyMode_) return;

    if (fly && body_) {
        // Switching from physics to fly: sync fly position from body
        Vec3 pos = body_->position();
        flyPosition_ = glm::dvec3(pos.x, pos.y + eyeHeight_, pos.z);
    } else if (!fly && body_) {
        // Switching from fly to physics: sync body from fly position
        body_->setPosition(Vec3(
            static_cast<float>(flyPosition_.x),
            static_cast<float>(flyPosition_.y - eyeHeight_),
            static_cast<float>(flyPosition_.z)
        ));
        body_->setVelocity(Vec3(0.0f));
    }

    flyMode_ = fly;
}

void PlayerController::update(float dt) {
    flyPositionDelta_ = glm::dvec3(0.0);
    clock_ += dt;

    if (flyMode_) {
        updateFlyMovement(dt);
    } else if (body_ && physics_) {
        updateCrouch();
        if (flying_) {
            updateBodyFlight(dt);
        } else {
            updatePhysicsMovement(dt);
        }
    }
    // A press is for this update only: one made in the air doesn't jump on landing
    jumpRequested_ = false;
}

void PlayerController::updateCrouch() {
    bool want = sneak_ && !flying_;
    if (want == crouched_) return;
    Vec3 he = body_->halfExtents();
    float change = crouchShrink_ * 0.5f;
    if (want) {
        body_->setHalfExtents(Vec3(he.x, he.y - change, he.z));
        crouched_ = true;
        return;
    }
    // Stand up only where the full height fits
    Vec3 standing(he.x, he.y + change, he.z);
    Vec3 pos = body_->position();
    AABB full(pos.x - standing.x, pos.y, pos.z - standing.z,
              pos.x + standing.x, pos.y + standing.y * 2.0f, pos.z + standing.z);
    if (!physics_->isClear(full)) return;
    body_->setHalfExtents(standing);
    crouched_ = false;
}

Vec3 PlayerController::guardEdge(Vec3 move) const {
    float depth = body_->maxStepHeight();
    AABB box = body_->boundingBox();
    // Only on firm ground: one already hanging over the edge isn't held there
    if (!physics_->hasFooting(box, depth)) return move;
    // Held back a little short of the very edge, so that rounding never leaves the body
    // reading as already over it
    constexpr float HOLD = 0.01f;
    AABB held(box.min.x + HOLD, box.min.y, box.min.z + HOLD, box.max.x - HOLD, box.max.y, box.max.z - HOLD);
    auto footed = [&](float dx, float dz) {
        return physics_->hasFooting(held.translated(Vec3(dx, 0.0f, dz)), depth);
    };
    // The longest share of a move that keeps footing, found by halving: footing over a share
    // means footing over less of it too, as the body only slides along a straight line
    auto longest = [&](float dx, float dz) {
        if (footed(dx, dz)) return 1.0f;
        float lo = 0.0f, hi = 1.0f;
        for (int i = 0; i < 12; ++i) {
            float mid = 0.5f * (lo + hi);
            if (footed(dx * mid, dz * mid)) lo = mid; else hi = mid;
        }
        return lo;
    };
    // Each way on its own first, so a body at a corner can still slide along the edge
    move.x *= longest(move.x, 0.0f);
    move.z *= longest(0.0f, move.z);
    float both = longest(move.x, move.z);
    move.x *= both;
    move.z *= both;
    return move;
}

glm::vec3 PlayerController::forwardVector() const {
    return glm::vec3{
        std::cos(pitch_) * std::sin(yaw_),
        std::sin(pitch_),
        std::cos(pitch_) * std::cos(yaw_)
    };
}

glm::vec3 PlayerController::horizontalForward() const {
    return glm::vec3{std::sin(yaw_), 0.0f, std::cos(yaw_)};
}

glm::dvec3 PlayerController::eyePosition() const {
    if (!flyMode_ && body_) {
        Vec3 pos = body_->position();
        return glm::dvec3(pos.x, pos.y + eyeHeight_, pos.z);
    }
    return flyPosition_;
}

Vec3 PlayerController::getMoveDirection() const {
    if (!moveForward_ && !moveBack_ && !moveLeft_ && !moveRight_) {
        return Vec3(0.0f);
    }

    Vec3 hfwd = horizontalForward();
    Vec3 right = glm::normalize(glm::cross(hfwd, Vec3(0, 1, 0)));

    Vec3 dir(0.0f);
    if (moveForward_) dir += hfwd;
    if (moveBack_) dir -= hfwd;
    if (moveRight_) dir += right;
    if (moveLeft_) dir -= right;

    if (glm::length(dir) > 0.0f) {
        return glm::normalize(dir);
    }
    return Vec3(0.0f);
}

bool PlayerController::isOnGround() const {
    if (body_) return body_->isOnGround();
    return false;
}

void PlayerController::updatePhysicsMovement(float dt) {
    Vec3 moveDir = getMoveDirection();

    FluidContactInfo contact;
    if (fluidQuery_) {
        contact = computeFluidContact(*body_, eyeHeight_, fluidQuery_);
    }

    float speed = contact.inFluid ? moveSpeed_ * 0.65f : moveSpeed_;
    if (crouched_ && !contact.inFluid) speed *= sneakSpeed_;
    bool wantUp = jumpHeld_ || jumpRequested_;
    Vec3 vel = body_->velocity();
    if (glm::length(moveDir) > 0.0f) {
        vel.x = moveDir.x * speed;
        vel.z = moveDir.z * speed;
    } else {
        // Quick stop friction
        vel.x *= 0.8f;
        vel.z *= 0.8f;
        if (std::abs(vel.x) < 0.1f) vel.x = 0.0f;
        if (std::abs(vel.z) < 0.1f) vel.z = 0.0f;
    }

    bool swimSurface = false;
    if (contact.inFluid) {
        constexpr float kSurfaceSubmersion = 0.28f;
        float bodyHeight = body_->halfExtents().y * 2.0f;
        float depthBelowTarget = (contact.submersion - kSurfaceSubmersion) * bodyHeight;
        if (wantUp) {
            swimSurface = depthBelowTarget < 0.45f;
            if (body_->isOnGround() && contact.submersion < 0.6f) {
                vel.y = std::max(vel.y, jumpVelocity_);
            } else if (depthBelowTarget > 0.0f) {
                float targetVelY = std::min(depthBelowTarget * 6.0f, 3.5f);
                vel.y = std::max(vel.y, targetVelY + physics_->gravity() * dt);
            } else if (vel.y <= 3.6f) {
                // At the surface: hold steady unless already rising out on a jump or bank hop
                vel.y = std::max(vel.y * std::exp(-12.0f * dt), 0.0f) + physics_->gravity() * dt;
            }
        } else {
            // Gently sink with partial buoyancy and water drag
            vel.y += 0.72f * physics_->gravity() * contact.submersion * dt;
            vel.y *= std::exp(-5.0f * dt);
        }
    } else if (wantUp && body_->isOnGround()) {
        vel.y = jumpVelocity_;
    }

    // Sneaking on the ground: hold back from any edge higher than a step
    if (sneak_ && !contact.inFluid && body_->isOnGround() && vel.y <= 0.0f && dt > 0.0f) {
        Vec3 held = guardEdge(Vec3(vel.x * dt, 0.0f, vel.z * dt));
        vel.x = held.x / dt;
        vel.z = held.z / dt;
    }

    body_->setVelocity(vel);
    Vec3 desiredHoriz(vel.x * dt, 0.0f, vel.z * dt);
    Vec3 actualMove = physics_->update(*body_, dt);

    // When treading water at the surface and pressing into a bank, hop up onto the ledge
    if (swimSurface && glm::length(moveDir) > 0.0f) {
        bool blockedHoriz = std::abs(actualMove.x - desiredHoriz.x) > 1e-4f ||
                            std::abs(actualMove.z - desiredHoriz.z) > 1e-4f;
        if (blockedHoriz) {
            Vec3 postVel = body_->velocity();
            postVel.y = std::max(postVel.y, std::max(6.0f, jumpVelocity_ * 0.9f));
            body_->setVelocity(postVel);
        }
    }
}

void PlayerController::updateBodyFlight(float dt) {
    Vec3 moveDir = getMoveDirection();
    Vec3 vel = body_->velocity();
    float speed = moveSpeed_ * flySpeed_;
    // Eases to the wanted speed, so flight glides a little at the start and stop
    float ease = 1.0f - std::exp(-10.0f * dt);
    vel.x += (moveDir.x * speed - vel.x) * ease;
    vel.z += (moveDir.z * speed - vel.z) * ease;
    float climb = 0.0f;
    if (jumpHeld_ || jumpRequested_) climb += flyClimb_;
    if (sneak_) climb -= flyClimb_;
    vel.y = climb;
    body_->setVelocity(vel);

    Vec3 want = vel * dt;
    Vec3 moved = physics_->moveBody(*body_, want);
    if (std::abs(moved.x - want.x) > 1e-4f) vel.x = 0.0f;
    if (std::abs(moved.z - want.z) > 1e-4f) vel.z = 0.0f;
    body_->setVelocity(vel);
    // Coming down onto the ground ends flight
    if (climb < 0.0f && body_->isOnGround()) setFlying(false);
}

void PlayerController::updateFlyMovement(float dt) {
    glm::dvec3 hfwd{std::sin(yaw_), 0.0, std::cos(yaw_)};
    glm::dvec3 right = glm::normalize(glm::cross(hfwd, glm::dvec3(0, 1, 0)));

    glm::dvec3 velocity{0.0};
    if (moveForward_) velocity += hfwd;
    if (moveBack_) velocity -= hfwd;
    if (moveRight_) velocity += right;
    if (moveLeft_) velocity -= right;
    if (moveUp_) velocity.y += 1.0;
    if (moveDown_) velocity.y -= 1.0;

    if (glm::length(velocity) > 0.0) {
        velocity = glm::normalize(velocity) * static_cast<double>(moveSpeed_);
    }

    flyPositionDelta_ = velocity * static_cast<double>(dt);
    flyPosition_ += flyPositionDelta_;
}

}  // namespace finevox
