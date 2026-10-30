#include "finevox/core/navigation.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace finevox {

namespace {

constexpr float DIAGONAL_COST = 1.414f;
constexpr float CLIMB_PENALTY = 0.4f;
constexpr float WADE_PENALTY = 3.0f;

/// Step height for agents under navigation. Default physics stops at 0.6, and a
/// mob jump clears less than a block, so without this no terrain step is passable.
constexpr float AGENT_STEP_HEIGHT = 1.05f;

constexpr float ARRIVE_RADIUS = 0.45f;
/// Placing a block next to an agent needs its body clear of that cell
constexpr float BUILD_STANCE_RADIUS = 0.2f;
constexpr float STUCK_SECONDS = 2.0f;
constexpr int MAX_REPATHS = 2;
constexpr float MIN_HOP_PROGRESS = 2.0f;

uint64_t packCoord(const BlockCoord& p) {
    // 24 bits each is ±8M blocks, far beyond any loaded column in practice
    return (static_cast<uint64_t>(static_cast<uint32_t>(p.x) & 0xFFFFFF) << 40) |
           (static_cast<uint64_t>(static_cast<uint32_t>(p.y) & 0xFFFF) << 24) |
           static_cast<uint64_t>(static_cast<uint32_t>(p.z) & 0xFFFFFF);
}

/// Per-search view of the world. A search re-reads the same cells many times from
/// neighbouring nodes, and block type properties sit behind a registry lock.
class CellProbe {
public:
    enum Flag : uint8_t {
        Solid = 1,    ///< Something to stand on
        Blocked = 2,  ///< Occupies the cell; can't pass through
        Fluid = 4,
    };

    explicit CellProbe(const World& world) : world_(world) {
        cells_.reserve(4096);
    }

    uint8_t flags(const BlockCoord& pos) {
        uint64_t key = packCoord(pos);
        auto [it, inserted] = cells_.try_emplace(key, uint8_t{0});
        if (!inserted) return it->second;

        uint8_t result = 0;
        BlockTypeId block = world_.getBlock(pos);
        if (!block.isAir() && BlockRegistry::global().getType(block).hasCollision()) {
            result |= Solid | Blocked;
        }
        if (!world_.getFluid(pos).isEmpty()) {
            result |= Fluid;
        }
        it->second = result;
        return result;
    }

    bool solid(const BlockCoord& pos) { return flags(pos) & Solid; }
    bool clear(const BlockCoord& pos) { return !(flags(pos) & Blocked); }

    float floorSpeed(const BlockCoord& below) {
        BlockTypeId block = world_.getBlock(below);
        return block.isAir() ? 1.0f : BlockRegistry::global().getType(block).walkSpeed();
    }

private:
    const World& world_;
    std::unordered_map<uint64_t, uint8_t> cells_;
};

BlockCoord up(BlockCoord p, int n = 1) { return BlockCoord(p.x, p.y + n, p.z); }
BlockCoord down(BlockCoord p, int n = 1) { return BlockCoord(p.x, p.y - n, p.z); }

/// Node bookkeeping. Actions are the blocks to change before entering the cell.
struct NodeInfo {
    uint64_t parent = 0;
    BlockCoord pos;
    float g = 0.0f;
    bool closed = false;
    uint8_t actionCount = 0;
    NavAction actions[2]{NavAction::Walk, NavAction::Walk};
    BlockCoord actionPos[2];
};

struct OpenEntry {
    float f;
    uint64_t key;
    bool operator>(const OpenEntry& other) const { return f > other.f; }
};

class Search {
public:
    Search(const World& world, const NavQuery& query)
        : query_(query), probe_(world), clearance_(std::max(1, int(std::ceil(query.entityHeight)))) {
        startCell_ = toCell(query.start);
        goalCell_ = toCell(query.goal);
    }

