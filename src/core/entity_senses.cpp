#include "finevox/core/entity_senses.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include <cmath>
#include <limits>

namespace finevox {

void EntitySenses::update(const MobEntity& self, EntityManager& em, float dt) {
    updateTimer_ -= dt;
    if (updateTimer_ <= 0.0f) {
        refresh(self, em);
        updateTimer_ = scanInterval_;
    }
}

void EntitySenses::refresh(const MobEntity& self, EntityManager& em) {
    visible_.clear();
    nearestPlayer_ = nullptr;

    float nearestPlayerDist = std::numeric_limits<float>::max();
    auto selfPos = self.position();
    float rangeSq = scanRange_ * scanRange_;

    for (auto& [id, entity] : em.entities()) {
        if (id == self.id()) continue;
        if (entity->isMarkedForRemoval()) continue;

        auto diff = entity->position() - selfPos;
        float distSq = static_cast<float>(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        if (distSq <= rangeSq) {
            visible_.push_back(entity.get());

            if (isPlayer(*entity) && distSq < nearestPlayerDist) {
                nearestPlayerDist = distSq;
                nearestPlayer_ = entity.get();
            }
        }
    }
}

}  // namespace finevox
