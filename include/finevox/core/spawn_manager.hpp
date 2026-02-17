#pragma once

/**
 * @file spawn_manager.hpp
 * @brief SpawnManager — rules-based entity spawning system
 *
 * Periodically evaluates spawn rules against the world state,
 * finding valid surfaces near players and spawning entity groups.
 */

#include "finevox/core/spawn_rule.hpp"
#include "finevox/core/position.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <optional>
#include <random>

namespace finevox {

class World;
class EntityManager;

class SpawnManager {
public:
    SpawnManager();

    /// Add a spawn rule
    void addRule(SpawnRule rule);

    /// Clear all rules
    void clearRules();

    /// Get number of rules
    [[nodiscard]] size_t ruleCount() const { return rules_.size(); }

    /// Get the rules (for testing)
    [[nodiscard]] const std::vector<SpawnRule>& rules() const { return rules_; }

    /// Process one tick of spawning logic
    void tick(float dt, World& world, EntityManager& em,
              const std::vector<glm::dvec3>& playerPositions);

    /// Set global mob cap (total entities allowed)
    void setGlobalMobCap(int cap) { globalMobCap_ = cap; }
    [[nodiscard]] int globalMobCap() const { return globalMobCap_; }

    /// Set spawn check interval (seconds)
    void setSpawnInterval(float seconds) { spawnInterval_ = seconds; }
    [[nodiscard]] float spawnInterval() const { return spawnInterval_; }

    /// Find a valid spawn surface near a position
    [[nodiscard]] std::optional<BlockCoord> findSpawnSurface(
        const World& world,
        const BlockCoord& near,
        const SpawnRule& rule) const;

    /// Check if a position satisfies a rule's light constraints
    [[nodiscard]] bool checkLightLevel(const World& world,
                                       const BlockCoord& pos,
                                       const SpawnRule& rule) const;

    /// Count entities of a given type in the entity manager
    [[nodiscard]] int countEntitiesOfType(const EntityManager& em,
                                          EntityTypeId typeId) const;

private:
    std::vector<SpawnRule> rules_;
    int globalMobCap_ = 80;
    float spawnTimer_ = 0.0f;
    float spawnInterval_ = 1.0f;
    mutable std::mt19937 rng_{std::random_device{}()};

    bool trySpawnGroup(const SpawnRule& rule, World& world, EntityManager& em,
                       const glm::dvec3& nearPlayer);

    /// Select a rule by weighted random
    const SpawnRule* selectRule() const;
};

}  // namespace finevox
