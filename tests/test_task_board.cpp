#include <gtest/gtest.h>
#include "finevox/core/task_board.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <random>

using namespace finevox;

namespace {

constexpr TaskBoard::Resource WOOD = 1;
constexpr TaskBoard::Resource STONE = 2;

TaskBoard::Task task(TaskBoard::Key key, double x, double z, TaskBoard::Resource need = 0, double priority = 0.0) {
    TaskBoard::Task t;
    t.key = key;
    t.pos = glm::dvec3(x, 0.0, z);
    t.need = need;
    t.priority = priority;
    return t;
}

TaskBoard::Agent agent(EntityId id, double x, double z, TaskBoard::Resource holds = 0, bool fetches = false) {
    TaskBoard::Agent a;
    a.id = id;
    a.pos = glm::dvec3(x, 0.0, z);
    a.holds = holds;
    a.fetches = fetches;
    return a;
}

}  // namespace

TEST(TaskBoard, TheAgentStandingThereGetsItNotTheOneFarOff) {
    TaskBoard board;
    board.setTask(task(1, 0, 0, WOOD));
    board.setAgent(agent(10, 40, 0, WOOD));
    board.setAgent(agent(11, 1, 0, WOOD));
    board.solve();
    EXPECT_EQ(board.assignment(11), std::optional<TaskBoard::Key>(1));
    EXPECT_FALSE(board.assignment(10).has_value());
}

TEST(TaskBoard, AJobTakenEarlyGoesToWhoeverIsNearerAtTheNextSolve) {
    TaskBoard board;
    board.setTask(task(1, 0, 0, WOOD));
    board.setAgent(agent(10, 40, 0, WOOD));
    EXPECT_EQ(board.take(10), std::optional<TaskBoard::Key>(1));
    board.setAgent(agent(11, 1, 0, WOOD));
    board.solve();
    EXPECT_EQ(board.assignment(11), std::optional<TaskBoard::Key>(1));
    EXPECT_FALSE(board.assignment(10).has_value());
}

TEST(TaskBoard, SolvesForTheTotalNotForWhoeverAsksFirst) {
    // Agent 10 alone would take the task beside it, leaving 11 the long way round
    TaskBoard board;
    board.setTask(task(1, 1, 0));
    board.setTask(task(2, -10, 0));
    board.setAgent(agent(10, 0, 0));
    board.setAgent(agent(11, 10, 0));
    board.solve();
    EXPECT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(2));
    EXPECT_EQ(board.assignment(11), std::optional<TaskBoard::Key>(1));
}

TEST(TaskBoard, FetchingCostsTheTripByWayOfTheSource) {
    TaskBoard board;
    board.setSources(WOOD, {glm::dvec3(0, 0, 30)});
    auto t = task(1, 0, 0, WOOD);
    auto fetcher = agent(10, 0, 10, 0, true);
    auto c = board.cost(fetcher, t);
    ASSERT_TRUE(c.has_value());
    EXPECT_DOUBLE_EQ(*c, 20.0 + 30.0);

    EXPECT_FALSE(board.cost(agent(11, 0, 10, 0, false), t).has_value()) << "may not fetch";
    EXPECT_FALSE(board.cost(agent(12, 0, 10, STONE, true), t).has_value()) << "has stone in hand";
    EXPECT_FALSE(board.cost(fetcher, task(2, 0, 0, STONE)).has_value()) << "no stone anywhere";
    EXPECT_TRUE(board.cost(agent(13, 0, 10, STONE), task(3, 0, 0)).has_value()) << "needs nothing";
    auto carrier = agent(14, 0, 10, STONE);
    carrier.onlyHolds = true;
    EXPECT_FALSE(board.cost(carrier, task(3, 0, 0)).has_value()) << "a carrier only delivers";
    EXPECT_TRUE(board.cost(carrier, task(4, 0, 0, STONE)).has_value());
    carrier.holds = 0;
    EXPECT_TRUE(board.cost(carrier, task(3, 0, 0)).has_value()) << "empty-handed, it can do anything";
}

TEST(TaskBoard, AHolderBeatsAFetcherWhoIsNearerTheTask) {
    TaskBoard board;
    board.setSources(WOOD, {glm::dvec3(0, 0, 50)});
    board.setTask(task(1, 0, 0, WOOD));
    board.setAgent(agent(10, 2, 0, 0, true));
    board.setAgent(agent(11, 12, 0, WOOD));
    board.solve();
    EXPECT_EQ(board.assignment(11), std::optional<TaskBoard::Key>(1));
}

TEST(TaskBoard, ClimbingCostsMoreThanWalking) {
    TaskBoard board;
    board.setVerticalWeight(2.0);
    auto up = task(1, 0, 0);
    up.pos.y = 5.0;
    EXPECT_DOUBLE_EQ(*board.cost(agent(10, 0, 0), up), 10.0);
}

TEST(TaskBoard, PriorityIsWorthItsWeightInBlocks) {
    TaskBoard board;
    board.setTask(task(1, 5, 0, 0, 0.0));
    board.setTask(task(2, 12, 0, 0, 10.0));
    board.setAgent(agent(10, 0, 0));
    board.solve();
    EXPECT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(2));
}

TEST(TaskBoard, ACommittedAgentKeepsItsTask) {
    TaskBoard board;
    board.setTask(task(1, 0, 0));
    auto busy = agent(10, 3, 0);
    board.setAgent(busy);
    ASSERT_TRUE(board.take(10).has_value());
    busy.committed = true;
    board.setAgent(busy);
    board.setAgent(agent(11, 0, 0));
    board.solve();
    EXPECT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(1));
    EXPECT_FALSE(board.assignment(11).has_value());
}

