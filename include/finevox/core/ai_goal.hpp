#pragma once

/**
 * @file ai_goal.hpp
 * @brief AIGoal — abstract base for mob behavior goals
 *
 * Goals are composable units of behavior. An AIBrain selects the
 * highest-priority eligible goal each tick.
 */

#include <cstdint>

namespace finevox {

class MobEntity;

class AIGoal {
public:
    virtual ~AIGoal() = default;

    /// Check if this goal can start given the mob's current state
    virtual bool canStart(MobEntity& mob) = 0;

    /// Called when this goal becomes the active goal
    virtual void start(MobEntity& mob) = 0;

    /// Update logic while this goal is active
    virtual void tick(MobEntity& mob, float dt) = 0;

    /// Check if this goal is done (brain will switch to next)
    virtual bool isComplete(MobEntity& mob) = 0;

    /// Called when this goal is interrupted or completed
    virtual void stop(MobEntity& mob) = 0;

    /// Priority of this goal (higher = more urgent)
    virtual int priority() const = 0;
};

}  // namespace finevox
