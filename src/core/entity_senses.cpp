#include "finevox/core/entity_senses.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace finevox {

void EntitySenses::update(const MobEntity& self, EntityManager& em, float dt) {
    updateTimer_ -= dt;
    if (updateTimer_ <= 0.0f) {
        refresh(self, em);
        updateTimer_ = scanInterval_;
    } else {
        std::erase_if(visible_, [](const std::weak_ptr<Entity>& w) {
            auto e = w.lock();
            return !e || e->isMarkedForRemoval();
        });
        if (auto p = nearestPlayer_.lock(); !p || p->isMarkedForRemoval()) {
            nearestPlayer_.reset();
        }
    }
}

void EntitySenses::refresh(const MobEntity& self, EntityManager& em) {
    visible_.clear();
    nearestPlayer_.reset();

    float nearestPlayerDist = std::numeric_limits<float>::max();
    auto selfPos = self.position();
    float rangeSq = scanRange_ * scanRange_;

    for (const auto& [id, entity] : em.entities()) {
        if (id == self.id()) continue;
        if (entity->isMarkedForRemoval()) continue;

        auto diff = entity->position() - selfPos;
        float distSq = static_cast<float>(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);

        if (distSq <= rangeSq) {
            visible_.push_back(entity);

            if (isPlayer(*entity) && distSq < nearestPlayerDist) {
                nearestPlayerDist = distSq;
                nearestPlayer_ = entity;
            }
        }
    }
}

std::shared_ptr<Entity> EntitySenses::nearestPlayer() const {
    auto p = nearestPlayer_.lock();
    if (!p || p->isMarkedForRemoval()) {
        return nullptr;
    }
    return p;
}

std::vector<std::shared_ptr<Entity>> EntitySenses::visibleEntities() const {
    std::vector<std::shared_ptr<Entity>> out;
    out.reserve(visible_.size());
    for (const auto& w : visible_) {
        if (auto e = w.lock(); e && !e->isMarkedForRemoval()) {
            out.push_back( std::move(e));
        }
    }
    return out;
}

size_t EntitySenses::visibleCount() const {
    size_t count = 0;
    for (const auto& w : visible_) {
        if (auto e = w.lock(); e && !e->isMarkedForRemoval()) {
            ++count;
        }
    }
    return count;
}

}  // namespace finevox
