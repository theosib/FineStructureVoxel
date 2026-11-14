#include <gtest/gtest.h>
#include "finevox/core/player_controller.hpp"
#include "finevox/core/key_bindings.hpp"
#include "finevox/core/config.hpp"
#include <cmath>
#include <glm/gtc/constants.hpp>
#include <vector>

using namespace finevox;

// ============================================================================
// PlayerController — Construction
// ============================================================================

TEST(PlayerControllerTest, DefaultConstruction) {
    PlayerController pc;
    EXPECT_FLOAT_EQ(pc.yaw(), 0.0f);
    EXPECT_FLOAT_EQ(pc.pitch(), 0.0f);
    EXPECT_TRUE(pc.flyMode());
    EXPECT_FLOAT_EQ(pc.moveSpeed(), 10.0f);
    EXPECT_FLOAT_EQ(pc.lookSensitivity(), 0.002f);
    EXPECT_FLOAT_EQ(pc.jumpVelocity(), 8.0f);
    EXPECT_FLOAT_EQ(pc.eyeHeight(), 1.65f);
    EXPECT_EQ(pc.physicsBody(), nullptr);
}

// ============================================================================
// PlayerController — Look
// ============================================================================

TEST(PlayerControllerTest, LookYaw) {
    PlayerController pc;
    pc.setLookSensitivity(0.01f);
    pc.look(100.0f, 0.0f);
    // yaw -= dx * sensitivity = -100 * 0.01 = -1.0
    EXPECT_NEAR(pc.yaw(), -1.0f, 0.001f);
    EXPECT_FLOAT_EQ(pc.pitch(), 0.0f);
}

TEST(PlayerControllerTest, LookPitch) {
    PlayerController pc;
    pc.setLookSensitivity(0.01f);
    pc.look(0.0f, 50.0f);
    // pitch -= dy * sensitivity = -50 * 0.01 = -0.5
    EXPECT_NEAR(pc.pitch(), -0.5f, 0.001f);
    EXPECT_FLOAT_EQ(pc.yaw(), 0.0f);
}

TEST(PlayerControllerTest, PitchClamp) {
    PlayerController pc;
    pc.setLookSensitivity(1.0f);
    // Push pitch way past +1.5
    pc.look(0.0f, -10.0f);  // pitch -= -10*1.0 = +10.0, clamped to 1.5
    EXPECT_FLOAT_EQ(pc.pitch(), 1.5f);

    // Push pitch way past -1.5
    pc.setPitch(0.0f);
    pc.look(0.0f, 10.0f);  // pitch -= 10*1.0 = -10.0, clamped to -1.5
    EXPECT_FLOAT_EQ(pc.pitch(), -1.5f);
}

TEST(PlayerControllerTest, SetPitchClamps) {
    PlayerController pc;
    pc.setPitch(5.0f);
    EXPECT_FLOAT_EQ(pc.pitch(), 1.5f);
    pc.setPitch(-5.0f);
    EXPECT_FLOAT_EQ(pc.pitch(), -1.5f);
}

// ============================================================================
// PlayerController — Forward Vector
// ============================================================================

TEST(PlayerControllerTest, ForwardVectorDefaultOrientation) {
    PlayerController pc;
    // yaw=0, pitch=0 -> forward is (0, 0, 1)
    glm::vec3 fwd = pc.forwardVector();
    EXPECT_NEAR(fwd.x, 0.0f, 0.001f);
    EXPECT_NEAR(fwd.y, 0.0f, 0.001f);
    EXPECT_NEAR(fwd.z, 1.0f, 0.001f);
}

TEST(PlayerControllerTest, ForwardVectorYaw90) {
    PlayerController pc;
    pc.setYaw(glm::half_pi<float>());
    // yaw=pi/2, pitch=0 -> forward is (1, 0, 0)
    glm::vec3 fwd = pc.forwardVector();
    EXPECT_NEAR(fwd.x, 1.0f, 0.001f);
    EXPECT_NEAR(fwd.y, 0.0f, 0.001f);
    EXPECT_NEAR(fwd.z, 0.0f, 0.001f);
}

