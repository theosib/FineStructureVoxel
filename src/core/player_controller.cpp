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

    if (flyMode_) {
        updateFlyMovement(dt);
    } else if (body_ && physics_) {
        updatePhysicsMovement(dt);
    }
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

    Vec3 vel = body_->velocity();
    if (glm::length(moveDir) > 0.0f) {
        vel.x = moveDir.x * moveSpeed_;
        vel.z = moveDir.z * moveSpeed_;
    } else {
        // Quick stop friction
        vel.x *= 0.8f;
        vel.z *= 0.8f;
        if (std::abs(vel.x) < 0.1f) vel.x = 0.0f;
        if (std::abs(vel.z) < 0.1f) vel.z = 0.0f;
    }

    if (jumpRequested_ && body_->isOnGround()) {
        vel.y = jumpVelocity_;
        jumpRequested_ = false;
    }

    body_->setVelocity(vel);
    physics_->update(*body_, dt);
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
