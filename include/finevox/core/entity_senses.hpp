#pragma once

/**
 * @file entity_senses.hpp
 * @brief EntitySenses — cached nearby entity awareness for AI decisions
 *
 * Periodically scans for nearby entities and caches results to avoid
 * expensive lookups every tick.
 */

#include "finevox/core/entity_state.hpp"
#include <vector>

namespace finevox {

class Entity;
class MobEntity;
class EntityManager;

class EntitySenses {
public:
    EntitySenses() = default;

    /// Update senses (re-scans when timer expires)
    void update(const MobEntity& self, EntityManager& em, float dt);

    /// Force an immediate re-scan
    void refresh(const MobEntity& self, EntityManager& em);

    /// Get nearest player entity (or nullptr)
    [[nodiscard]] Entity* nearestPlayer() const { return nearestPlayer_; }

    /// Get all visible entities
    [[nodiscard]] const std::vector<Entity*>& visibleEntities() const { return visible_; }

    /// Get number of visible entities
    [[nodiscard]] size_t visibleCount() const { return visible_.size(); }

    /// Set scan range (defaults to follow range from EntityTypeDef)
    void setScanRange(float range) { scanRange_ = range; }
    [[nodiscard]] float scanRange() const { return scanRange_; }

    /// Set scan interval (seconds between re-scans)
    void setScanInterval(float interval) { scanInterval_ = interval; }

private:
    std::vector<Entity*> visible_;
    Entity* nearestPlayer_ = nullptr;
    float updateTimer_ = 0.0f;
    float scanInterval_ = 0.5f;  // Re-scan every 0.5s
    float scanRange_ = 16.0f;
};

}  // namespace finevox