TEST(PlayerControllerTest, ForwardVectorUnitLength) {
    PlayerController pc;
    pc.setYaw(1.23f);
    pc.setPitch(0.5f);
    glm::vec3 fwd = pc.forwardVector();
    EXPECT_NEAR(glm::length(fwd), 1.0f, 0.001f);
}

// ============================================================================
// PlayerController — Horizontal Forward
// ============================================================================

TEST(PlayerControllerTest, HorizontalForwardIgnoresPitch) {
    PlayerController pc;
    pc.setPitch(1.0f);
    glm::vec3 hfwd = pc.horizontalForward();
    EXPECT_FLOAT_EQ(hfwd.y, 0.0f);
    EXPECT_NEAR(glm::length(hfwd), 1.0f, 0.001f);
}

TEST(PlayerControllerTest, HorizontalForwardDefault) {
    PlayerController pc;
    // yaw=0 -> (0, 0, 1)
    glm::vec3 hfwd = pc.horizontalForward();
    EXPECT_NEAR(hfwd.x, 0.0f, 0.001f);
    EXPECT_NEAR(hfwd.z, 1.0f, 0.001f);
}

// ============================================================================
// PlayerController — Move Direction
// ============================================================================

TEST(PlayerControllerTest, MoveDirectionNoInput) {
    PlayerController pc;
    Vec3 dir = pc.getMoveDirection();
    EXPECT_FLOAT_EQ(dir.x, 0.0f);
    EXPECT_FLOAT_EQ(dir.y, 0.0f);
    EXPECT_FLOAT_EQ(dir.z, 0.0f);
}

TEST(PlayerControllerTest, MoveDirectionForward) {
    PlayerController pc;
    pc.setMoveForward(true);
    Vec3 dir = pc.getMoveDirection();
    // yaw=0 -> forward is (0, 0, 1), move direction matches
    EXPECT_NEAR(dir.x, 0.0f, 0.001f);
    EXPECT_FLOAT_EQ(dir.y, 0.0f);
    EXPECT_NEAR(dir.z, 1.0f, 0.001f);
}

TEST(PlayerControllerTest, MoveDirectionDiagonalNormalized) {
    PlayerController pc;
    pc.setMoveForward(true);
    pc.setMoveRight(true);
    Vec3 dir = pc.getMoveDirection();
    // Diagonal should be normalized (length ~1.0, not sqrt(2))
    EXPECT_NEAR(glm::length(dir), 1.0f, 0.001f);
}

// ============================================================================
// PlayerController — Fly Movement
// ============================================================================

TEST(PlayerControllerTest, FlyMovementForward) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(0.0));
    pc.setMoveSpeed(10.0f);
    pc.setMoveForward(true);
    pc.update(1.0f);

    // Should move 10 units in forward direction (yaw=0 -> +Z)
    glm::dvec3 pos = pc.flyPosition();
    EXPECT_NEAR(pos.z, 10.0, 0.01);
    EXPECT_NEAR(pos.x, 0.0, 0.01);
}

TEST(PlayerControllerTest, FlyMovementUp) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(0.0));
    pc.setMoveUp(true);
    pc.update(1.0f);

    glm::dvec3 pos = pc.flyPosition();
    EXPECT_NEAR(pos.y, 10.0, 0.01);
}

TEST(PlayerControllerTest, FlyMovementDiagonalSpeed) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(0.0));
    pc.setMoveSpeed(10.0f);
    pc.setMoveForward(true);
    pc.setMoveRight(true);
    pc.update(1.0f);

    // Diagonal should be normalized — total distance should be 10, not 14.14
    glm::dvec3 pos = pc.flyPosition();
    double dist = glm::length(pos);
    EXPECT_NEAR(dist, 10.0, 0.01);
}

