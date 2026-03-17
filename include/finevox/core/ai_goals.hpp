#pragma once

/**
 * @file ai_goals.hpp
 * @brief Built-in AIGoal implementations with configurable parameters
 *
 * Each goal accepts a params struct with defaults matching the original
 * hardcoded values. Scripts can override via mob_add_goal native function.
 */

#include "finevox/core/ai_goal.hpp"
#include "finevox/core/entity_state.hpp"
#include <glm/glm.hpp>
#include <optional>
#include <vector>

namespace finevox {

struct PathNode;

// ============================================================================
// Goal Parameter Structs (defaults match original hardcoded values)
// ============================================================================

struct IdleGoalParams {
    float minDuration = 2.0f;
    float maxDuration = 5.0f;
    int animSlot = 0;
};

struct WanderGoalParams {
    float range = 10.0f;
    float maxTime = 10.0f;
    float startChance = 0.1f;
    int animSlot = 1;
};

struct ChaseGoalParams {
    float maxRange = 16.0f;
    float repathInterval = 1.0f;
    float damageMemory = 10.0f;  // Neutral mobs forget after this many seconds
    int animSlot = 1;
};

struct AttackGoalParams {
    int animSlot = 2;
    float rangeHysteresis = 1.5f;  // Multiplier for exit range
};

struct FleeGoalParams {
    float distance = 16.0f;
    float duration = 5.0f;
    float speedMult = 1.5f;
    float damageMemory = 2.0f;  // Trigger when damaged within this many seconds
    int animSlot = 1;
};

struct LookAtPlayerGoalParams {
    float range = 8.0f;
    float duration = 3.0f;
};

struct PanicGoalParams {
    float duration = 5.0f;
    float speedMult = 1.5f;
    float wanderRange = 12.0f;
    float damageMemory = 1.0f;
    int animSlot = 1;
};

// ============================================================================
// IdleGoal — stand still, play idle animation
// ============================================================================

class IdleGoal : public AIGoal {
public:
    explicit IdleGoal(int prio = 0, IdleGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    IdleGoalParams params_;
    float idleTimer_ = 0.0f;
    float idleDuration_ = 3.0f;
};

// ============================================================================
// WanderGoal — pick random nearby position, walk to it
// ============================================================================

class WanderGoal : public AIGoal {
public:
    explicit WanderGoal(int prio = 1, WanderGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    WanderGoalParams params_;
    glm::dvec3 target_{0.0};
    float timeout_ = 0.0f;
};

// ============================================================================
// ChaseGoal — pathfind toward a target entity
// ============================================================================

class ChaseGoal : public AIGoal {
public:
    explicit ChaseGoal(int prio = 5, ChaseGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    ChaseGoalParams params_;
    EntityId targetId_ = INVALID_ENTITY_ID;
    float repathTimer_ = 0.0f;
};

// ============================================================================
// AttackGoal — when in range, deal damage
// ============================================================================

class AttackGoal : public AIGoal {
public:
    explicit AttackGoal(int prio = 6, AttackGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    AttackGoalParams params_;
    EntityId targetId_ = INVALID_ENTITY_ID;
    float cooldownTimer_ = 0.0f;
};

// ============================================================================
// FleeGoal — run away from threat
// ============================================================================

class FleeGoal : public AIGoal {
public:
    explicit FleeGoal(int prio = 7, FleeGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    FleeGoalParams params_;
    float fleeTimer_ = 0.0f;
};

// ============================================================================
// LookAtPlayerGoal — turn to face nearest player
// ============================================================================

class LookAtPlayerGoal : public AIGoal {
public:
    explicit LookAtPlayerGoal(int prio = 2, LookAtPlayerGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    LookAtPlayerGoalParams params_;
    float lookTimer_ = 0.0f;
};

// ============================================================================
// PanicGoal — run randomly after taking damage
// ============================================================================

class PanicGoal : public AIGoal {
public:
    explicit PanicGoal(int prio = 8, PanicGoalParams params = {})
        : priority_(prio), params_(params) {}

    bool canStart(MobEntity& mob) override;
    void start(MobEntity& mob) override;
    void tick(MobEntity& mob, float dt) override;
    bool isComplete(MobEntity& mob) override;
    void stop(MobEntity& mob) override;
    int priority() const override { return priority_; }

private:
    int priority_;
    PanicGoalParams params_;
    float panicTimer_ = 0.0f;
};

}  // namespace finevox
