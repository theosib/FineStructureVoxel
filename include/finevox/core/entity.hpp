#pragma once

/**
 * @file entity.hpp
 * @brief Entity base class and EntityType enumeration
 *
 * Design: [25-entity-system.md] §25.10 Entity Base Class
 */

#include "finevox/core/block_event.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/physics.hpp"
#include <string>

namespace finevox {

// Forward declarations
class World;

// ============================================================================
// EntityType - Categories of entities
// ============================================================================

/**
 * @brief Entity type enumeration
 *
 * Used for fast type checking and polymorphic dispatch.
 * Custom entity types from mods start at Custom.
 */
enum class EntityType : uint16_t {
    Player = 0,

    // Passive mobs
    Pig = 100,
    Cow,
    Sheep,
    Chicken,

    // Hostile mobs
    Zombie = 200,
    Skeleton,
    Spider,

    // Items and projectiles
    ItemDrop = 300,
    Arrow,
    Fireball,

    // Vehicles
    Minecart = 400,
    Boat,

    // Custom entity types start here
    Custom = 1000,
};

// ============================================================================
// Entity - Base class for all entities
// ============================================================================

/**
 * @brief Base entity class (game thread)
 *
 * Entities are non-block objects in the world: players, mobs, items, projectiles.
 * All entities have a position, velocity, bounding box, and tick behavior.
 *
 * Thread safety: Entity instances are owned by EntityManager on the game thread.
 * Graphics thread receives snapshots via GraphicsEventQueue.
 */
class Entity : public PhysicsBody {
public:
    Entity(EntityId id, EntityType type);
    virtual ~Entity() = default;

    // Non-copyable but movable
    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;
    Entity(Entity&&) = default;
    Entity& operator=(Entity&&) = default;

    // ========================================================================
    // Identity
    // ========================================================================

    [[nodiscard]] EntityId id() const { return id_; }
    void setId(EntityId id) { id_ = id; }
    [[nodiscard]] EntityType type() const { return type_; }

    /// Check if this entity is a player (overridden by MobEntity)
    [[nodiscard]] virtual bool isPlayerEntity() const {
        return type_ == EntityType::Player;
    }

    // Human-readable type name (for debugging)
    [[nodiscard]] virtual std::string typeName() const;

    // ========================================================================
    // PhysicsBody Interface (position, velocity, bounding box)
    // ========================================================================

    [[nodiscard]] Vec3 position() const override { return position_; }
    void setPosition(const Vec3& pos) override { position_ = pos; }

    [[nodiscard]] Vec3 velocity() const override { return velocity_; }
    void setVelocity(const Vec3& vel) override { velocity_ = vel; }

    [[nodiscard]] AABB boundingBox() const override {
        // Position is bottom-center of bounding box
        return AABB(
            position_.x - halfExtents_.x,
            position_.y,
            position_.z - halfExtents_.z,
            position_.x + halfExtents_.x,
            position_.y + halfExtents_.y * 2.0f,
            position_.z + halfExtents_.z
        );
    }

    [[nodiscard]] Vec3 halfExtents() const override { return halfExtents_; }
    void setHalfExtents(const Vec3& he) { halfExtents_ = he; }

    [[nodiscard]] bool isOnGround() const override { return onGround_; }
    void setOnGround(bool onGround) override { onGround_ = onGround; }

    [[nodiscard]] bool hasGravity() const override { return hasGravity_; }
    void setHasGravity(bool g) { hasGravity_ = g; }

    [[nodiscard]] float maxStepHeight() const override { return maxStepHeight_; }
    void setMaxStepHeight(float h) { maxStepHeight_ = h; }

    // ========================================================================
    // Look Direction
    // ========================================================================

    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    void setYaw(float y) { yaw_ = y; }
    void setPitch(float p) { pitch_ = p; }
    void setLook(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }

    // Get eye position (for first-person camera)
    [[nodiscard]] Vec3 eyePosition() const {
        return position_ + Vec3(0.0f, eyeHeight_, 0.0f);
    }

    [[nodiscard]] float eyeHeight() const { return eyeHeight_; }
    void setEyeHeight(float h) { eyeHeight_ = h; }

    // Get look direction as unit vector
    [[nodiscard]] Vec3 lookDirection() const;