TEST(PlayerControllerTest, FlyNoInputNoMovement) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(5.0, 10.0, 15.0));
    pc.update(1.0f);

    glm::dvec3 pos = pc.flyPosition();
    EXPECT_DOUBLE_EQ(pos.x, 5.0);
    EXPECT_DOUBLE_EQ(pos.y, 10.0);
    EXPECT_DOUBLE_EQ(pos.z, 15.0);
}

TEST(PlayerControllerTest, FlyPositionDelta) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(0.0));
    pc.setMoveForward(true);
    pc.update(0.5f);

    glm::dvec3 delta = pc.flyPositionDelta();
    EXPECT_NEAR(delta.z, 5.0, 0.01);  // 10 * 0.5
}

// ============================================================================
// PlayerController — Physics Movement
// ============================================================================

TEST(PlayerControllerTest, PhysicsMovementSetsVelocity) {
    // No-collision shape provider (empty world)
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);

    SimplePhysicsBody body(Vec3(0, 10, 0), Vec3(0.3f, 0.9f, 0.3f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);
    pc.setMoveForward(true);
    pc.update(0.1f);

    // Body velocity should be set in the forward direction
    Vec3 vel = body.velocity();
    EXPECT_NEAR(vel.z, 10.0f, 0.5f);  // moveSpeed=10, yaw=0 -> Z direction
}

TEST(PlayerControllerTest, PhysicsJumpOnGround) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);

    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));
    body.setOnGround(true);

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);
    pc.setJumpVelocity(8.0f);
    pc.requestJump();
    pc.update(0.016f);

    // Jump should have been applied
    // Note: gravity may have reduced it slightly during the same update
    // Just check that velocity.y was positive at some point
    // After physics.update, gravity subtracts, but starting from 8.0 with dt=0.016
    // gravity (-32) * 0.016 = -0.512, so vel.y should be ~7.49
    EXPECT_GT(body.velocity().y, 5.0f);
}

TEST(PlayerControllerTest, PhysicsJumpNotOnGround) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);

    SimplePhysicsBody body(Vec3(0, 10, 0), Vec3(0.3f, 0.9f, 0.3f));
    body.setOnGround(false);

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);
    pc.requestJump();
    pc.update(0.016f);

    // Jump should NOT be applied when not on ground
    // Velocity.y should be negative (only gravity)
    EXPECT_LE(body.velocity().y, 0.0f);
}

TEST(PlayerControllerTest, PhysicsFriction) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);

    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));
    body.setVelocity(Vec3(5.0f, 0.0f, 5.0f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);
    // No movement input — friction should reduce horizontal velocity
    pc.update(0.016f);

    Vec3 vel = body.velocity();
    EXPECT_LT(std::abs(vel.x), 5.0f);
    EXPECT_LT(std::abs(vel.z), 5.0f);
}

// ============================================================================
// PlayerController — Clear Input
// ============================================================================

TEST(PlayerControllerTest, ClearInput) {
    PlayerController pc;
    pc.setMoveForward(true);
    pc.setMoveBack(true);
    pc.setMoveLeft(true);
    pc.setMoveRight(true);
    pc.setMoveUp(true);
    pc.setMoveDown(true);
    pc.requestJump();

    pc.clearInput();

    // After clear, no movement should happen
    pc.setFlyPosition(glm::dvec3(0.0));
    pc.update(1.0f);
    glm::dvec3 pos = pc.flyPosition();
    EXPECT_DOUBLE_EQ(pos.x, 0.0);
    EXPECT_DOUBLE_EQ(pos.y, 0.0);
    EXPECT_DOUBLE_EQ(pos.z, 0.0);
}

// ============================================================================
// PlayerController — Eye Position
// ============================================================================

