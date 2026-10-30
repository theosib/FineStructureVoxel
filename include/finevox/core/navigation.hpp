#pragma once

/**
 * @file navigation.hpp
 * @brief Budgeted pathing and path following for ground agents
 *
 * Pathfinder (pathfinder.hpp) is a small synchronous helper for mob AI.
 * NavigationSystem is the heavier service: scripted agents ask to go somewhere,
 * the search runs under a per-tick node budget, and C++ walks the agent along
 * the result between the agent's (much rarer) script thinks.
 *
 * Paths can be *constructive*. With allowPlace/allowDig the search may route
 * through space that needs a block placed or removed; those steps come back as
 * actions. The system stops the agent at each one and reports Blocked, leaving
 * what to do about it (spend materials, swing a tool, give up) to game policy.
 *
 * All methods run on the game thread.
 */

#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/entity_state.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace finevox {

class World;
class EntityManager;
class MobEntity;

// ============================================================================
// Search
// ============================================================================

struct NavQuery {
    glm::dvec3 start{0.0};
    glm::dvec3 goal{0.0};
    float entityHeight = 1.8f;
    float goalRange = 0.0f;    ///< Accept any standable cell this close (horizontally) to goal
    int maxNodes = 12000;      ///< Expansion cap
    int maxFall = 3;           ///< Largest drop taken without building
    bool allowPlace = false;   ///< May route through cells that need a block placed
    bool allowDig = false;     ///< May route through cells that need a block removed
    float placeCost = 5.0f;
    float digCost = 7.0f;
    /// Fastest walkSpeed() the heuristic should assume. It must stay admissible for
    /// the search to choose a road over the straight line, at the price of more
    /// expansions the faster this is.
    float fastestSurface = 1.5f;
};

enum class NavAction : uint8_t {
    Walk,
    Place,  ///< Put a block at actionPos, then continue
    Dig,    ///< Remove the block at actionPos, then continue
};

struct NavStep {
    BlockCoord pos;           ///< Cell to stand in (feet)
    NavAction action = NavAction::Walk;
    BlockCoord actionPos;     ///< Target of Place/Dig; performed from `pos`
};

struct NavPath {
    std::vector<NavStep> steps;
    int nodesExpanded = 0;
};

/// True if an entity of this height can stand with feet in `pos`
[[nodiscard]] bool isStandable(const World& world, BlockCoord pos, float entityHeight);

/// Search for a path. nullopt when none exists within query.maxNodes.
/// `outNodes`, if given, reports the expansions spent even on failure.
[[nodiscard]] std::optional<NavPath> findNavPath(const World& world, const NavQuery& query,
                                                 int* outNodes = nullptr);

// ============================================================================
// NavigationSystem
// ============================================================================

enum class NavState : uint8_t {
    Idle,     ///< No orders
    Pending,  ///< Waiting for a search to run
    Moving,
    Blocked,  ///< Stopped at a Place/Dig step; see currentAction()
    Arrived,
    Failed,
};

struct NavOrder {
    glm::dvec3 goal{0.0};
    float goalRange = 0.0f;
    bool allowPlace = false;
    bool allowDig = false;
    int maxNodes = 12000;
};

class NavigationSystem : public GameSubsystem {
public:
    NavigationSystem(World& world, EntityManager& entities);

    [[nodiscard]] std::string_view name() const override { return "Navigation"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PostTick; }
    /// Before EntitySubsystem(0) would see stale targets the tick after arrival
    [[nodiscard]] int32_t priority() const override { return -10; }
    void tick(float dt) override;

    /// Send an agent somewhere, replacing any current order
    void order(EntityId id, const NavOrder& order);

    /// Cancel orders and stop the agent
    void stop(EntityId id);

    /// Clear a terminal state back to Idle without moving the agent
    void acknowledge(EntityId id);

    [[nodiscard]] NavState state(EntityId id) const;

    /// The Place/Dig step the agent is stopped at, while state == Blocked
    [[nodiscard]] std::optional<NavStep> currentAction(EntityId id) const;

    /// Continue past the current Blocked step
    void resume(EntityId id);

    /// Search expansions allowed per tick across all agents
    void setNodeBudget(int nodes) { nodeBudget_ = nodes; }

    /// Observability
    [[nodiscard]] size_t activeAgents() const { return agents_.size(); }
    [[nodiscard]] int nodesLastTick() const { return nodesLastTick_; }

private:
    struct Agent {
        NavOrder order;
        NavState state = NavState::Idle;
        std::vector<NavStep> steps;
        size_t next = 0;
        float stuckTime = 0.0f;
        float bestDistance = 1e9f;
        int repaths = 0;
    };

    void runSearches();
    void followPath(EntityId id, Agent& agent, MobEntity& mob, float dt);
    void finish(Agent& agent, MobEntity& mob, NavState state);

    World& world_;
    EntityManager& entities_;
    std::unordered_map<EntityId, Agent> agents_;
    std::deque<EntityId> searchQueue_;
    int nodeBudget_ = 24000;
    int nodesLastTick_ = 0;
};

}  // namespace finevox