    // ========================================================================
    // Animation State
    // ========================================================================

    [[nodiscard]] float animationTime() const { return animationTime_; }
    [[nodiscard]] uint8_t animationId() const { return animationId_; }

    void setAnimation(uint8_t id) {
        if (animationId_ != id) {
            animationId_ = id;
            animationTime_ = 0.0f;
        }
    }

    void advanceAnimation(float dt) { animationTime_ += dt; }

    // ========================================================================
    // Fluid State
    // ========================================================================

    [[nodiscard]] bool isInFluid() const { return inFluid_; }
    [[nodiscard]] bool isSubmerged() const { return isSubmerged_; }
    [[nodiscard]] float fluidSubmersion() const { return fluidSubmersion_; }
    [[nodiscard]] FluidTypeId inFluidType() const { return inFluidType_; }
    [[nodiscard]] virtual bool isAffectedByFluids() const { return true; }

    void setFluidState(bool inFluid, bool submerged, float submersion, FluidTypeId type) {
        inFluid_ = inFluid;
        isSubmerged_ = submerged;
        fluidSubmersion_ = submersion;
        inFluidType_ = type;
    }

    // ========================================================================
    // Lifecycle
    // ========================================================================

    /// Mark entity for removal (will be despawned next tick)
    void markForRemoval() { markedForRemoval_ = true; }
    [[nodiscard]] bool isMarkedForRemoval() const { return markedForRemoval_; }

    /// Check if entity is alive (not marked for removal)
    [[nodiscard]] bool isAlive() const { return !markedForRemoval_; }

    // ========================================================================
    // Tick Update
    // ========================================================================

    /**
     * @brief Update entity logic (called every game tick)
     *
     * Override in subclasses for entity-specific behavior (AI, timers, etc.)
     * Physics is applied separately by EntityManager.
     *
     * @param dt Delta time in seconds (typically 0.05 for 20 TPS)
     * @param world Reference to world for block queries
     */
    virtual void tick(float /*dt*/, World& /*world*/) {}

    // ========================================================================
    // Extra Data (per-entity DataContainer for inventory, custom state, etc.)
    // ========================================================================

    /// Get extra data for this entity (may be nullptr)
    [[nodiscard]] DataContainer* entityData() { return entityData_.get(); }
    [[nodiscard]] const DataContainer* entityData() const { return entityData_.get(); }

    /// Get or create extra data for this entity
    DataContainer& getOrCreateEntityData() {
        if (!entityData_) entityData_ = std::make_unique<DataContainer>();
        return *entityData_;
    }

    // ========================================================================
    // Subchunk Tracking
    // ========================================================================

    [[nodiscard]] ChunkPos currentChunk() const { return currentChunk_; }
    void setCurrentChunk(ChunkPos chunk) { currentChunk_ = chunk; }

protected:
    EntityId id_;
    EntityType type_;

    // Position/motion
    Vec3 position_{0.0f};
    Vec3 velocity_{0.0f};
    Vec3 halfExtents_{0.35f, 0.925f, 0.35f};  // Default player-like size

    // Ground state
    bool onGround_ = false;
    bool hasGravity_ = true;
    float maxStepHeight_ = MAX_STEP_HEIGHT;

    // Look direction
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float eyeHeight_ = 1.65f;  // Default player eye height

    // Animation
    float animationTime_ = 0.0f;
    uint8_t animationId_ = 0;

    // Lifecycle
    bool markedForRemoval_ = false;

    // Fluid state
    bool inFluid_ = false;
    bool isSubmerged_ = false;
    float fluidSubmersion_ = 0.0f;
    FluidTypeId inFluidType_;
    float fluidDamageAccumulator_ = 0.0f;

    // Subchunk tracking (for EntityManager)
    ChunkPos currentChunk_{0, 0, 0};

    // Per-entity extra data (inventory, custom state, etc.)
    std::unique_ptr<DataContainer> entityData_;
};

// ============================================================================
// Helper: Check if entity type is a player
// ============================================================================

inline bool isPlayer(EntityType type) {
    return type == EntityType::Player;
}

/// Check if an entity is a player (supports both plain Entity and MobEntity players)
inline bool isPlayer(const Entity& entity) {
    return entity.isPlayerEntity();
}

}  // namespace finevox
