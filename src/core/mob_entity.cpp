#include "finevox/core/mob_entity.hpp"
#include "finevox/core/mob_event_hooks.hpp"
#include "finevox/core/ai_driver.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/entity_type_registry.hpp"
#include "finevox/core/entity_manager.hpp"
#include "finevox/core/ai_goals.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"
#include <cmath>
#include <algorithm>

namespace finevox {

MobEntity::MobEntity(EntityId id, EntityTypeId typeId)
    : Entity(id, EntityType::Custom)  // All mob entities use Custom type
    , typeId_(typeId)
{
    initFromTypeDef();
}

MobEntity::~MobEntity() = default;

void MobEntity::setDriver(std::unique_ptr<AIDriver> driver) {
    driver_ = std::move(driver);
}

void MobEntity::initFromTypeDef() {
    const auto* def = typeDef();
    if (!def) return;

    halfExtents_ = Vec3(def->halfExtents);
    hasGravity_ = def->hasGravity;
    health_ = def->maxHealth;
    maxHealth_ = def->maxHealth;
    senses_.setScanRange(def->followRange);

    // Set eye height to roughly head level
    eyeHeight_ = def->halfExtents.y * 2.0f * 0.85f;
}

const EntityTypeDef* MobEntity::typeDef() const {
    return EntityTypeRegistry::global().getType(typeId_);
}

std::string MobEntity::typeName() const {
    return std::string(typeId_.name());
}

uint8_t MobEntity::resolveAnimation(std::string_view name, uint8_t defaultSlot) const {
    const auto* def = typeDef();
    if (def) return def->resolveAnimation(name, defaultSlot);
    return defaultSlot;
}

void MobEntity::playAnimation(std::string_view name) {
    setAnimation(resolveAnimation(name, animationId_));
}

void MobEntity::tick(float dt, World& world) {
    // Landing detection — capture velocity before ground state changes
    if (!onGround_) {
        preLandingVelocityY_ = velocity_.y;
    }
    if (onGround_ && !wasOnGround_) {
        // Just landed — preLandingVelocityY_ holds the impact velocity
        // Fall damage is computed by scripts via preLandingVelocityY()
    }
    wasOnGround_ = onGround_;

    // Update timers
    timeSinceLastDamage_ += dt;
    animationTime_ += dt;

    // Apply fluid damage if in fluid
    if (isInFluid()) {
        BlockCoord feetBlock = toBlockCoord(position_);
        FluidTypeId fid = world.getFluid(feetBlock);
        if (!fid.isEmpty()) {
            const FluidType* ft = FluidRegistry::global().getType(fid);
            if (ft) {
                float dmg = ft->contactDamage;
                if (isSubmerged() && ft->submersionDamage > ft->contactDamage) {
                    dmg = ft->submersionDamage;
                }
                if (dmg > 0.0f) {
                    fluidDamageAccumulator_ += dmg * dt;
                    if (fluidDamageAccumulator_ >= 1.0f) {
                        int wholeDmg = static_cast<int>(fluidDamageAccumulator_);
                        damage(static_cast<float>(wholeDmg));
                        fluidDamageAccumulator_ -= static_cast<float>(wholeDmg);
                    }
                }
            }
        }
    }

    // Update senses
    if (entityManager_) {
        senses_.update(*this, *entityManager_, dt);
    }

    // Run AI brain
    brain_.tick(*this, dt);

    // Apply movement toward target
    updateMovement(dt);

    // Check for death
    if (isDead() && !deathHookFired_) {
        deathHookFired_ = true;
        if (eventHooks_) {
            eventHooks_->onDeath(*this, lastAttacker_);
        }
        markForRemoval();
    }
}

void MobEntity::updateMovement(float dt) {
    if (!hasMoveTarget_) {
        // Decelerate when no target
        velocity_.x *= 0.8f;
        velocity_.z *= 0.8f;
        return;
    }

    const auto* def = typeDef();
    float maxSpeed = def ? def->maxSpeed : 4.0f;
    maxSpeed *= speedMultiplier_;

    // Direction to target (XZ plane only)
    double dx = moveTarget_.x - position_.x;
    double dz = moveTarget_.z - position_.z;
    double distXZ = std::sqrt(dx * dx + dz * dz);

    if (distXZ < 0.5) {
        // Close enough — stop
        hasMoveTarget_ = false;
        velocity_.x *= 0.5f;
        velocity_.z *= 0.5f;
        return;
    }

    // Normalize and apply speed
    float dirX = static_cast<float>(dx / distXZ);
    float dirZ = static_cast<float>(dz / distXZ);

    velocity_.x = dirX * maxSpeed;
    velocity_.z = dirZ * maxSpeed;

    // Face movement direction
    yaw_ = std::atan2(dirX, dirZ);
}

void MobEntity::setHealth(float hp) {
    health_ = std::clamp(hp, 0.0f, maxHealth_);
}

void MobEntity::damage(float amount, EntityId source) {
    if (isDead()) return;
    health_ = std::max(0.0f, health_ - amount);
    lastAttacker_ = source;
    timeSinceLastDamage_ = 0.0f;

    if (eventHooks_) {
        eventHooks_->onDamage(*this, amount, source);
    }
}

void MobEntity::heal(float amount) {
    health_ = std::min(maxHealth_, health_ + amount);
}

void MobEntity::moveTo(const glm::dvec3& target) {
    moveTarget_ = target;
    hasMoveTarget_ = true;
}

void MobEntity::clearMoveTarget() {
    hasMoveTarget_ = false;
}

void MobEntity::lookAt(const glm::dvec3& target) {
    double dx = target.x - position_.x;
    double dz = target.z - position_.z;
    double dy = target.y - (position_.y + eyeHeight_);
    double distXZ = std::sqrt(dx * dx + dz * dz);

    yaw_ = static_cast<float>(std::atan2(dx, dz));
    pitch_ = static_cast<float>(-std::atan2(dy, distXZ));
}

void MobEntity::jump() {
    if (onGround_) {
        const auto* def = typeDef();
        float strength = def ? def->jumpStrength : 0.5f;
        velocity_.y = strength * 10.0f;  // Scale for physics
    }
}

// ============================================================================
// AI Preset Configuration
// ============================================================================

void configureAIPreset(MobEntity& mob, AIType aiType) {
    auto& brain = mob.brain();

    switch (aiType) {
        case AIType::Passive:
            brain.addGoal(0, std::make_unique<IdleGoal>(0));
            brain.addGoal(1, std::make_unique<WanderGoal>(1));
            brain.addGoal(2, std::make_unique<LookAtPlayerGoal>(2));
            brain.addGoal(8, std::make_unique<PanicGoal>(8));
            break;

        case AIType::Hostile:
            brain.addGoal(0, std::make_unique<IdleGoal>(0));
            brain.addGoal(1, std::make_unique<WanderGoal>(1));
            brain.addGoal(2, std::make_unique<LookAtPlayerGoal>(2));
            brain.addGoal(5, std::make_unique<ChaseGoal>(5));
            brain.addGoal(6, std::make_unique<AttackGoal>(6));
            break;

        case AIType::Neutral:
            brain.addGoal(0, std::make_unique<IdleGoal>(0));
            brain.addGoal(1, std::make_unique<WanderGoal>(1));
            brain.addGoal(2, std::make_unique<LookAtPlayerGoal>(2));
            brain.addGoal(5, std::make_unique<ChaseGoal>(5));
            brain.addGoal(6, std::make_unique<AttackGoal>(6));
            break;

        case AIType::None:
            // No built-in goals — script-only
            break;
    }
}

}  // namespace finevox