TEST(TaskBoard, ItTakesARealGainToMoveAnAgent) {
    TaskBoard board;
    board.setStick(3.0);
    board.setTask(task(1, 10, 0));
    board.setAgent(agent(10, 0, 0));
    board.solve();
    ASSERT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(1));

    board.setTask(task(2, 8, 0));
    board.solve();
    EXPECT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(1)) << "2 blocks isn't worth a change";

    board.setTask(task(3, 4, 0));
    board.solve();
    EXPECT_EQ(board.assignment(10), std::optional<TaskBoard::Key>(3));
}

TEST(TaskBoard, SpareAgentsAndImpossibleTasksAreLeftIdle) {
    TaskBoard board;
    board.setTask(task(1, 0, 0));
    board.setTask(task(2, 5, 0, STONE));
    board.setAgent(agent(10, 1, 0));
    board.setAgent(agent(11, 2, 0));
    auto stats = board.solve();
    EXPECT_EQ(stats.assigned, 1u);
    EXPECT_EQ(stats.agents, 2u);
    EXPECT_EQ(stats.tasks, 2u);
}

TEST(TaskBoard, TakeGivesTheBestTaskNobodyHas) {
    TaskBoard board;
    board.setTask(task(1, 1, 0));
    board.setTask(task(2, 3, 0));
    board.setAgent(agent(10, 0, 0));
    board.setAgent(agent(11, 0, 0));
    EXPECT_EQ(board.take(10), std::optional<TaskBoard::Key>(1));
    EXPECT_EQ(board.take(10), std::optional<TaskBoard::Key>(1)) << "already has one";
    EXPECT_EQ(board.take(11), std::optional<TaskBoard::Key>(2));
    board.setAgent(agent(12, 0, 0));
    EXPECT_FALSE(board.take(12).has_value());
}

TEST(TaskBoard, ADoneTaskFreesItsAgent) {
    TaskBoard board;
    board.setTask(task(1, 1, 0));
    board.setAgent(agent(10, 0, 0));
    ASSERT_TRUE(board.take(10).has_value());
    board.removeTask(1);
    EXPECT_FALSE(board.assignment(10).has_value());
    board.setTask(task(2, 1, 0));
    EXPECT_EQ(board.take(10), std::optional<TaskBoard::Key>(2));
}

// The invariant: no other assignment costs less. Checked against every assignment there is, on
// random boards, with costs worked out here rather than by the board.
TEST(TaskBoard, NoAssignmentCostsLessThanTheOneItFinds) {
    std::mt19937 rng(20261111);
    std::uniform_real_distribution<double> coord(-30.0, 30.0);
    std::uniform_real_distribution<double> prio(0.0, 15.0);
    for (int trial = 0; trial < 400; ++trial) {
        int n = 1 + static_cast<int>(rng() % 5);
        int m = 1 + static_cast<int>(rng() % 6);
        TaskBoard board;
        board.setStick(0.0);
        std::vector<glm::dvec2> agents, tasks;
        std::vector<double> priorities;
        for (int i = 0; i < n; ++i) {
            agents.emplace_back(coord(rng), coord(rng));
            board.setAgent(agent(100 + i, agents.back().x, agents.back().y));
        }
        for (int j = 0; j < m; ++j) {
            tasks.emplace_back(coord(rng), coord(rng));
            priorities.push_back(prio(rng));
            board.setTask(task(1 + j, tasks.back().x, tasks.back().y, 0, priorities.back()));
        }
        auto pairCost = [&](int i, int j) {
            return std::hypot(agents[i].x - tasks[j].x, agents[i].y - tasks[j].y) - priorities[j];
        };

        board.solve();
        double got = 0.0;
        int busy = 0;
        for (int i = 0; i < n; ++i) {
            auto key = board.assignment(100 + i);
            if (!key) continue;
            got += pairCost(i, static_cast<int>(*key) - 1);
            ++busy;
        }
        // Everyone works while there's work
        ASSERT_EQ(busy, std::min(n, m)) << "trial " << trial;

        double best = std::numeric_limits<double>::max();
        std::vector<int> pick(n, -1);
        std::vector<bool> taken(m, false);
        std::function<void(int, double, int)> search = [&](int i, double sum, int working) {
            if (i == n) {
                if (working == std::min(n, m)) best = std::min(best, sum);
                return;
            }
            search(i + 1, sum, working);
            for (int j = 0; j < m; ++j) {
                if (taken[j]) continue;
                taken[j] = true;
                search(i + 1, sum + pairCost(i, j), working + 1);
                taken[j] = false;
            }
        };
        search(0, 0.0, 0);
        ASSERT_NEAR(got, best, 1e-6) << "trial " << trial << " with " << n << " agents, " << m << " tasks";
    }
}

TEST(TaskBoard, AHundredAgentsOnThreeHundredTasksSolveQuickly) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> coord(-100.0, 100.0);
    TaskBoard board;
    for (int i = 0; i < 100; ++i) board.setAgent(agent(1000 + i, coord(rng), coord(rng)));
    for (int j = 0; j < 300; ++j) board.setTask(task(1 + j, coord(rng), coord(rng)));
    auto stats = board.solve();
    EXPECT_EQ(stats.assigned, 100u);
    RecordProperty("micros", std::to_string(stats.micros));
    std::cout << "100 agents x 300 tasks: " << stats.micros << " us\n";
}
