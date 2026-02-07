#pragma once

/**
 * @file entity_manager.hpp
 * @brief EntityManager for game thread entity management
 *
 * Design: [25-entity-system.md] §25.6 Entity Manager
 */

#include "finevox/core/entity.hpp"
#include "finevox/core/graphics_event_queue.hpp"
#include "finevox/core/physics.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace finevox {

// Forward declarations
class World;
class UpdateScheduler;

// ============================================================================
// PlayerAuthority - Server-side tracking of player state for validation
// ============================================================================

/**
 * @brief Tracks authoritative player state for prediction validation
 *
 * When player events arrive from graphics thread, we update this state.
 * At the end of each tick, we compare against the entity and send
 * corrections if they've diverged too much.
 */
struct PlayerAuthority {
    EntityId playerId = INVALID_ENTITY_ID;
    Vec3 lastReceivedPosition{0.0f};
    Vec3 lastReceivedVelocity{0.0f};
    bool lastReceivedOnGround = false;
    uint64_t lastInputSequence = 0;

    // Threshold for sending corrections
    float correctionThreshold = 0.1f;  // 10 cm
};

// ============================================================================
// EntityManager - Manages all entities in the game thread
// ============================================================================

/**
 * @brief Manages all entities in the game thread
 *
 * Receives player events via UpdateScheduler (same path as block events).
 * Publishes entity state to GraphicsEventQueue for rendering.
 *
 * Thread safety: All methods should be called from the game thread only.
 * GraphicsEventQueue is the only thread-safe point of contact with graphics.
 */
class EntityManager {
public:
    EntityManager(World& world, GraphicsEventQueue& graphicsQueue);
    ~EntityManager();

    // Non-copyable, non-movable
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    EntityManager(EntityManager&&) = delete;
    EntityManager& operator=(EntityManager&&) = delete;

    // ========================================================================
    // Entity Lifecycle
    // ========================================================================

    /**
     * @brief Spawn a new entity
     * @param type Entity type
     * @param position Initial position
     * @return ID of the new entity
     */
    EntityId spawnEntity(EntityType type, Vec3 position);

    /**
     * @brief Spawn an entity with custom initialization
     * @param entity Pre-configured entity (takes ownership)
     * @return ID of the spawned entity
     */
    EntityId spawnEntity(std::unique_ptr<Entity> entity);

    /**
     * @brief Despawn an entity
     * @param id Entity to despawn
     * @return True if entity was found and despawned
     */
    bool despawnEntity(EntityId id);

    /**
     * @brief Get an entity by ID
     * @return Pointer to entity, or nullptr if not found
     */
    Entity* getEntity(EntityId id);
    const Entity* getEntity(EntityId id) const;

    /**
     * @brief Check if entity exists
     */
    bool hasEntity(EntityId id) const;

    /**
     * @brief Get all entities (for debugging/rendering)
     */
    const std::unordered_map<EntityId, std::unique_ptr<Entity>>& entities() const {
        return entities_;
    }

    /**
     * @brief Get entity count
     */
    size_t entityCount() const { return entities_.size(); }

    // ========================================================================
    // Player Management
    // ========================================================================

    /**
     * @brief Spawn a player entity
     * @param position Initial position
     * @return ID of the new player
     */
    EntityId spawnPlayer(Vec3 position);

    /**
     * @brief Get the local player entity (for single-player)
     * @return Player entity, or nullptr if no local player
     */
    Entity* getLocalPlayer();
    const Entity* getLocalPlayer() const;

    /**
     * @brief Set which entity is the local player
     */
    void setLocalPlayerId(EntityId id) { localPlayerId_ = id; }
    EntityId localPlayerId() const { return localPlayerId_; }

    // ========================================================================
    // Tick Processing
    // ========================================================================

    /**
     * @brief Process one game tick
     *
     * Called at fixed rate (typically 20 TPS).
     * Processes entity logic, physics, and publishes snapshots.
     *
     * @param tickDt Delta time in seconds (typically 0.05)
     */
    void tick(float tickDt);

    /**
     * @brief Get current game tick number
     */
    uint64_t currentTick() const { return currentTick_; }

    // ========================================================================
    // Player Event Handlers (called by UpdateScheduler)
    // ========================================================================

    /**
     * @brief Handle player position update from graphics thread
     */
    void handlePlayerPosition(const BlockEvent& event);

    /**
     * @brief Handle player look direction change
     */
    void handlePlayerLook(const BlockEvent& event);

    /**
     * @brief Handle player jump
     */
    void handlePlayerJump(const BlockEvent& event);

    /**
     * @brief Handle player sprint start/stop
     */
    void handlePlayerSprint(const BlockEvent& event, bool starting);

    /**
     * @brief Handle player sneak start/stop
     */
    void handlePlayerSneak(const BlockEvent& event, bool starting);

    // ========================================================================
    // Configuration
    // ========================================================================

    /**
     * @brief Set correction threshold (meters of drift before correction)
     */
    void setCorrectionThreshold(float threshold) { correctionThreshold_ = threshold; }
    float correctionThreshold() const { return correctionThreshold_; }

    /**
     * @brief Enable/disable player validation
     */
    void setValidationEnabled(bool enabled) { validationEnabled_ = enabled; }
    bool validationEnabled() const { return validationEnabled_; }

    // ========================================================================
    // Physics Access
    // ========================================================================

    PhysicsSystem& physics() { return physics_; }
    const PhysicsSystem& physics() const { return physics_; }

private:
    World& world_;
    GraphicsEventQueue& graphicsQueue_;
    PhysicsSystem physics_;

    // Entity storage
    std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
    EntityId nextEntityId_ = 1;
    uint64_t currentTick_ = 0;

    // Local player (for single-player)
    EntityId localPlayerId_ = INVALID_ENTITY_ID;

    // Player authority tracking (for validation)
    std::unordered_map<EntityId, PlayerAuthority> playerAuthorities_;

    // Configuration
    float correctionThreshold_ = 0.1f;  // 10 cm
    bool validationEnabled_ = true;

    // Entities pending removal (cleaned up at end of tick)
    std::vector<EntityId> pendingRemovals_;

    // ========================================================================
    // Internal Methods
    // ========================================================================

    /**
     * @brief Run physics for all entities
     */
    void physicsPass(float tickDt);

    /**
     * @brief Process entity chunk transfers
     */
    void processEntityTransfers();

    /**
     * @brief Validate player predictions and generate corrections
     */
    void validatePlayerPredictions();

    /**
     * @brief Publish entity snapshots to graphics queue
     */
    void publishSnapshots();

    /**
     * @brief Process pending entity removals
     */
    void processPendingRemovals();

    /**
     * @brief Create an entity of the given type
     */
    std::unique_ptr<Entity> createEntity(EntityType type, EntityId id);

    /**
     * @brief Get or create player authority tracking
     */
    PlayerAuthority& getPlayerAuthority(EntityId playerId);
};

// ============================================================================
// Event Handler Registration Helper
// ============================================================================

/**
 * @brief Register EntityManager handlers with UpdateScheduler
 *
 * Call this after creating EntityManager to wire up player events.
 *
 * @param scheduler The UpdateScheduler to register with
 * @param manager The EntityManager to receive events
 */
void registerEntityEventHandlers(UpdateScheduler& scheduler, EntityManager& manager);

}  // namespace finevox
