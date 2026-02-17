#include "finevox/core/ai_brain.hpp"
#include "finevox/core/mob_entity.hpp"
#include <algorithm>

namespace finevox {

void AIBrain::addGoal(int priority, std::unique_ptr<AIGoal> goal) {
    goals_.push_back({priority, std::move(goal)});
    // Keep sorted by priority descending (highest first)
    std::sort(goals_.begin(), goals_.end(),
        [](const GoalEntry& a, const GoalEntry& b) {
            return a.priority > b.priority;
        });
}

void AIBrain::tick(MobEntity& mob, float dt) {
    // Check if a higher priority goal should take over
    AIGoal* bestGoal = nullptr;
    for (auto& entry : goals_) {
        if (entry.goal->canStart(mob)) {
            bestGoal = entry.goal.get();
            break;  // First match is highest priority
        }
    }

    // If current goal is complete, stop it
    if (active_ && active_->isComplete(mob)) {
        active_->stop(mob);
        active_ = nullptr;
    }

    // Switch to better goal if available
    if (bestGoal && bestGoal != active_) {
        if (active_) {
            active_->stop(mob);
        }
        active_ = bestGoal;
        active_->start(mob);
    }

    // Tick active goal
    if (active_) {
        active_->tick(mob, dt);
    }
}

void AIBrain::clear() {
    if (active_) {
        // Can't call stop without a mob reference, so just null it
        active_ = nullptr;
    }
    goals_.clear();
}

}  // namespace finevox
