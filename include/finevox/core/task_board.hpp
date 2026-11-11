#pragma once

/**
 * @file task_board.hpp
 * @brief Who does which job: an optimal assignment of agents to tasks
 *
 * Agents that each pick their own work grab whatever is nearest when they ask, and keep it however
 * things change: an agent far off holds a job that one standing beside it could do now. A board
 * sees every agent and every task at once, and gives each agent the job that makes the total cost
 * least.
 *
 * Cost is travel, in blocks, divided by the agent's speed. A task can need a resource. An agent
 * holding it walks straight there; one that may fetch walks to the best source first; any other
 * agent can't take that task. Priority is in the same units: a task with priority 10 is worth
 * walking 10 blocks further for.
 *
 * Two things keep it from thrashing. A committed agent (already at work) keeps its task, out of
 * the problem. And the task an agent already has costs it `stick` less, so a new solve only moves
 * it for a real gain.
 *
 * The solve is the Hungarian method, exact, O(n^2 m) for n agents and m tasks: under a
 * millisecond for dozens of agents and hundreds of tasks. Between solves, take() gives a free agent
 * the best task nobody has.
 *
 * See docs/systems/task-board.md. Game thread only.
 */

#include "finevox/core/entity_state.hpp"

#include <glm/vec3.hpp>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace finevox {

class TaskBoard {
public:
    using Key = uint64_t;
    /// What a task needs and an agent holds. 0 is nothing.
    using Resource = uint32_t;

    struct Task {
        Key key = 0;
        glm::dvec3 pos{0.0};
        Resource need = 0;
        double priority = 0.0;
        /// The caller's, carried along: which site, which block
        int64_t group = 0;
        uint32_t what = 0;
    };

    struct Agent {
        EntityId id = INVALID_ENTITY_ID;
        glm::dvec3 pos{0.0};
        Resource holds = 0;
        /// May go to a source for what a task needs
        bool fetches = false;
        /// Holding something, it takes only tasks that use it: a carrier, not a worker
        bool onlyHolds = false;
        /// At work on its task: keeps it, and nobody else is offered it
        bool committed = false;
        double speed = 1.0;
    };

    struct Stats {
        size_t agents = 0;
        size_t tasks = 0;
        size_t assigned = 0;
        double micros = 0.0;
    };

    /// Add or replace a task. An agent that had it keeps it.
    void setTask(const Task& task);
    /// Remove a task, and whoever had it has none
    void removeTask(Key key);
    [[nodiscard]] const Task* task(Key key) const;
    [[nodiscard]] const std::unordered_map<Key, Task>& tasks() const { return tasks_; }

    /// Add or update an agent. Its assignment stays.
    void setAgent(const Agent& agent);
    void removeAgent(EntityId id);
    [[nodiscard]] const Agent* agent(EntityId id) const;
    [[nodiscard]] const std::unordered_map<EntityId, Agent>& agents() const { return agents_; }

    /// Where a resource can be fetched from, replacing what was there
    void setSources(Resource kind, std::vector<glm::dvec3> where);

    /// What staying on the same task is worth, in blocks
    void setStick(double blocks) { stick_ = blocks; }
    /// A block of climb costs this many of walking
    void setVerticalWeight(double weight) { verticalWeight_ = weight; }

    /// Assign every free agent at once, at least total cost
    Stats solve();

    /// The task this agent has, if any
    [[nodiscard]] std::optional<Key> assignment(EntityId id) const;

    /// The agent's task, or if it has none, the best one nobody has, which it now has
    std::optional<Key> take(EntityId id);

    /// The agent has no task now (it finished, or gave up)
    void release(EntityId id);

    /// The cost of this agent doing this task, or nullopt if it can't
    [[nodiscard]] std::optional<double> cost(const Agent& agent, const Task& task) const;

private:
    [[nodiscard]] double travel(const glm::dvec3& a, const glm::dvec3& b) const;
    void assign(EntityId id, Key key);

    std::unordered_map<Key, Task> tasks_;
    std::unordered_map<EntityId, Agent> agents_;
    std::unordered_map<Resource, std::vector<glm::dvec3>> sources_;
    std::unordered_map<EntityId, Key> assigned_;
    std::unordered_map<Key, EntityId> holder_;
    double stick_ = 3.0;
    double verticalWeight_ = 2.0;
};

}  // namespace finevox
