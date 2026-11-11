#include "finevox/core/task_board.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

namespace finevox {

namespace {

// Costs the solver must never prefer. An agent left with nothing costs IDLE, and one given a
// task it can't do costs more, so it would always rather be idle.
constexpr double IDLE = 1e7;
constexpr double CANT = 1e9;

// Least-cost assignment of n rows to m >= n columns, by the Hungarian method with potentials.
// a is 1-based, (n + 1) x (m + 1). Returns the column of each row, 1-based, index 0 unused.
std::vector<int> hungarian(const std::vector<std::vector<double>>& a, int n, int m) {
    const double inf = std::numeric_limits<double>::max() / 4;
    std::vector<double> u(n + 1, 0.0), v(m + 1, 0.0);
    std::vector<int> p(m + 1, 0), way(m + 1, 0);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(m + 1, inf);
        std::vector<bool> used(m + 1, false);
        do {
            used[j0] = true;
            int i0 = p[j0];
            int j1 = 0;
            double delta = inf;
            for (int j = 1; j <= m; ++j) {
                if (used[j]) continue;
                double cur = a[i0][j] - u[i0] - v[j];
                if (cur < minv[j]) {
                    minv[j] = cur;
                    way[j] = j0;
                }
                if (minv[j] < delta) {
                    delta = minv[j];
                    j1 = j;
                }
            }
            for (int j = 0; j <= m; ++j) {
                if (used[j]) {
                    u[p[j]] += delta;
                    v[j] -= delta;
                } else {
                    minv[j] -= delta;
                }
            }
            j0 = j1;
        } while (p[j0] != 0);
        do {
            int j1 = way[j0];
            p[j0] = p[j1];
            j0 = j1;
        } while (j0 != 0);
    }
    std::vector<int> columnOf(n + 1, 0);
    for (int j = 1; j <= m; ++j) {
        if (p[j] != 0) columnOf[p[j]] = j;
    }
    return columnOf;
}

}  // namespace

void TaskBoard::setTask(const Task& task) { tasks_[task.key] = task; }

void TaskBoard::removeTask(Key key) {
    tasks_.erase(key);
    auto it = holder_.find(key);
    if (it != holder_.end()) {
        assigned_.erase(it->second);
        holder_.erase(it);
    }
}

const TaskBoard::Task* TaskBoard::task(Key key) const {
    auto it = tasks_.find(key);
    return it == tasks_.end() ? nullptr : &it->second;
}

void TaskBoard::setAgent(const Agent& agent) { agents_[agent.id] = agent; }

void TaskBoard::removeAgent(EntityId id) {
    release(id);
    agents_.erase(id);
}

const TaskBoard::Agent* TaskBoard::agent(EntityId id) const {
    auto it = agents_.find(id);
    return it == agents_.end() ? nullptr : &it->second;
}

void TaskBoard::setSources(Resource kind, std::vector<glm::dvec3> where) {
    if (where.empty()) {
        sources_.erase(kind);
    } else {
        sources_[kind] = std::move(where);
    }
}

double TaskBoard::travel(const glm::dvec3& a, const glm::dvec3& b) const {
    double dx = a.x - b.x;
    double dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz) + verticalWeight_ * std::abs(a.y - b.y);
}

std::optional<double> TaskBoard::cost(const Agent& agent, const Task& task) const {
    double blocks = 0.0;
    if (agent.onlyHolds && agent.holds != 0 && task.need != agent.holds) return std::nullopt;
    if (task.need == 0 || agent.holds == task.need) {
        blocks = travel(agent.pos, task.pos);
    } else if (agent.holds == 0 && agent.fetches) {
        auto it = sources_.find(task.need);
        if (it == sources_.end()) return std::nullopt;
        double best = std::numeric_limits<double>::max();
        for (const auto& source : it->second) {
            best = std::min(best, travel(agent.pos, source) + travel(source, task.pos));
        }
        blocks = best;
    } else {
        // Holding something else: that has to go first
        return std::nullopt;
    }
    double speed = agent.speed > 0.0 ? agent.speed : 1.0;
    return blocks / speed - task.priority;
}

