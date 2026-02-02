#pragma once

/**
 * @file graphics_event_queue.hpp
 * @brief Event queue for game thread to graphics thread communication
 *
 * Design: [25-entity-system.md] §25.3 Graphics Event Queue
 */

#include "finevox/queue.hpp"
#include "finevox/block_event.hpp"
#include "finevox/entity.hpp"
#include "finevox/position.hpp"

#include <chrono>
#include <optional>
#include <vector>

namespace finevox {

// ============================================================================
// GraphicsEventType - Event categories for graphics thread
// ============================================================================

/**
 * @brief Types of events sent from game thread to graphics thread
 */
enum class GraphicsEventType : uint8_t {
    // Entity state (published every tick for visible entities)
    EntitySnapshot,      // Full state for interpolation
    EntitySpawn,         // New entity appeared
    EntityDespawn,       // Entity removed

    // Player corrections
    PlayerCorrection,    // Authority disagrees with prediction

    // World state corrections (for prediction rollback)
    BlockCorrection,     // Block state differs from what client expected

    // Effects
    PlaySound,           // Sound at position
    SpawnParticle,       // Particle effect

    // Animation
    EntityAnimation,     // Animation state change
};

// ============================================================================
// CorrectionReason - Why a player correction was issued
// ============================================================================

/**
 * @brief Reason for player correction
 *
 * Affects how graphics thread handles the correction (lerp vs snap).
 */
enum class CorrectionReason : uint8_t {
    PhysicsDivergence,  // Small drift, lerp to correct
    BlockChanged,       // World changed under player
    Knockback,          // Damage or explosion
    Teleport,           // Command or portal
    MobPush,            // Pushed by entity
    VehicleMove,        // Riding something that moved
};

// ============================================================================
// GraphicsEvent - Event sent from game thread to graphics thread
// ============================================================================

/**
 * @brief Event sent from game thread to graphics thread
 *
 * Serialization-ready structure for network transmission.
 * Fixed-size, POD-friendly for efficient batching.
 */
struct GraphicsEvent {
    GraphicsEventType type = GraphicsEventType::EntitySnapshot;
    uint64_t timestamp = 0;
    uint64_t tickNumber = 0;  // Game tick when this was generated

    // Entity identification
    EntityId entityId = INVALID_ENTITY_ID;
    uint16_t entityType = 0;  // EntityType as uint16_t for serialization

    // Position/motion (for snapshots and corrections)
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool onGround = false;

    // Animation state
    float animationTime = 0.0f;
    uint8_t animationId = 0;

    // Correction-specific
    uint64_t inputSequence = 0;
    CorrectionReason correctionReason = CorrectionReason::PhysicsDivergence;

    // Block correction
    int32_t blockX = 0, blockY = 0, blockZ = 0;
    uint32_t correctBlockType = 0;
    uint32_t expectedBlockType = 0;

    // ========================================================================
    // Helpers
    // ========================================================================

    [[nodiscard]] Vec3 position() const { return Vec3(posX, posY, posZ); }
    [[nodiscard]] Vec3 velocity() const { return Vec3(velX, velY, velZ); }
    [[nodiscard]] BlockPos blockPos() const { return BlockPos(blockX, blockY, blockZ); }

    void setPosition(Vec3 p) { posX = p.x; posY = p.y; posZ = p.z; }
    void setVelocity(Vec3 v) { velX = v.x; velY = v.y; velZ = v.z; }

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /**
     * @brief Create entity snapshot for interpolation
     */
    static GraphicsEvent entitySnapshot(const Entity& entity, uint64_t tick);

    /**
     * @brief Create entity spawn event
     */
    static GraphicsEvent entitySpawn(EntityId id, EntityType type,
                                      Vec3 pos, float yaw, float pitch);

    /**
     * @brief Create entity despawn event
     */
    static GraphicsEvent entityDespawn(EntityId id);

    /**
     * @brief Create player correction event
     */
    static GraphicsEvent playerCorrection(EntityId id, Vec3 pos, Vec3 vel,
                                           bool ground, uint64_t seq,
                                           CorrectionReason reason);

    /**
     * @brief Create block correction event
     */
    static GraphicsEvent blockCorrection(BlockPos pos, BlockTypeId correct,
                                          BlockTypeId expected);

    /**
     * @brief Create animation change event
     */
    static GraphicsEvent animation(EntityId id, uint8_t animId, float time);
};

// ============================================================================
// GraphicsEventQueue - Thread-safe queue for game→graphics events
// ============================================================================

/**
 * @brief Queue for game thread to graphics thread communication
 *
 * Uses Queue<GraphicsEvent> for unified queue semantics with:
 * - Internal CV for waitForWork() blocking
 * - Alarm support for timed wakeups
 * - WakeSignal attachment for multi-queue coordination
 *
 * Methods available:
 * - push(event) - Push single event
 * - pushBatch(vector<event>) - Push multiple events atomically
 * - tryPop() -> optional<event> - Non-blocking pop
 * - drainAll() -> vector<event> - Drain all events
 * - drainUpTo(n) -> vector<event> - Drain up to n events
 * - setAlarm(time), clearAlarm(), hasAlarm() - Alarm management
 * - waitForWork(), waitForWork(timeout) - Blocking wait
 * - attach(signal), detach() - WakeSignal for multi-queue wake
 * - shutdown(), isShutdown(), resetShutdown() - Lifecycle
 * - empty(), size(), clear() - Query operations
 */
using GraphicsEventQueue = Queue<GraphicsEvent>;

}  // namespace finevox
