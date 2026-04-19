#include "finevox/core/item_drop_entity.hpp"
#include "finevox/core/entity_manager.hpp"

#include <algorithm>
#include <cmath>

namespace finevox {

ItemDropEntity::ItemDropEntity(EntityId id, ItemStack item)
    : Entity(id, EntityType::ItemDrop)
    , item_(std::move(item))
{
    // Small bounding box for item drops
    halfExtents_ = Vec3(0.125f, 0.125f, 0.125f);
    eyeHeight_ = 0.25f;
    hasGravity_ = true;
    maxStepHeight_ = 0.0f;
}

ItemStack ItemDropEntity::takeItem() {
    ItemStack taken = std::move(item_);
    item_.clear();
    return taken;
}

void ItemDropEntity::tick(float dt, World& /*world*/) {
    age_ += dt;
    if (age_ >= maxAge_) {
        markForRemoval();
        return;
    }

    if (!entityManager_ || item_.isEmpty()) return;

    // Cheapest: check local player first (single-player fast path).
    // Fallback: scan nearby entities for any player.
    const Vec3 myPos = position();
    float attractRadius = std::max(pickupRadius_, attractRadius_);
    float bestDistSq = attractRadius * attractRadius;
    EntityId bestPlayerId = INVALID_ENTITY_ID;
    Vec3 bestPlayerPos{0.0f};

    auto considerPlayer = [&](EntityId id, const Entity& entity) {
        if (!entity.isPlayerEntity()) return;
        Vec3 p = entity.position();
        float dx = p.x - myPos.x;
        float dy = p.y - myPos.y;
        float dz = p.z - myPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 <= bestDistSq) {
            bestDistSq = d2;
            bestPlayerId = id;
            bestPlayerPos = p;
        }
    };

    // Fast path: single-player local player
    EntityId localId = entityManager_->localPlayerId();
    if (localId != INVALID_ENTITY_ID) {
        if (const Entity* local = entityManager_->getEntity(localId)) {
            considerPlayer(localId, *local);
        }
    }

    // Multiplayer / additional players: scan nearby cells
    if (bestPlayerId == INVALID_ENTITY_ID) {
        auto nearby = entityManager_->spatialIndex().queryRadius(myPos, attractRadius);
        for (EntityId id : nearby) {
            if (id == localId) continue;  // already considered
            if (const Entity* e = entityManager_->getEntity(id)) {
                considerPlayer(id, *e);
            }
        }
    }

    if (bestPlayerId == INVALID_ENTITY_ID) return;

    float bestDist = std::sqrt(bestDistSq);

    if (bestDist <= pickupRadius_ && isPickupable()) {
        if (entityManager_->tryDeliverItemToPlayer(bestPlayerId, item_)) {
            if (item_.isEmpty()) {
                markForRemoval();
            }
        }
        return;
    }

    // Magnetism: accelerate toward the player, clamped to maxAttractSpeed_
    if (bestDist > 1e-4f && bestDist <= attractRadius_) {
        Vec3 toPlayer(bestPlayerPos.x - myPos.x,
                      bestPlayerPos.y - myPos.y,
                      bestPlayerPos.z - myPos.z);
        float invDist = 1.0f / bestDist;
        Vec3 dir(toPlayer.x * invDist, toPlayer.y * invDist, toPlayer.z * invDist);

        Vec3 vel = velocity();
        vel.x += dir.x * attractAccel_ * dt;
        vel.y += dir.y * attractAccel_ * dt;
        vel.z += dir.z * attractAccel_ * dt;

        float horizSpeed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
        if (horizSpeed > maxAttractSpeed_) {
            float scale = maxAttractSpeed_ / horizSpeed;
            vel.x *= scale;
            vel.z *= scale;
        }
        setVelocity(vel);
    }
}

std::string ItemDropEntity::typeName() const {
    return "ItemDrop";
}

}  // namespace finevox
