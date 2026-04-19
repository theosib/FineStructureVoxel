#pragma once

/**
 * @file item_drop_entity.hpp
 * @brief ItemDropEntity — dropped item in the world
 *
 * Design: Phase 13 Inventory & Items, Phase 25 Block-Drop Pipeline
 *
 * Represents an item stack floating in the world (dropped by players,
 * spawned by block breaking, etc.). Has a pickup delay before it can
 * be collected and a max age before it despawns.
 *
 * Each tick the drop queries EntityManager's spatial index for nearby
 * players: close enough → attempt pickup via the EntityManager-configured
 * ItemPickupCallback; mid-range → magnetism (accelerate toward player).
 */

#include "finevox/core/entity.hpp"
#include "finevox/core/item_stack.hpp"

namespace finevox {

class EntityManager;

class ItemDropEntity : public Entity {
public:
    ItemDropEntity(EntityId id, ItemStack item);
    ~ItemDropEntity() override = default;

    /// The item stack this entity represents
    [[nodiscard]] const ItemStack& item() const { return item_; }
    [[nodiscard]] ItemStack& item() { return item_; }

    /// Take the item (moves it out, leaves entity with empty stack — mark for removal after)
    ItemStack takeItem();

    /// Whether enough time has passed for this item to be picked up
    [[nodiscard]] bool isPickupable() const { return age_ >= pickupDelay_; }

    /// Time since this entity was created
    [[nodiscard]] float age() const { return age_; }

    void setPickupDelay(float seconds) { pickupDelay_ = seconds; }
    [[nodiscard]] float pickupDelay() const { return pickupDelay_; }

    void setMaxAge(float seconds) { maxAge_ = seconds; }
    [[nodiscard]] float maxAge() const { return maxAge_; }

    /// Attach the EntityManager so tick() can query for nearby players.
    /// Called automatically by EntityManager::spawnEntity.
    void setEntityManager(EntityManager* em) { entityManager_ = em; }

    // Pickup + magnetism tuning (defaults match design spec)
    void setPickupRadius(float r) { pickupRadius_ = r; }
    [[nodiscard]] float pickupRadius() const { return pickupRadius_; }

    void setAttractRadius(float r) { attractRadius_ = r; }
    [[nodiscard]] float attractRadius() const { return attractRadius_; }

    void setAttractAccel(float a) { attractAccel_ = a; }
    [[nodiscard]] float attractAccel() const { return attractAccel_; }

    void setMaxAttractSpeed(float s) { maxAttractSpeed_ = s; }
    [[nodiscard]] float maxAttractSpeed() const { return maxAttractSpeed_; }

    void tick(float dt, World& world) override;
    [[nodiscard]] std::string typeName() const override;

private:
    ItemStack item_;
    float pickupDelay_ = 0.5f;   // Seconds before pickup is allowed
    float maxAge_ = 300.0f;      // 5 minutes before despawn
    float age_ = 0.0f;

    EntityManager* entityManager_ = nullptr;
    float pickupRadius_ = 1.25f;
    float attractRadius_ = 3.0f;
    float attractAccel_ = 25.0f;    // m/s^2 toward player
    float maxAttractSpeed_ = 8.0f;  // clamp horizontal attract speed
};

}  // namespace finevox
