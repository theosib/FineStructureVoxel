#pragma once

/**
 * @file projectile_system.hpp
 * @brief Ballistic projectiles that fly through the real world
 *
 * Projectiles are simulated particles, not hitscan: they arc under gravity and
 * stop on the first solid block or enemy body in their path. Cover needs no special
 * case. An arrow that meets a merlon hits the merlon. Game thread, except where
 * noted.
 */

#include "finevox/core/game_subsystem.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/position.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace finevox {

class World;
class EntityManager;

struct ProjectileSpec {
    glm::dvec3 origin{0.0};
    glm::dvec3 velocity{0.0};
    float gravity = 14.0f;     ///< Magnitude, downward
    float damage = 1.0f;
    float lifetime = 6.0f;     ///< Seconds before it falls out of the world
    float hitRadius = 0.25f;   ///< Added to a body's box when testing hits
    uint16_t faction = 0;      ///< Won't strike its own side
    EntityId owner = INVALID_ENTITY_ID;
};

struct ProjectileHit {
    glm::dvec3 position{0.0};
    EntityId entity = INVALID_ENTITY_ID;  ///< INVALID when it struck a block
    BlockCoord block{};
    EntityId owner = INVALID_ENTITY_ID;
    uint16_t faction = 0;
    float damage = 0.0f;
};

/// For drawing: where each projectile is and which way it's heading
struct ProjectileView {
    glm::dvec3 position;
    glm::vec3 velocity;
    uint16_t faction;
};

/// Launch velocity to reach `to` from `from` at a given speed. Takes the flatter of
/// the two arcs unless `lobbed`. nullopt if the target is out of range.
[[nodiscard]] std::optional<glm::dvec3> solveBallistic(const glm::dvec3& from, const glm::dvec3& to,
                                                       float speed, float gravity,
                                                       bool lobbed = false);

class ProjectileSystem : public GameSubsystem {
public:
    ProjectileSystem(World& world, EntityManager& entities);

    [[nodiscard]] std::string_view name() const override { return "Projectiles"; }
    [[nodiscard]] TickPhase phase() const override { return TickPhase::PostTick; }
    [[nodiscard]] int32_t priority() const override { return 20; }
    void tick(float dt) override;

    void fire(const ProjectileSpec& spec);

    /// Hits since the last call; game logic reacts to these
    [[nodiscard]] std::vector<ProjectileHit> drainHits();

    /// Thread-safe copy for a renderer
    [[nodiscard]] std::vector<ProjectileView> snapshot() const;

    [[nodiscard]] size_t activeCount() const { return active_.size(); }

private:
    struct Projectile {
        ProjectileSpec spec;
        glm::dvec3 position;
        glm::dvec3 velocity;
        float age = 0.0f;
    };

    /// Advance one projectile. True when it's finished.
    bool step(Projectile& p, float dt);

    World& world_;
    EntityManager& entities_;
    std::vector<Projectile> active_;
    std::vector<ProjectileHit> hits_;

    mutable std::mutex viewMutex_;
    std::vector<ProjectileView> view_;
};

}  // namespace finevox