TEST(PlayerControllerTest, EyePositionFlyMode) {
    PlayerController pc;
    pc.setFlyPosition(glm::dvec3(10.0, 20.0, 30.0));

    glm::dvec3 eye = pc.eyePosition();
    EXPECT_DOUBLE_EQ(eye.x, 10.0);
    EXPECT_DOUBLE_EQ(eye.y, 20.0);
    EXPECT_DOUBLE_EQ(eye.z, 30.0);
}

TEST(PlayerControllerTest, EyePositionPhysicsMode) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);
    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);
    pc.setEyeHeight(1.65f);

    // Set body position after mode switch (transition syncs from flyPosition)
    body.setPosition(Vec3(5, 10, 15));

    glm::dvec3 eye = pc.eyePosition();
    EXPECT_NEAR(eye.x, 5.0, 0.001);
    EXPECT_NEAR(eye.y, 11.65, 0.001);
    EXPECT_NEAR(eye.z, 15.0, 0.001);
}

// ============================================================================
// PlayerController — Mode Switching
// ============================================================================

TEST(PlayerControllerTest, SwitchToFlyFromPhysics) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);
    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);

    // Set body position after switching to physics mode
    body.setPosition(Vec3(5, 10, 15));

    // Switch to fly mode — fly position should sync from body + eye height
    pc.setFlyMode(true);
    glm::dvec3 fly = pc.flyPosition();
    EXPECT_NEAR(fly.x, 5.0, 0.001);
    EXPECT_NEAR(fly.y, 10.0 + pc.eyeHeight(), 0.001);
    EXPECT_NEAR(fly.z, 15.0, 0.001);
}

TEST(PlayerControllerTest, SwitchToPhysicsFromFly) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);
    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyPosition(glm::dvec3(5.0, 20.0, 15.0));

    // Switch to physics mode — body should sync from fly position - eye height
    pc.setFlyMode(false);
    Vec3 bodyPos = body.position();
    EXPECT_NEAR(bodyPos.x, 5.0f, 0.01f);
    EXPECT_NEAR(bodyPos.y, 20.0f - pc.eyeHeight(), 0.01f);
    EXPECT_NEAR(bodyPos.z, 15.0f, 0.01f);
}

// ============================================================================
// PlayerController — IsOnGround
// ============================================================================

TEST(PlayerControllerTest, IsOnGroundNoBody) {
    PlayerController pc;
    EXPECT_FALSE(pc.isOnGround());
}

TEST(PlayerControllerTest, IsOnGroundDelegates) {
    BlockShapeProvider noCollision = [](const BlockCoord&, RaycastMode) -> const CollisionShape* {
        return nullptr;
    };
    PhysicsSystem physics(noCollision);
    SimplePhysicsBody body(Vec3(0, 0, 0), Vec3(0.3f, 0.9f, 0.3f));

    PlayerController pc;
    pc.setPhysics(&body, &physics);

    body.setOnGround(true);
    EXPECT_TRUE(pc.isOnGround());
    body.setOnGround(false);
    EXPECT_FALSE(pc.isOnGround());
}

// ============================================================================
// PlayerController — Configuration
// ============================================================================

TEST(PlayerControllerTest, ConfigSetters) {
    PlayerController pc;
    pc.setMoveSpeed(20.0f);
    EXPECT_FLOAT_EQ(pc.moveSpeed(), 20.0f);

    pc.setLookSensitivity(0.005f);
    EXPECT_FLOAT_EQ(pc.lookSensitivity(), 0.005f);

    pc.setJumpVelocity(12.0f);
    EXPECT_FLOAT_EQ(pc.jumpVelocity(), 12.0f);

    pc.setEyeHeight(1.8f);
    EXPECT_FLOAT_EQ(pc.eyeHeight(), 1.8f);
}

// ============================================================================
// Key Bindings
// ============================================================================