    std::optional<NavPath> run(int* outNodes) {
        uint64_t startKey = packCoord(startCell_);
        NodeInfo& start = nodes_[startKey];
        start.pos = startCell_;
        start.parent = startKey;
        open_.push({heuristic(startCell_), startKey});

        int expanded = 0;
        uint64_t closestKey = startKey;
        float closest = heuristic(startCell_);
        while (!open_.empty() && expanded < query_.maxNodes) {
            OpenEntry entry = open_.top();
            open_.pop();

            NodeInfo& current = nodes_[entry.key];
            if (current.closed) continue;
            current.closed = true;
            ++expanded;

            if (closeEnough(current.pos)) {
                if (outNodes) *outNodes = expanded;
                return reconstruct(entry.key, expanded);
            }

            float remaining = heuristic(current.pos);
            if (remaining < closest) {
                closest = remaining;
                closestKey = entry.key;
            }
            expand(entry.key, current);
        }

        if (outNodes) *outNodes = expanded;
        if (query_.allowPartial && closestKey != startKey) {
            NavPath path = reconstruct(closestKey, expanded);
            path.partial = true;
            return path;
        }
        return std::nullopt;
    }

private:
    static BlockCoord toCell(const glm::dvec3& p) {
        return BlockCoord(static_cast<int32_t>(std::floor(p.x)),
                          static_cast<int32_t>(std::floor(p.y)),
                          static_cast<int32_t>(std::floor(p.z)));
    }

    bool bodyClear(const BlockCoord& feet) {
        for (int i = 0; i < clearance_; ++i) {
            if (!probe_.clear(up(feet, i))) return false;
        }
        return true;
    }

    bool isStandableHere(const BlockCoord& feet) {
        return probe_.solid(down(feet)) && bodyClear(feet);
    }

    bool closeEnough(const BlockCoord& cell) {
        int dx = cell.x - goalCell_.x;
        int dz = cell.z - goalCell_.z;
        int dy = cell.y - goalCell_.y;
        if (query_.goalRange <= 0.0f) return dx == 0 && dy == 0 && dz == 0;
        // Ranged goals are for interacting with something: reach past it at the
        // same level, or a little above or below.
        float reach = query_.goalRange + 0.5f;
        return float(dx * dx + dz * dz) <= reach * reach && std::abs(dy) <= 2;
    }

    float heuristic(const BlockCoord& cell) const {
        int dx = std::abs(cell.x - goalCell_.x);
        int dz = std::abs(cell.z - goalCell_.z);
        int dy = std::abs(cell.y - goalCell_.y);
        int diag = std::min(dx, dz);
        int straight = std::max(dx, dz) - diag;
        return (diag * DIAGONAL_COST + straight) / query_.fastestSurface + dy;
    }

    void consider(uint64_t fromKey, const NodeInfo& from, const BlockCoord& cell, float stepCost,
                  uint8_t actionCount = 0, NavAction a0 = NavAction::Walk, BlockCoord p0 = {},
                  NavAction a1 = NavAction::Walk, BlockCoord p1 = {}) {
        uint64_t key = packCoord(cell);
        float g = from.g + stepCost;
        auto it = nodes_.find(key);
        if (it != nodes_.end() && (it->second.closed || it->second.g <= g)) return;

        NodeInfo& node = nodes_[key];
        node.parent = fromKey;
        node.pos = cell;
        node.g = g;
        node.actionCount = actionCount;
        node.actions[0] = a0;
        node.actionPos[0] = p0;
        node.actions[1] = a1;
        node.actionPos[1] = p1;
        open_.push({g + heuristic(cell), key});
    }

    float walkCost(const BlockCoord& feet, float base) {
        float speed = std::max(0.25f, probe_.floorSpeed(down(feet)));
        float cost = base / speed;
        if (probe_.flags(feet) & CellProbe::Fluid) cost += WADE_PENALTY;
        return cost;
    }

