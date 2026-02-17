#include "finevox/core/ai_goals.hpp"
#include "finevox/core/mob_entity.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
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
    idleDuration_ = randomFloat(2.0f, 5.0f);
    mob.clearMoveTarget();
    mob.setAnimation(0);  // Idle animation
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
    return randomFloat(0.0f, 1.0f) < 0.1f;  // 10% chance each tick
}

void WanderGoal::start(MobEntity& mob) {
    auto pos = mob.position();
    float dx = randomFloat(-wanderRange_, wanderRange_);
    float dz = randomFloat(-wanderRange_, wanderRange_);
    target_ = glm::dvec3(pos.x + dx, pos.y, pos.z + dz);

    mob.moveTo(target_);
    mob.setAnimation(1);  // Walk animation
    timeout_ = 0.0f;
}

void WanderGoal::tick(MobEntity& /*mob*/, float dt) {
    timeout_ += dt;
}

bool WanderGoal::isComplete(MobEntity& mob) {
    if (timeout_ >= MAX_WANDER_TIME) return true;
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
    repathTimer_ = 0.0f;
    mob.setAnimation(1);  // Walk/run animation
}

void ChaseGoal::tick(MobEntity& mob, float dt) {
    repathTimer_ += dt;

    if (targetId_ == INVALID_ENTITY_ID) return;

    // Find target entity through senses
    Entity* target = nullptr;
    for (auto* e : mob.senses().visibleEntities()) {
        if (e->id() == targetId_) {
            target = e;
            break;
        }
    }

    if (!target) return;

    // Re-path periodically
    if (repathTimer_ >= REPATH_INTERVAL || !mob.hasMoveTarget()) {
        repathTimer_ = 0.0f;
        mob.moveTo(glm::dvec3(target->position()));
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
    if (def && def->aiType == AIType::Neutral && !mob.wasRecentlyDamaged(10.0f)) {
        return true;
    }

    return false;
}

void ChaseGoal::stop(MobEntity& mob) {
    targetId_ = INVALID_ENTITY_ID;
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

    mob.setAnimation(2);  // Attack animation
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
        return dist > range * 1.5f;  // Some hysteresis
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
        return mob.wasRecentlyDamaged(2.0f);
    }
    return false;
}

void FleeGoal::start(MobEntity& mob) {
    fleeTimer_ = 0.0f;

    // Run in a random direction away from attacker (or random if no attacker)
    auto pos = mob.position();
    float dx = randomFloat(-fleeDistance_, fleeDistance_);
    float dz = randomFloat(-fleeDistance_, fleeDistance_);

    mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    mob.setSpeedMultiplier(1.5f);  // Run faster when fleeing
    mob.setAnimation(1);  // Walk/run animation
}

void FleeGoal::tick(MobEntity& /*mob*/, float dt) {
    fleeTimer_ += dt;
}

bool FleeGoal::isComplete(MobEntity& /*mob*/) {
    return fleeTimer_ >= FLEE_DURATION;
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

    return dist <= lookRange_;
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
    if (lookTimer_ >= LOOK_DURATION) return true;
    return mob.senses().nearestPlayer() == nullptr;
}

void LookAtPlayerGoal::stop(MobEntity& /*mob*/) {
}

// ============================================================================
// PanicGoal
// ============================================================================

bool PanicGoal::canStart(MobEntity& mob) {
    return mob.wasRecentlyDamaged(1.0f);
}

void PanicGoal::start(MobEntity& mob) {
    panicTimer_ = 0.0f;

    auto pos = mob.position();
    float dx = randomFloat(-12.0f, 12.0f);
    float dz = randomFloat(-12.0f, 12.0f);

    mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    mob.setSpeedMultiplier(1.5f);
    mob.setAnimation(1);  // Run
}

void PanicGoal::tick(MobEntity& mob, float dt) {
    panicTimer_ += dt;

    // Pick new random direction if we've reached target
    if (!mob.hasMoveTarget() && panicTimer_ < PANIC_DURATION) {
        auto pos = mob.position();
        float dx = randomFloat(-12.0f, 12.0f);
        float dz = randomFloat(-12.0f, 12.0f);
        mob.moveTo(glm::dvec3(pos.x + dx, pos.y, pos.z + dz));
    }
}

bool PanicGoal::isComplete(MobEntity& /*mob*/) {
    return panicTimer_ >= PANIC_DURATION;
}

void PanicGoal::stop(MobEntity& mob) {
    mob.setSpeedMultiplier(1.0f);
    mob.clearMoveTarget();
}

}  // namespace finevox
