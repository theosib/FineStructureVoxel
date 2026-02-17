#pragma once

/**
 * @file ai_brain.hpp
 * @brief AIBrain — priority-based goal selector for mob AI
 *
 * Owns a list of goals sorted by priority. Each tick, selects the
 * highest-priority goal that can start, or continues the active goal
 * if nothing higher priority is available.
 */

#include "finevox/core/ai_goal.hpp"
#include <memory>
#include <vector>

namespace finevox {

class MobEntity;

class AIBrain {
public:
    AIBrain() = default;
    ~AIBrain() = default;

    // Movable
    AIBrain(AIBrain&&) = default;
    AIBrain& operator=(AIBrain&&) = default;

    /// Add a goal at the given priority (higher = more urgent)
    void addGoal(int priority, std::unique_ptr<AIGoal> goal);

    /// Tick the brain: select and run the best goal
    void tick(MobEntity& mob, float dt);

    /// Get the currently active goal (may be null)
    [[nodiscard]] AIGoal* activeGoal() const { return active_; }

    /// Get number of registered goals
    [[nodiscard]] size_t goalCount() const { return goals_.size(); }

    /// Clear all goals
    void clear();

private:
    struct GoalEntry {
        int priority;
        std::unique_ptr<AIGoal> goal;
    };

    std::vector<GoalEntry> goals_;  // Sorted by priority descending
    AIGoal* active_ = nullptr;
};

}  // namespace finevox