    void expand(uint64_t key, const NodeInfo& current) {
        // nodes_ may rehash below; everything needed is copied out here
        const NodeInfo node = current;
        static constexpr int DX[8] = {1, -1, 0, 0, 1, 1, -1, -1};
        static constexpr int DZ[8] = {0, 0, 1, -1, 1, -1, 1, -1};

        for (int i = 0; i < 8; ++i) {
            BlockCoord next(node.pos.x + DX[i], node.pos.y, node.pos.z + DZ[i]);
            bool diagonal = i >= 4;

            if (diagonal) {
                // No corner cutting: both orthogonal cells must admit the body
                BlockCoord sideA(node.pos.x + DX[i], node.pos.y, node.pos.z);
                BlockCoord sideB(node.pos.x, node.pos.y, node.pos.z + DZ[i]);
                if (!bodyClear(sideA) || !bodyClear(sideB)) continue;
                if (isStandableHere(next)) {
                    consider(key, node, next, walkCost(next, DIAGONAL_COST));
                }
                continue;
            }

            expandStraight(key, node, next);
        }

    }

    void expandStraight(uint64_t key, const NodeInfo& node, const BlockCoord& next) {
        bool headroomToClimb = probe_.clear(up(node.pos, clearance_));

        if (bodyClear(next)) {
            if (probe_.solid(down(next))) {
                consider(key, node, next, walkCost(next, 1.0f));
                return;
            }

            // No floor: a short drop if there's ground, otherwise a bridge
            bool landed = false;
            for (int drop = 1; drop <= query_.maxFall; ++drop) {
                BlockCoord cell = down(next, drop);
                if (!probe_.clear(cell)) break;
                if (probe_.solid(down(cell))) {
                    int excess = std::max(drop - query_.comfortableFall, 0);
                    consider(key, node, cell,
                             walkCost(cell, 1.0f + 0.3f * drop) + excess * query_.fallPenalty);
                    landed = true;
                    break;
                }
            }
            if (!landed && query_.allowPlace) {
                consider(key, node, next, query_.placeCost + 1.0f, 1, NavAction::Place, down(next));
            }

            // Raise a step in the open cell and climb onto it
            if (query_.allowPlace && headroomToClimb) {
                BlockCoord above = up(next);
                if (bodyClear(above)) {
                    consider(key, node, above, query_.placeCost + 1.0f + CLIMB_PENALTY, 1,
                             NavAction::Place, next);
                }
            }
            return;
        }

        // Something in the way: step onto it if it's a single block high
        if (headroomToClimb) {
            BlockCoord above = up(next);
            if (probe_.solid(next) && bodyClear(above)) {
                consider(key, node, above, walkCost(above, 1.0f + CLIMB_PENALTY));
                return;
            }
        }

        if (query_.allowDig) {
            // Tunnel forward. Floor must already exist (or be dug out of a solid
            // mass, in which case the cell below is solid by construction).
            if (!probe_.solid(down(next))) return;
            BlockCoord feet = next;
            BlockCoord head = up(next);
            bool digFeet = !probe_.clear(feet);
            bool digHead = clearance_ > 1 && !probe_.clear(head);
            if (digFeet && digHead) {
                consider(key, node, next, 1.0f + 2.0f * query_.digCost, 2, NavAction::Dig, head,
                         NavAction::Dig, feet);
            } else if (digFeet) {
                consider(key, node, next, 1.0f + query_.digCost, 1, NavAction::Dig, feet);
            } else if (digHead) {
                consider(key, node, next, 1.0f + query_.digCost, 1, NavAction::Dig, head);
            }
        }
    }

    NavPath reconstruct(uint64_t goalKey, int expanded) {
        std::vector<NavStep> reversed;
        uint64_t key = goalKey;
        while (true) {
            const NodeInfo& node = nodes_[key];
            if (node.parent == key) break;
            reversed.push_back({node.pos, NavAction::Walk, {}});
            const NodeInfo& parent = nodes_[node.parent];
            for (int a = node.actionCount - 1; a >= 0; --a) {
                reversed.push_back({parent.pos, node.actions[a], node.actionPos[a]});
            }
            key = node.parent;
        }
        NavPath path;
        path.nodesExpanded = expanded;
        path.steps.assign(reversed.rbegin(), reversed.rend());
        return path;
    }