TEST(KeyBindingsTest, DefaultBindings) {
    auto bindings = getDefaultKeyBindings();
    EXPECT_GE(bindings.size(), 8u);

    // Check forward is W (87)
    bool foundForward = false;
    for (const auto& b : bindings) {
        if (b.action == "forward") {
            EXPECT_EQ(b.keyCode, 87);
            EXPECT_FALSE(b.isMouse);
            foundForward = true;
        }
    }
    EXPECT_TRUE(foundForward);
}

TEST(KeyBindingsTest, DefaultBindingsMouse) {
    auto bindings = getDefaultKeyBindings();
    bool foundBreak = false;
    for (const auto& b : bindings) {
        if (b.action == "break") {
            EXPECT_EQ(b.keyCode, 0);  // GLFW_MOUSE_BUTTON_LEFT
            EXPECT_TRUE(b.isMouse);
            foundBreak = true;
        }
    }
    EXPECT_TRUE(foundBreak);
}

TEST(KeyBindingsTest, BindingConfigKey) {
    EXPECT_EQ(bindingConfigKey("forward"), "input.bind.forward");
    EXPECT_EQ(bindingConfigKey("break"), "input.bind.break");
}

TEST(KeyBindingsTest, LoadDefaultsWhenNoConfig) {
    // ConfigManager not initialized -> should return defaults
    ConfigManager::instance().reset();
    auto bindings = loadKeyBindings();
    auto defaults = getDefaultKeyBindings();
    EXPECT_EQ(bindings.size(), defaults.size());
    for (size_t i = 0; i < bindings.size(); ++i) {
        EXPECT_EQ(bindings[i].action, defaults[i].action);
        EXPECT_EQ(bindings[i].keyCode, defaults[i].keyCode);
    }
}

// ============================================================================
// PlayerController — jumping, sneaking and flight over real blocks
// ============================================================================

namespace {

// Solid cubes at the given cells, the top half of a cell at others, nothing elsewhere
struct BlockWorld {
    std::vector<BlockCoord> cells;
    std::vector<BlockCoord> upperHalves;
    BlockShapeProvider shapes() const {
        static const CollisionShape upper = [] {
            CollisionShape shape;
            shape.addBox(AABB(0.0f, 0.5f, 0.0f, 1.0f, 1.0f, 1.0f));
            return shape;
        }();
        return [this](const BlockCoord& pos, RaycastMode) -> const CollisionShape* {
            for (const auto& c : cells) {
                if (c.x == pos.x && c.y == pos.y && c.z == pos.z) return &CollisionShape::FULL_BLOCK;
            }
            for (const auto& c : upperHalves) {
                if (c.x == pos.x && c.y == pos.y && c.z == pos.z) return &upper;
            }
            return nullptr;
        };
    }
};

BlockWorld floorWorld() {
    BlockWorld w;
    for (int x = -4; x <= 4; ++x) {
        for (int z = -4; z <= 4; ++z) w.cells.emplace_back(x, -1, z);
    }
    return w;
}

constexpr float FRAME = 1.0f / 60.0f;

// Frames run, counting take-offs: the body leaving the ground going up
int runCountingJumps(PlayerController& pc, SimplePhysicsBody& body, int frames) {
    int jumps = 0;
    for (int i = 0; i < frames; ++i) {
        bool wasOnGround = body.isOnGround();
        pc.update(FRAME);
        if (wasOnGround && body.velocity().y > 1.0f) ++jumps;
    }
    return jumps;
}

}  // namespace

TEST(PlayerControllerTest, JumpPressedInTheAirIsForgotten) {
    BlockWorld world = floorWorld();
    PhysicsSystem physics(world.shapes());
    const Vec3 start(0.5f, 2.0f, 0.5f);
    SimplePhysicsBody body(start, Vec3(0.35f, 0.95f, 0.35f));
    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);  // which moves the body to the fly position: put it back
    body.setPosition(start);

    pc.requestJump();  // falling: too late to jump, and not to be kept for the landing
    EXPECT_EQ(runCountingJumps(pc, body, 180), 0);
    EXPECT_TRUE(body.isOnGround());
}

