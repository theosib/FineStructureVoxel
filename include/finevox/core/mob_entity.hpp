#pragma once

/**
 * @file mob_entity.hpp
 * @brief MobEntity — Entity with AI, health, and combat
 *
 * Extends Entity with an AIBrain for behavior, EntitySenses for
 * awareness, health/damage for combat, and movement commands
 * for AI goals to use.
 */

#include "finevox/core/entity.hpp"
#include "finevox/core/entity_type_id.hpp"
#include "finevox/core/entity_type_def.hpp"
#include "finevox/core/ai_brain.hpp"
#include "finevox/core/entity_senses.hpp"

namespace finevox {

struct EntityTypeDef;
class EntityManager;

class MobEntity : public Entity {
public:
    MobEntity(EntityId id, EntityTypeId typeId);
    ~MobEntity() override = default;

    void tick(float dt, World& world) override;
    [[nodiscard]] std::string typeName() const override;

    // ========================================================================
    // Type
    // ========================================================================

    [[nodiscard]] EntityTypeId typeId() const { return typeId_; }
    [[nodiscard]] const EntityTypeDef* typeDef() const;

    // ========================================================================
    // AI
    // ========================================================================

    [[nodiscard]] AIBrain& brain() { return brain_; }
    [[nodiscard]] const AIBrain& brain() const { return brain_; }

    [[nodiscard]] EntitySenses& senses() { return senses_; }
    [[nodiscard]] const EntitySenses& senses() const { return senses_; }

    /// Set entity manager reference (for senses to scan entities)
    void setEntityManager(EntityManager* em) { entityManager_ = em; }

    // ========================================================================
    // Health & Combat
    // ========================================================================

    [[nodiscard]] float health() const { return health_; }
    [[nodiscard]] float maxHealth() const { return maxHealth_; }
    [[nodiscard]] bool isDead() const { return health_ <= 0.0f; }

    void setHealth(float hp);
    void setMaxHealth(float maxHp) { maxHealth_ = maxHp; }

    /// Apply damage from a source entity
    void damage(float amount, EntityId source = INVALID_ENTITY_ID);

    /// Heal the entity
    void heal(float amount);

    /// Get ID of last entity that damaged us (for aggro)
    [[nodiscard]] EntityId lastAttacker() const { return lastAttacker_; }

    /// Get time since last damage (seconds)
    [[nodiscard]] float timeSinceLastDamage() const { return timeSinceLastDamage_; }

    /// Check if mob was recently damaged
    [[nodiscard]] bool wasRecentlyDamaged(float withinSeconds = 5.0f) const {
        return timeSinceLastDamage_ < withinSeconds;
    }

    // ========================================================================
    // Movement Commands (called by AIGoals)
    // ========================================================================

    /// Set a target position for the mob to walk toward
    void moveTo(const glm::dvec3& target);

    /// Clear the movement target (stop walking)
    void clearMoveTarget();

    /// Check if mob has a movement target
    [[nodiscard]] bool hasMoveTarget() const { return hasMoveTarget_; }

    /// Get the current movement target
    [[nodiscard]] const glm::dvec3& moveTarget() const { return moveTarget_; }

    /// Turn to face a position
    void lookAt(const glm::dvec3& target);

    /// Jump (if on ground)
    void jump();

    /// Get movement speed multiplier (0-1, default 1)
    [[nodiscard]] float speedMultiplier() const { return speedMultiplier_; }
    void setSpeedMultiplier(float mult) { speedMultiplier_ = mult; }

private:
    EntityTypeId typeId_;
    AIBrain brain_;
    EntitySenses senses_;
    EntityManager* entityManager_ = nullptr;

    // Health
    float health_ = 20.0f;
    float maxHealth_ = 20.0f;
    EntityId lastAttacker_ = INVALID_ENTITY_ID;
    float timeSinceLastDamage_ = 999.0f;

    // Movement
    glm::dvec3 moveTarget_{0.0};
    bool hasMoveTarget_ = false;
    float speedMultiplier_ = 1.0f;

    /// Apply movement toward target
    void updateMovement(float dt);

    /// Initialize from type definition
    void initFromTypeDef();
};

/// Configure a MobEntity's AIBrain with preset goals based on AIType
void configureAIPreset(MobEntity& mob, AIType aiType);

}  // namespace finevox