    NavQuery query_;
    CellProbe probe_;
    int clearance_;
    BlockCoord startCell_;
    BlockCoord goalCell_;
    std::unordered_map<uint64_t, NodeInfo> nodes_;
    std::priority_queue<OpenEntry, std::vector<OpenEntry>, std::greater<>> open_;
};

}  // namespace

bool isStandable(const World& world, BlockCoord pos, float entityHeight) {
    CellProbe probe(world);
    if (!probe.solid(down(pos))) return false;
    int clearance = std::max(1, int(std::ceil(entityHeight)));
    for (int i = 0; i < clearance; ++i) {
        if (!probe.clear(up(pos, i))) return false;
    }
    return true;
}

std::optional<NavPath> findNavPath(const World& world, const NavQuery& query, int* outNodes) {
    Search search(world, query);
    return search.run(outNodes);
}

// ============================================================================
// NavigationSystem
// ============================================================================

NavigationSystem::NavigationSystem(World& world, EntityManager& entities)
    : world_(world), entities_(entities) {}

void NavigationSystem::order(EntityId id, const NavOrder& order) {
    Agent& agent = agents_[id];
    agent.order = order;
    agent.state = NavState::Pending;
    agent.steps.clear();
    agent.next = 0;
    agent.repaths = 0;
    agent.stuckTime = 0.0f;
    agent.bestDistance = 1e9f;
    agent.partialPath = false;
    agent.lastHopDistance = 1e9f;
    searchQueue_.push_back(id);
}

void NavigationSystem::stop(EntityId id) {
    if (auto* mob = entities_.getMob(id)) {
        mob->clearMoveTarget();
    }
    agents_.erase(id);
}

void NavigationSystem::acknowledge(EntityId id) {
    auto it = agents_.find(id);
    if (it == agents_.end()) return;
    if (it->second.state == NavState::Arrived || it->second.state == NavState::Failed) {
        agents_.erase(it);
    }
}

NavState NavigationSystem::state(EntityId id) const {
    auto it = agents_.find(id);
    return it == agents_.end() ? NavState::Idle : it->second.state;
}

std::optional<NavStep> NavigationSystem::currentAction(EntityId id) const {
    auto it = agents_.find(id);
    if (it == agents_.end() || it->second.state != NavState::Blocked) return std::nullopt;
    const Agent& agent = it->second;
    if (agent.next >= agent.steps.size()) return std::nullopt;
    return agent.steps[agent.next];
}

void NavigationSystem::resume(EntityId id) {
    auto it = agents_.find(id);
    if (it == agents_.end() || it->second.state != NavState::Blocked) return;
    Agent& agent = it->second;
    ++agent.next;
    agent.state = NavState::Moving;
    agent.stuckTime = 0.0f;
    agent.bestDistance = 1e9f;
}

void NavigationSystem::tick(float dt) {
    runSearches();

    for (auto it = agents_.begin(); it != agents_.end();) {
        MobEntity* mob = entities_.getMob(it->first);
        if (!mob || !mob->isAlive()) {
            it = agents_.erase(it);
            continue;
        }
        if (it->second.state == NavState::Moving) {
            followPath(it->first, it->second, *mob, dt);
        }
        ++it;
    }
}

void NavigationSystem::runSearches() {
    int spent = 0;
    while (!searchQueue_.empty() && spent < nodeBudget_) {
        EntityId id = searchQueue_.front();
        searchQueue_.pop_front();

        auto it = agents_.find(id);
        if (it == agents_.end() || it->second.state != NavState::Pending) continue;
        Agent& agent = it->second;

        MobEntity* mob = entities_.getMob(id);
        if (!mob) continue;
        Vec3 pos = mob->position();
        Vec3 half = mob->halfExtents();

        NavQuery query;
        query.start = glm::dvec3(pos.x, pos.y + 0.05, pos.z);
        query.goal = agent.order.goal;
        query.goalRange = agent.order.goalRange;
        query.entityHeight = half.y * 2.0f;
        query.allowPlace = agent.order.allowPlace;
        query.allowDig = agent.order.allowDig;
        query.maxNodes = agent.order.maxNodes;
        query.maxFall = maxFall_;
        query.allowPartial = agent.order.allowPartial;

        int nodes = 0;
        auto path = findNavPath(world_, query, &nodes);
        spent += std::max(nodes, 1);

        if (!path) {
            agent.state = NavState::Failed;
            mob->clearMoveTarget();
            continue;
        }
        agent.partialPath = path->partial;
        agent.steps = std::move(path->steps);
        agent.next = 0;
        agent.state = NavState::Moving;
        agent.stuckTime = 0.0f;
        agent.bestDistance = 1e9f;
        mob->setMaxStepHeight(AGENT_STEP_HEIGHT);
        mob->setArriveRadius(BUILD_STANCE_RADIUS / 2.0f);
    }
    nodesLastTick_ = spent;
}