TEST(PlayerControllerTest, ATapJumpsOnceAndAHoldKeepsJumping) {
    BlockWorld world = floorWorld();
    PhysicsSystem physics(world.shapes());
    const Vec3 start(0.5f, 0.0f, 0.5f);
    SimplePhysicsBody body(start, Vec3(0.35f, 0.95f, 0.35f));
    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);  // which moves the body to the fly position: put it back
    body.setPosition(start);
    runCountingJumps(pc, body, 10);  // settle onto the floor
    ASSERT_TRUE(body.isOnGround());

    // A tap: the key down for a few frames, as a quick press is
    pc.requestJump();
    pc.setJumpHeld(true);
    int jumps = runCountingJumps(pc, body, 4);
    pc.setJumpHeld(false);
    jumps += runCountingJumps(pc, body, 240);
    EXPECT_EQ(jumps, 1);

    // Held: up again on each landing
    pc.requestJump();
    pc.setJumpHeld(true);
    EXPECT_GE(runCountingJumps(pc, body, 240), 2);
}

TEST(PlayerControllerTest, SneakingNeverWalksOffAPillar) {
    BlockWorld world;
    world.cells.emplace_back(0, 0, 0);  // one block to stand on, its top at y = 1
    PhysicsSystem physics(world.shapes());
    for (int heading = 0; heading < 8; ++heading) {
        for (bool sneak : {true, false}) {
            SimplePhysicsBody body(Vec3(0.5f, 1.0f, 0.5f), Vec3(0.35f, 0.95f, 0.35f));
            body.setOnGround(true);
            PlayerController pc;
            pc.setPhysics(&body, &physics);
            pc.setFlyMode(false);  // which moves the body to the fly position: put it back
            body.setPosition(Vec3(0.5f, 1.0f + COLLISION_MARGIN, 0.5f));
            pc.setYaw(heading * glm::quarter_pi<float>());
            pc.setSneak(sneak);
            pc.setMoveForward(true);
            for (int i = 0; i < 120; ++i) pc.update(FRAME);
            Vec3 at = body.position();
            if (sneak) {
                EXPECT_NEAR(at.y, 1.0f, 0.01f) << "heading " << heading;
                // Out to the edge, but some of the body still over the block
                EXPECT_LT(at.x, 1.35f) << "heading " << heading;
                EXPECT_GT(at.x, -0.35f) << "heading " << heading;
                EXPECT_LT(at.z, 1.35f) << "heading " << heading;
                EXPECT_GT(at.z, -0.35f) << "heading " << heading;
                EXPECT_GT(std::max(std::abs(at.x - 0.5f), std::abs(at.z - 0.5f)), 0.6f)
                    << "heading " << heading << ": should reach the edge";
            } else {
                EXPECT_LT(at.y, 0.5f) << "heading " << heading << ": walking on falls off";
            }
        }
    }
}

TEST(PlayerControllerTest, DoubleTapStartsFlightOnlyWhereAllowedAndQuickEnough) {
    BlockWorld world = floorWorld();
    PhysicsSystem physics(world.shapes());
    auto tryDoubleTap = [&](bool allowed, int framesBetween) {
        SimplePhysicsBody body(Vec3(0.5f, 0.0f, 0.5f), Vec3(0.35f, 0.95f, 0.35f));
        PlayerController pc;
        pc.setPhysics(&body, &physics);
        pc.setFlyMode(false);  // which moves the body to the fly position: put it back
        body.setPosition(Vec3(0.5f, 0.0f, 0.5f));
        pc.setFlightAllowed(allowed);
        runCountingJumps(pc, body, 10);
        pc.requestJump();
        for (int i = 0; i < framesBetween; ++i) pc.update(FRAME);
        pc.requestJump();
        pc.update(FRAME);
        return pc.flying();
    };
    EXPECT_TRUE(tryDoubleTap(true, 6));
    EXPECT_FALSE(tryDoubleTap(false, 6));
    EXPECT_FALSE(tryDoubleTap(true, 40));  // two separate jumps
}

