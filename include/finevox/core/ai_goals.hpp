#pragma once

/**
 * @file ai_goals.hpp
 * @brief Built-in AIGoal implementations
 */

#include "finevox/core/ai_goal.hpp"
#include "finevox/core/entity_state.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace finevox {

struct PathNode;

// ============================================================================
// IdleGoal — stand still, play idle animation
// ============================================================================

class IdleGoal : public AIGoal {
public:
    explicit IdleGoal(int prio = 0) : priority_(prio) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float idleTimer_ = 0.0f;
    float idleDuration_ = 3.0f;
};

// ============================================================================
// WanderGoal — pick random nearby position, walk to it
// ============================================================================

class WanderGoal : public AIGoal {
public:
    explicit WanderGoal(int prio = 1, float range = 10.0f)
        : priority_(prio), wanderRange_(range) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float wanderRange_;
    glm::dvec3 target_{0.0};
    float timeout_ = 0.0f;
    static constexpr float MAX_WANDER_TIME = 10.0f;
};

// ============================================================================
// ChaseGoal — pathfind toward a target entity
// ============================================================================

class ChaseGoal : public AIGoal {
public:
    explicit ChaseGoal(int prio = 5, float maxRange = 16.0f)
        : priority_(prio), maxRange_(maxRange) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float maxRange_;
    EntityId targetId_ = INVALID_ENTITY_ID;
    float repathTimer_ = 0.0f;
    static constexpr float REPATH_INTERVAL = 1.0f;
};

// ============================================================================
// AttackGoal — when in range, deal damage
// ============================================================================

class AttackGoal : public AIGoal {
public:
    explicit AttackGoal(int prio = 6)
        : priority_(prio) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    EntityId targetId_ = INVALID_ENTITY_ID;
    float cooldownTimer_ = 0.0f;
};

// ============================================================================
// FleeGoal — run away from threat
// ============================================================================

class FleeGoal : public AIGoal {
public:
    explicit FleeGoal(int prio = 7, float fleeDistance = 16.0f)
        : priority_(prio), fleeDistance_(fleeDistance) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float fleeDistance_;
    float fleeTimer_ = 0.0f;
    static constexpr float FLEE_DURATION = 5.0f;
};

// ============================================================================
// LookAtPlayerGoal — turn to face nearest player
// ============================================================================

class LookAtPlayerGoal : public AIGoal {
public:
    explicit LookAtPlayerGoal(int prio = 2, float range = 8.0f)
        : priority_(prio), lookRange_(range) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float lookRange_;
    float lookTimer_ = 0.0f;
    static constexpr float LOOK_DURATION = 3.0f;
};

// ============================================================================
// PanicGoal — run randomly after taking damage
// ============================================================================

class PanicGoal : public AIGoal {
public:
    explicit PanicGoal(int prio = 8)
        : priority_(prio) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    float panicTimer_ = 0.0f;
    static constexpr float PANIC_DURATION = 5.0f;
};

}  // namespace finevox