void NavigationSystem::finish(Agent& agent, MobEntity& mob, NavState state) {
    agent.state = state;
    agent.steps.clear();
    agent.next = 0;
    mob.clearMoveTarget();
}

void NavigationSystem::beginNextHop(EntityId id, Agent& agent, MobEntity& mob) {
    Vec3 pos = mob.position();
    double dx = agent.order.goal.x - pos.x;
    double dz = agent.order.goal.z - pos.z;
    float remaining = static_cast<float>(std::sqrt(dx * dx + dz * dz));

    // Each hop has to gain ground, or the closest point is a dead end
    if (remaining >= agent.lastHopDistance - MIN_HOP_PROGRESS) {
        finish(agent, mob, NavState::Failed);
        return;
    }
    agent.lastHopDistance = remaining;
    agent.state = NavState::Pending;
    agent.steps.clear();
    agent.next = 0;
    searchQueue_.push_back(id);
}

void NavigationSystem::followPath(EntityId id, Agent& agent, MobEntity& mob, float dt) {
    if (agent.next >= agent.steps.size()) {
        if (agent.partialPath) {
            beginNextHop(id, agent, mob);
        } else {
            finish(agent, mob, NavState::Arrived);
        }
        return;
    }

    Vec3 pos = mob.position();
    const NavStep& step = agent.steps[agent.next];
    glm::dvec3 target(step.pos.x + 0.5, step.pos.y, step.pos.z + 0.5);

    double dx = target.x - pos.x;
    double dz = target.z - pos.z;
    float distance = static_cast<float>(std::sqrt(dx * dx + dz * dz));
    float dy = static_cast<float>(target.y - pos.y);
    bool atCell = distance < ARRIVE_RADIUS && std::abs(dy) < 1.5f;

    if (step.action != NavAction::Walk) {
        // A build step happens from the cell before the change. Stop there,
        // centred enough that a block appearing next door can't clip the body.
        if (distance < BUILD_STANCE_RADIUS && std::abs(dy) < 1.5f) {
            agent.state = NavState::Blocked;
            mob.clearMoveTarget();
            return;
        }
    } else if (atCell) {
        ++agent.next;
        agent.stuckTime = 0.0f;
        agent.bestDistance = 1e9f;
        if (agent.next >= agent.steps.size()) {
            if (agent.partialPath) {
                beginNextHop(id, agent, mob);
            } else {
                finish(agent, mob, NavState::Arrived);
            }
            return;
        }
        const NavStep& upcoming = agent.steps[agent.next];
        target = glm::dvec3(upcoming.pos.x + 0.5, upcoming.pos.y, upcoming.pos.z + 0.5);
    }

    mob.moveTo(target);

    if (distance < agent.bestDistance - 0.05f) {
        agent.bestDistance = distance;
        agent.stuckTime = 0.0f;
        return;
    }

    agent.stuckTime += dt;
    if (agent.stuckTime < STUCK_SECONDS) return;

    // Something moved (a wall went up, another unit is in the way). Search again
    // from here rather than pushing against it forever.
    if (agent.repaths >= MAX_REPATHS) {
        finish(agent, mob, NavState::Failed);
        return;
    }
    ++agent.repaths;
    agent.state = NavState::Pending;
    agent.stuckTime = 0.0f;
    agent.bestDistance = 1e9f;
    searchQueue_.push_back(id);
}

}  // namespace finevox