TEST(PlayerControllerTest, FlightHoversClimbsAndEndsOnLanding) {
    BlockWorld world = floorWorld();
    PhysicsSystem physics(world.shapes());
    const Vec3 start(0.5f, 3.0f, 0.5f);
    SimplePhysicsBody body(start, Vec3(0.35f, 0.95f, 0.35f));
    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);  // which moves the body to the fly position: put it back
    body.setPosition(start);
    pc.setFlightAllowed(true);
    pc.setFlying(true);
    pc.setFlyClimb(5.0f);

    for (int i = 0; i < 60; ++i) pc.update(FRAME);
    EXPECT_NEAR(body.position().y, 3.0f, 0.01f) << "hovers with no input";

    pc.setJumpHeld(true);
    for (int i = 0; i < 60; ++i) pc.update(FRAME);
    pc.setJumpHeld(false);
    EXPECT_NEAR(body.position().y, 8.0f, 0.1f) << "climbs at fly_climb";

    pc.setSneak(true);
    for (int i = 0; i < 180 && pc.flying(); ++i) pc.update(FRAME);
    EXPECT_FALSE(pc.flying()) << "landing ends flight";
    EXPECT_NEAR(body.position().y, 0.0f, 0.01f);

    pc.setFlying(true);
    pc.setFlightAllowed(false);
    EXPECT_FALSE(pc.flying()) << "taking flight away lands the body";
}

TEST(PlayerControllerTest, CrouchingFitsUnderALowCeilingAndStandsOnlyWithRoom) {
    // A floor, and a ceiling 1.5 above it over x = 2..3
    BlockWorld world = floorWorld();
    for (int x = 2; x <= 3; ++x) world.upperHalves.emplace_back(x, 1, 0);
    PhysicsSystem physics(world.shapes());
    // Resting a margin above the floor, as bodies do: a gap of exactly its height still fits
    const Vec3 start(0.5f, COLLISION_MARGIN, 0.5f);
    SimplePhysicsBody body(start, Vec3(0.35f, 0.95f, 0.35f));
    PlayerController pc;
    pc.setPhysics(&body, &physics);
    pc.setFlyMode(false);  // which moves the body to the fly position: put it back
    body.setPosition(start);
    pc.setCrouchShrink(0.4f);
    pc.setYaw(glm::half_pi<float>());  // facing +X

    // Standing, the ceiling stops it
    pc.setMoveForward(true);
    for (int i = 0; i < 90; ++i) pc.update(FRAME);
    EXPECT_LT(body.position().x, 2.0f);

    // Crouched it goes under
    pc.setSneak(true);
    for (int i = 0; i < 120 && body.position().x < 2.8f; ++i) pc.update(FRAME);
    EXPECT_TRUE(pc.crouched());
    EXPECT_NEAR(body.halfExtents().y * 2.0f, 1.5f, 1e-5f);
    EXPECT_GT(body.position().x, 2.4f);
    pc.setMoveForward(false);

    // Letting go under the ceiling stays crouched; out from under it, stands
    pc.setSneak(false);
    for (int i = 0; i < 10; ++i) pc.update(FRAME);
    EXPECT_TRUE(pc.crouched()) << "no room to stand";
    pc.setYaw(-glm::half_pi<float>());
    pc.setMoveForward(true);
    for (int i = 0; i < 120 && pc.crouched(); ++i) pc.update(FRAME);
    EXPECT_FALSE(pc.crouched());
    EXPECT_NEAR(body.halfExtents().y * 2.0f, 1.9f, 1e-5f);
    EXPECT_LT(body.position().x, 2.0f + 0.35f + 0.01f);
}