void TaskBoard::assign(EntityId id, Key key) {
    release(id);
    auto held = holder_.find(key);
    if (held != holder_.end()) assigned_.erase(held->second);
    assigned_[id] = key;
    holder_[key] = id;
}

void TaskBoard::release(EntityId id) {
    auto it = assigned_.find(id);
    if (it == assigned_.end()) return;
    holder_.erase(it->second);
    assigned_.erase(it);
}

std::optional<TaskBoard::Key> TaskBoard::assignment(EntityId id) const {
    auto it = assigned_.find(id);
    if (it == assigned_.end()) return std::nullopt;
    return it->second;
}

std::optional<TaskBoard::Key> TaskBoard::take(EntityId id) {
    if (auto have = assignment(id); have && tasks_.count(*have)) return have;
    release(id);
    auto found = agents_.find(id);
    if (found == agents_.end()) return std::nullopt;
    const Agent& agent = found->second;
    std::optional<Key> best;
    double bestCost = std::numeric_limits<double>::max();
    for (const auto& [key, task] : tasks_) {
        if (holder_.count(key)) continue;
        auto c = cost(agent, task);
        if (c && *c < bestCost) {
            bestCost = *c;
            best = key;
        }
    }
    if (best) assign(id, *best);
    return best;
}

TaskBoard::Stats TaskBoard::solve() {
    auto started = std::chrono::steady_clock::now();
    Stats stats;

    // Drop assignments to tasks that went. A committed agent keeps its task, and that task is
    // nobody else's.
    for (auto it = assigned_.begin(); it != assigned_.end();) {
        if (!tasks_.count(it->second) || !agents_.count(it->first)) {
            holder_.erase(it->second);
            it = assigned_.erase(it);
        } else {
            ++it;
        }
    }
    std::vector<const Agent*> free;
    for (const auto& [id, agent] : agents_) {
        if (agent.committed && assigned_.count(id)) continue;
        free.push_back(&agent);
    }
    std::vector<const Task*> open;
    for (const auto& [key, task] : tasks_) {
        auto held = holder_.find(key);
        if (held != holder_.end()) {
            const Agent* by = agent(held->second);
            if (by && by->committed) continue;
        }
        open.push_back(&task);
    }
    // Same order every time, so equal costs settle the same way
    std::sort(free.begin(), free.end(), [](const Agent* a, const Agent* b) { return a->id < b->id; });
    std::sort(open.begin(), open.end(), [](const Task* a, const Task* b) { return a->key < b->key; });
    stats.agents = free.size();
    stats.tasks = open.size();

    if (!free.empty()) {
        // A column per task, then one "nothing" per agent so everyone can be left idle
        int n = static_cast<int>(free.size());
        int m = static_cast<int>(open.size()) + n;
        std::vector<std::vector<double>> a(n + 1, std::vector<double>(m + 1, IDLE));
        for (int i = 0; i < n; ++i) {
            auto mine = assignment(free[i]->id);
            for (size_t j = 0; j < open.size(); ++j) {
                auto c = cost(*free[i], *open[j]);
                double value = c ? *c : CANT;
                if (c && mine && *mine == open[j]->key) value -= stick_;
                a[i + 1][j + 1] = value;
            }
        }
        auto columnOf = hungarian(a, n, m);
        for (int i = 0; i < n; ++i) release(free[i]->id);
        for (int i = 0; i < n; ++i) {
            int j = columnOf[i + 1];
            if (j < 1 || j > static_cast<int>(open.size())) continue;
            if (a[i + 1][j] >= IDLE) continue;
            assign(free[i]->id, open[j - 1]->key);
        }
    }

    stats.assigned = assigned_.size();
    stats.micros = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - started).count();
    return stats;
}

}  // namespace finevox
