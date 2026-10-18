#include "finevox/core/ai_goals.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/pathfinder.hpp"
#include <cmath>
#include <random>

namespace finevox {

namespace {
// Thread-local RNG for AI decisions
thread_local std::mt19937 aiRng{std::random_device{}()};

float randomFloat(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(aiRng);
}
}  // anonymous namespace

// ============================================================================
// IdleGoal
// ============================================================================

bool IdleGoal::canStart(MobEntity& /*mob*/) {
    return true;  // Always a valid fallback
}

void IdleGoal::start(MobEntity& mob) {
    idleTimer_ = 0.0f;
    idleDuration_ = randomFloat(params_.minDuration, params_.maxDuration);
    mob.clearMoveTarget();
    mob.setAnimation(params_.animSlot);
}

void IdleGoal::tick(MobEntity& /*mob*/, float dt) {
    idleTimer_ += dt;
}

bool IdleGoal::isComplete(MobEntity& /*mob*/) {
    return idleTimer_ >= idleDuration_;
}

void IdleGoal::stop(MobEntity& /*mob*/) {
}

// ============================================================================
// WanderGoal
// ============================================================================

bool WanderGoal::canStart(MobEntity& /*mob*/) {
    return randomFloat(0.0f, 1.0f) < params_.startChance;
}

void WanderGoal::start(MobEntity& mob) {
    auto pos = mob.position();
    float dx = randomFloat(-params_.range, params_.range);
    float dz = randomFloat(-params_.range, params_.range);
    target_ = glm::dvec3(pos.x + dx, pos.y, pos.z + dz);

    mob.moveTo(target_);
    mob.setAnimation(params_.animSlot);
    timeout_ = 0.0f;
}

void WanderGoal::tick(MobEntity& /*mob*/, float dt) {
    timeout_ += dt;
}

bool WanderGoal::isComplete(MobEntity& mob) {
    if (timeout_ >= params_.maxTime) return true;
    return !mob.hasMoveTarget();
}

void WanderGoal::stop(MobEntity& mob) {
    mob.clearMoveTarget();
}

// ============================================================================
// ChaseGoal
// ============================================================================

bool ChaseGoal::canStart(MobEntity& mob) {
    const auto* def = mob.typeDef();
    if (!def) return false;

    // Hostile: chase any visible player
    if (def->aiType == AIType::Hostile) {
        return mob.senses().nearestPlayer() != nullptr;
    }

    // Neutral: only chase if recently damaged
    if (def->aiType == AIType::Neutral) {
        return mob.wasRecentlyDamaged() && mob.lastAttacker() != INVALID_ENTITY_ID;
    }

    return false;
}

void ChaseGoal::start(MobEntity& mob) {
    const auto* def = mob.typeDef();
    if (def && def->aiType == AIType::Hostile) {
        auto* player = mob.senses().nearestPlayer();
        targetId_ = player ? player->id() : INVALID_ENTITY_ID;
    } else {
        targetId_ = mob.lastAttacker();
    }
    repathTimer_ = params_.repathInterval;  // Trigger immediate path computation on first tick
    path_.clear();
    pathIndex_ = 0;
    mob.setAnimation(params_.animSlot);
}

void ChaseGoal::tick(MobEntity& mob, float dt) {
    repathTimer_ += dt;

    if (targetId_ == INVALID_ENTITY_ID) return;

    // Find target entity through senses (or entityManager)
    Entity* target = nullptr;
    for (auto* e : mob.senses().visibleEntities()) {
        if (e->id() == targetId_) {
            target = e;
            break;
        }
    }
    if (!target && mob.entityManager()) {
        target = mob.entityManager()->getEntity(targetId_);
    }

    if (!target) return;

    // Advance along existing path waypoints when current waypoint is reached
    if (pathIndex_ < path_.size() && !mob.hasMoveTarget()) {
        const auto& wp = path_[pathIndex_++];
        mob.moveTo(glm::dvec3(wp.pos.x + 0.5, wp.pos.y, wp.pos.z + 0.5));
    }

    // Re-path periodically or when out of waypoints
    if (repathTimer_ >= params_.repathInterval ||
        (!mob.hasMoveTarget() && pathIndex_ >= path_.size())) {
        repathTimer_ = 0.0f;
        path_.clear();
        pathIndex_ = 0;

        glm::dvec3 startPos(mob.position());
        glm::dvec3 goalPos(target->position());

        if (mob.world()) {
            auto he = mob.halfExtents();
            auto found = Pathfinder::findPath(
                *mob.world(), startPos, goalPos,
                he.x * 2.0f, he.y * 2.0f,
                static_cast<int32_t>(std::ceil(params_.maxRange * 2.0f)));
            if (found && !found->empty()) {
                path_ = std::move(*found);
                // Skip start node if we're already in that block cell
                if (path_.size() > 1) {
                    pathIndex_ = 1;
                } else {
                    pathIndex_ = 0;
                }
                const auto& wp = path_[pathIndex_++];
                mob.moveTo(glm::dvec3(wp.pos.x + 0.5, wp.pos.y, wp.pos.z + 0.5));
                return;
            }
        }

        // Fallback when unobstructed or no walkable floor path found
        mob.moveTo(goalPos);
    }
}

bool ChaseGoal::isComplete(MobEntity& mob) {
    if (targetId_ == INVALID_ENTITY_ID) return true;

    // Lost sight of target
    bool targetVisible = false;
    for (auto* e : mob.senses().visibleEntities()) {
        if (e->id() == targetId_) {
            targetVisible = true;
            break;
        }
    }
    if (!targetVisible) return true;

    // Neutral mobs stop chasing after a while
    const auto* def = mob.typeDef();
    if (def && def->aiType == AIType::Neutral && !mob.wasRecentlyDamaged(params_.damageMemory)) {
        return true;
    }

    return false;
}

void ChaseGoal::stop(MobEntity& mob) {
    targetId_ = INVALID_ENTITY_ID;
    path_.clear();
    pathIndex_ = 0;
    mob.clearMoveTarget();
}

// ============================================================================
// AttackGoal
// ============================================================================

bool AttackGoal::canStart(MobEntity& mob) {
    const auto* def = mob.typeDef();
    if (!def || def->attackDamage <= 0.0f) return false;

    // Need a target in range
    for (auto* e : mob.senses().visibleEntities()) {
        if (!isPlayer(*e)) continue;

        auto diff = e->position() - mob.position();
        float dist = static_cast<float>(std::sqrt(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));

        if (dist <= def->attackRange) {
            return true;
        }
    }
    return false;
}

void AttackGoal::start(MobEntity& mob) {
    cooldownTimer_ = 0.0f;

    // Find closest player in range
    const auto* def = mob.typeDef();
    float bestDist = def ? def->attackRange : 1.5f;
    targetId_ = INVALID_ENTITY_ID;

    for (auto* e : mob.senses().visibleEntities()) {
        if (!isPlayer(*e)) continue;
        auto diff = e->position() - mob.position();
        float dist = static_cast<float>(std::sqrt(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));
        if (dist <= bestDist) {
            bestDist = dist;
            targetId_ = e->id();
        }
    }

    mob.setAnimation(params_.animSlot);
}

void AttackGoal::tick(MobEntity& mob, float dt) {
    cooldownTimer_ -= dt;
    if (cooldownTimer_ > 0.0f) return;

    const auto* def = mob.typeDef();
    float cooldown = def ? def->attackCooldown : 1.0f;
    float damage = def ? def->attackDamage : 1.0f;

    // Find target and deal damage
    for (auto* e : mob.senses().visibleEntities()) {
        if (e->id() != targetId_) continue;

        auto diff = e->position() - mob.position();
        float dist = static_cast<float>(std::sqrt(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));

        float range = def ? def->attackRange : 1.5f;
        if (dist <= range) {
            // Deal damage to target (if it's a MobEntity)
            auto* targetMob = dynamic_cast<MobEntity*>(e);
            if (targetMob) {
                targetMob->damage(damage, mob.id());
            }
            cooldownTimer_ = cooldown;
            mob.lookAt(glm::dvec3(e->position()));
        }
        break;
    }
}

bool AttackGoal::isComplete(MobEntity& mob) {
    // No target
    if (targetId_ == INVALID_ENTITY_ID) return true;

    // Target out of range
    const auto* def = mob.typeDef();
    float range = def ? def->attackRange : 1.5f;

    for (auto* e : mob.senses().visibleEntities()) {
        if (e->id() != targetId_) continue;
        auto diff = e->position() - mob.position();
        float dist = static_cast<float>(std::sqrt(
            diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));
        return dist > range * params_.rangeHysteresis;
    }

    return true;  // Target not visible
}

void AttackGoal::stop(MobEntity& /*mob*/) {
    targetId_ = INVALID_ENTITY_ID;
}

// ============================================================================
// FleeGoal
// ============================================================================

bool FleeGoal::canStart(MobEntity& mob) {
    // Flee when recently damaged (for passive mobs)
    const auto* def = mob.typeDef();
    if (def && def->aiType == AIType::Passive) {
        return mob.wasRecentlyDamaged(params_.damageMemory);
    }
    return false;
}

void FleeGoal::start(MobEntity& mob) {
    fleeTimer_ = 0.0f;

    auto pos = mob.position();

    // Locate threat entity (lastAttacker first, then nearest visible player)
    const Entity* threat = nullptr;
    if (mob.lastAttacker() != INVALID_ENTITY_ID) {
        if (mob.entityManager()) {
            threat = mob.entityManager()->getEntity(mob.lastAttacker());
        }
        if (!threat) {
            for (auto* e : mob.senses().visibleEntities()) {
                if (e->id() == mob.lastAttacker()) {
                    threat = e;
                    break;
                }
            }
        }
    }
    if (!threat) {
        threat = mob.senses().nearestPlayer();
    }

    float dx = 0.0f;
    float dz = 0.0f;
    bool haveDirection = false;
    if (threat) {
        float awayX = pos.x - threat->position().x;
        float awayZ = pos.z - threat->position().z;
        float len = std::sqrt(awayX * awayX + awayZ * awayZ);
        if (len > 1e-4f) {
            dx = (awayX / len) * params_.distance;
            dz = (awayZ / len) * params_.distance;
            haveDirection = true;
        }
    }

    if (!haveDirection) {
        dx = randomFloat(-params_.distance, params_.distance);
        dz = randomFloat(-params_.distance, params_.distance);
    }

    mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    mob.setSpeedMultiplier(params_.speedMult);
    mob.setAnimation(params_.animSlot);
}

void FleeGoal::tick(MobEntity& /*mob*/, float dt) {
    fleeTimer_ += dt;
}

bool FleeGoal::isComplete(MobEntity& /*mob*/) {
    return fleeTimer_ >= params_.duration;
}

void FleeGoal::stop(MobEntity& mob) {
    mob.setSpeedMultiplier(1.0f);
    mob.clearMoveTarget();
}

// ============================================================================
// LookAtPlayerGoal
// ============================================================================

bool LookAtPlayerGoal::canStart(MobEntity& mob) {
    auto* player = mob.senses().nearestPlayer();
    if (!player) return false;

    auto diff = player->position() - mob.position();
    float dist = static_cast<float>(std::sqrt(
        diff.x * diff.x + diff.y * diff.y + diff.z * diff.z));

    return dist <= params_.range;
}

void LookAtPlayerGoal::start(MobEntity& /*mob*/) {
    lookTimer_ = 0.0f;
}

void LookAtPlayerGoal::tick(MobEntity& mob, float dt) {
    lookTimer_ += dt;
    auto* player = mob.senses().nearestPlayer();
    if (player) {
        mob.lookAt(glm::dvec3(player->position()));
    }
}

bool LookAtPlayerGoal::isComplete(MobEntity& mob) {
    if (lookTimer_ >= params_.duration) return true;
    return mob.senses().nearestPlayer() == nullptr;
}

void LookAtPlayerGoal::stop(MobEntity& /*mob*/) {
}

// ============================================================================
// PanicGoal
// ============================================================================

bool PanicGoal::canStart(MobEntity& mob) {
    return mob.wasRecentlyDamaged(params_.damageMemory);
}

void PanicGoal::start(MobEntity& mob) {
    panicTimer_ = 0.0f;

    auto pos = mob.position();
    float dx = randomFloat(-params_.wanderRange, params_.wanderRange);
    float dz = randomFloat(-params_.wanderRange, params_.wanderRange);

    mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    mob.setSpeedMultiplier(params_.speedMult);
    mob.setAnimation(params_.animSlot);
}

void PanicGoal::tick(MobEntity& mob, float dt) {
    panicTimer_ += dt;

    // Pick new random direction if we've reached target
    if (!mob.hasMoveTarget() && panicTimer_ < params_.duration) {
        auto pos = mob.position();
        float dx = randomFloat(-params_.wanderRange, params_.wanderRange);
        float dz = randomFloat(-params_.wanderRange, params_.wanderRange);
        mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    }
}

bool PanicGoal::isComplete(MobEntity& /*mob*/) {
    return panicTimer_ >= params_.duration;
}

void PanicGoal::stop(MobEntity& mob) {
    mob.setSpeedMultiplier(1.0f);
    mob.clearMoveTarget();
}

}  // namespace finevox
