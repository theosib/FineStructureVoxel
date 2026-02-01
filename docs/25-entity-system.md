# 25. Entity System Design

[Back to Index](INDEX.md) | [Previous: Event System](24-event-system.md)

---

## 25.1 Overview

The entity system manages all non-block objects in the world: players, mobs, items, projectiles, vehicles, etc. A key design challenge is that **player input must feel instant** (graphics thread) while **game logic runs at a fixed tick rate** (game thread).

### Design Goals

- **Instant player response** - No input lag, smooth camera movement at frame rate
- **Fixed-rate game logic** - Deterministic physics, AI, and world interactions at 20 TPS
- **Message-based synchronization** - Clean separation enables future networking
- **Smooth entity rendering** - Interpolation between game ticks for all visible entities
- **Shared physics code** - Player and entities use the same collision/movement logic
- **Unified queue infrastructure** - Extend existing AlarmQueue/event system, not parallel infrastructure
- **Batching everywhere** - Efficient compute and network-ready message grouping
- **Serialization-ready** - Message structures designed for CBOR/network transmission

### Integration with Existing Systems

The entity system extends the existing event infrastructure (§24):

- **Graphics → Game**: Player events flow through the existing `UpdateScheduler.pushExternalEvent()` alongside block place/break events
- **Game → Graphics**: New `GraphicsEventQueue` using `AlarmQueue` for entity snapshots, corrections, animations
- **Both directions** support batching and the same wake semantics as other queues

### Current State: Direct World Access

Currently, the graphics thread accesses world contents directly for:
- **Raycasting** for block placement/breaking (determining which block to target)
- **Collision queries** for player prediction physics

```cpp
// Current pattern in render_demo.cpp:
RaycastResult result = raycastBlocks(origin, direction, 10.0f,
                                      RaycastMode::Interaction, shapeProvider);
if (result.hit) {
    world.breakBlock(result.blockPos);  // Direct world modification
}
```

This direct access will change with networking:
- **Single-player**: Direct access continues to work
- **Multiplayer client**: Raycast against local world copy, send request to server, wait for confirmation
- **Server**: Validates request, applies change, broadcasts to clients

The message-based architecture prepares for this transition while keeping current functionality working.

### Thread Model

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          Graphics Thread                                 │
│                                                                          │
│  ┌─────────────────────┐     ┌──────────────────┐                       │
│  │ PlayerPredicted     │     │ EntitySnapshots  │                       │
│  │ (runs every frame)  │     │ (interpolated)   │                       │
│  │ - instant input     │     │ - from game tick │                       │
│  │ - local physics     │     │ - smooth render  │                       │
│  └──────────┬──────────┘     └────────▲─────────┘                       │
│             │                         │                                  │
│             │ PlayerEvent             │ GraphicsEvent                    │
│             │ (batched)               │ (batched snapshots)              │
│             ▼                         │                                  │
└─────────────┼─────────────────────────┼─────────────────────────────────┘
              │                         │
    ┌─────────▼─────────────┐   ┌───────┴─────────────┐
    │  UpdateScheduler      │   │  GraphicsEventQueue │
    │  externalInput_       │   │  (AlarmQueue-based) │
    │  (existing, extended) │   │  (NEW)              │
    └─────────┬─────────────┘   └───────▲─────────────┘
              │                         │
┌─────────────┼─────────────────────────┼─────────────────────────────────┐
│             ▼                         │          Game Thread             │
│  ┌─────────────────────────────────────────────┐                        │
│  │ EntityManager                                │                        │
│  │ - Processes PlayerEvents from scheduler     │                        │
│  │ - PlayerAuthority (sync'd from graphics)    │                        │
│  │ - All other entities at fixed tick rate     │                        │
│  │ - Publishes snapshots to GraphicsEventQueue │                        │
│  └─────────────────────────────────────────────┘                        │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 25.2 Event Types (Extending BlockEvent)

Player-related events extend the existing `EventType` enum from §24.3. These flow through the same `UpdateScheduler` infrastructure as block events (place, break, use).

```cpp
namespace finevox {

// Extended EventType enum (add to existing in block_event.hpp)
enum class EventType : uint8_t {
    None = 0,

    // === Block lifecycle events (existing) ===
    BlockPlaced,        // Block was placed/replaced
    BlockBroken,        // Block is being broken/removed
    BlockChanged,       // Block state changed (rotation, data)

    // === Tick events (existing) ===
    TickScheduled,
    TickRepeat,
    TickRandom,

    // === Neighbor events (existing) ===
    NeighborChanged,

    // === Interaction events (existing, now unified) ===
    PlayerUse,          // Player right-clicked (chest, lever)
    PlayerHit,          // Player left-clicked

    // === Chunk events (existing) ===
    ChunkLoaded,
    ChunkUnloaded,
    RepaintRequested,

    // === Player events (NEW - from graphics thread) ===
    PlayerPosition,     // Position/velocity update from prediction
    PlayerLook,         // Yaw/pitch changed
    PlayerJump,         // Jump action (discrete)
    PlayerStartSprint,  // Sprint began
    PlayerStopSprint,   // Sprint ended
    PlayerStartSneak,   // Sneak began
    PlayerStopSneak,    // Sneak ended

    // Note: BlockPlaced/BlockBroken already handle place/break
    // Player just triggers them with additional playerId context
};

/// Unique entity identifier
using EntityId = uint64_t;

}  // namespace finevox
```

### PlayerEventData Structure

Player-specific data attached to BlockEvent. Designed for serialization (CBOR-friendly: fixed-size fields, no pointers).

```cpp
namespace finevox {

/**
 * @brief Player-specific event data
 *
 * Serialization-ready: all fixed-size POD fields.
 * Stored in BlockEvent's extra data or a dedicated field.
 */
struct PlayerEventData {
    EntityId playerId = 0;

    // Position/motion (for PlayerPosition events)
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f;
    bool onGround = false;

    // Look direction (for PlayerLook events)
    float yaw = 0.0f;
    float pitch = 0.0f;

    // Input sequence for reconciliation
    uint64_t inputSequence = 0;

    // Helpers
    Vec3 position() const { return Vec3(posX, posY, posZ); }
    Vec3 velocity() const { return Vec3(velX, velY, velZ); }
    void setPosition(Vec3 p) { posX = p.x; posY = p.y; posZ = p.z; }
    void setVelocity(Vec3 v) { velX = v.x; velY = v.y; velZ = v.z; }
};

// BlockEvent gets a new field or variant to hold player data
// Implementation options:
// 1. Add PlayerEventData playerData; to BlockEvent (wastes space for non-player events)
// 2. Use std::variant<std::monostate, PlayerEventData, ...> in BlockEvent
// 3. Store PlayerEventData in a parallel array indexed by event

}  // namespace finevox
```

### Factory Methods for Player Events

```cpp
// In BlockEvent or separate PlayerEvent helper
namespace finevox {

struct PlayerEvents {
    static BlockEvent position(EntityId id, Vec3 pos, Vec3 vel, bool ground, uint64_t seq) {
        BlockEvent e;
        e.type = EventType::PlayerPosition;
        e.timestamp = getCurrentTimestamp();
        // Populate player data...
        return e;
    }

    static BlockEvent look(EntityId id, float yaw, float pitch) {
        BlockEvent e;
        e.type = EventType::PlayerLook;
        // ...
        return e;
    }

    static BlockEvent jump(EntityId id) {
        BlockEvent e;
        e.type = EventType::PlayerJump;
        // ...
        return e;
    }

    // etc.
};

}  // namespace finevox
```

---

## 25.3 Graphics Event Queue (Game → Graphics)

A new queue for entity state updates from game thread to graphics thread. Uses `AlarmQueue` for consistent wake semantics with other queues (mesh rebuild, input, etc.).

```cpp
namespace finevox {

/// Event types sent from game thread to graphics thread
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

/// Reason for player correction (affects how graphics thread handles it)
enum class CorrectionReason : uint8_t {
    PhysicsDivergence,  // Small drift, lerp to correct
    BlockChanged,       // World changed under player
    Knockback,          // Damage or explosion
    Teleport,           // Command or portal
    MobPush,            // Pushed by entity
    VehicleMove,        // Riding something that moved
};

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
    EntityId entityId = 0;
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

    // Helpers
    Vec3 position() const { return Vec3(posX, posY, posZ); }
    Vec3 velocity() const { return Vec3(velX, velY, velZ); }
    BlockPos blockPos() const { return BlockPos(blockX, blockY, blockZ); }

    // Factory methods
    static GraphicsEvent entitySnapshot(const Entity& entity, uint64_t tick);
    static GraphicsEvent entitySpawn(EntityId id, EntityType type, Vec3 pos, float yaw, float pitch);
    static GraphicsEvent entityDespawn(EntityId id);
    static GraphicsEvent playerCorrection(EntityId id, Vec3 pos, Vec3 vel, bool ground,
                                           uint64_t seq, CorrectionReason reason);
    static GraphicsEvent blockCorrection(BlockPos pos, BlockTypeId correct, BlockTypeId expected);
    static GraphicsEvent animation(EntityId id, uint8_t animId, float time);
};

/**
 * @brief Queue for game thread to graphics thread communication
 *
 * Based on AlarmQueue for consistent wake semantics.
 * Supports batch operations for efficient network transmission.
 */
class GraphicsEventQueue {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    // ========================================================================
    // Batch Push (preferred for efficiency)
    // ========================================================================

    /**
     * @brief Push a batch of events atomically
     *
     * Takes lock once, wakes consumers once. Use for per-tick snapshot publishing.
     * Maps directly to a single network packet when networked.
     */
    void pushBatch(std::vector<GraphicsEvent> events) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& e : events) {
                queue_.push_back(std::move(e));
            }
        }
        condition_.notify_all();
    }

    /**
     * @brief Push single event (for immediate corrections)
     */
    void push(GraphicsEvent event) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(event));
        }
        condition_.notify_all();
    }

    // ========================================================================
    // Batch Pop (preferred for efficiency)
    // ========================================================================

    /**
     * @brief Drain all pending events (non-blocking)
     *
     * Returns all events for batch processing. Graphics thread calls once per frame.
     */
    std::vector<GraphicsEvent> drainAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<GraphicsEvent> result;
        result.reserve(queue_.size());
        while (!queue_.empty()) {
            result.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        return result;
    }

    /**
     * @brief Try to pop single event (non-blocking)
     */
    std::optional<GraphicsEvent> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        GraphicsEvent e = std::move(queue_.front());
        queue_.pop_front();
        return e;
    }

    // ========================================================================
    // Alarm Support (consistent with AlarmQueue)
    // ========================================================================

    void setAlarm(TimePoint wakeTime) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!alarmPending_ || wakeTime > alarmTime_) {
            alarmTime_ = wakeTime;
            alarmPending_ = true;
        }
        condition_.notify_all();
    }

    void clearAlarm() {
        std::lock_guard<std::mutex> lock(mutex_);
        alarmPending_ = false;
    }

    /**
     * @brief Wait for events or alarm (blocking)
     *
     * Same semantics as AlarmQueue::waitForWork().
     */
    bool waitForWork() {
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            if (shutdownFlag_) return false;
            if (!queue_.empty()) return true;

            if (alarmPending_) {
                auto status = condition_.wait_until(lock, alarmTime_);
                if (status == std::cv_status::timeout) {
                    alarmPending_ = false;
                    return true;
                }
            } else {
                condition_.wait(lock);
            }
        }
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdownFlag_ = true;
        }
        condition_.notify_all();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<GraphicsEvent> queue_;
    bool alarmPending_ = false;
    TimePoint alarmTime_;
    bool shutdownFlag_ = false;
};

}  // namespace finevox
```

---

## 25.4 Batching Strategy

Batching is critical for both compute efficiency and network throughput.

### Graphics → Game Batching

Player events are accumulated and sent in batches through `UpdateScheduler`:

```cpp
class PlayerPredicted {
    // Accumulate events between sends
    std::vector<BlockEvent> pendingEvents_;
    Clock::time_point lastSendTime_;

    void accumulateState(uint64_t seq) {
        auto now = Clock::now();
        // Throttle to ~20 Hz to match game tick rate
        if (now - lastSendTime_ < std::chrono::milliseconds(50)) {
            return;
        }
        lastSendTime_ = now;

        pendingEvents_.push_back(PlayerEvents::position(
            id_, body_.position(), body_.velocity(), body_.onGround(), seq));
    }

    void flushToScheduler() {
        if (pendingEvents_.empty()) return;

        // Single lock acquisition via existing batch API
        scheduler_.pushExternalEvents(std::move(pendingEvents_));
        pendingEvents_.clear();
    }
};
```

### Game → Graphics Batching

Entity snapshots are published in a single batch per tick:

```cpp
void EntityManager::publishSnapshots() {
    std::vector<GraphicsEvent> batch;
    batch.reserve(entities_.size());

    for (const auto& [id, entity] : entities_) {
        // Filter by visibility (real implementation checks view distance)
        batch.push_back(GraphicsEvent::entitySnapshot(*entity, currentTick_));
    }

    // One lock, one wake, maps to one network packet
    graphicsQueue_.pushBatch(std::move(batch));
}
```

### Network-Ready Batching

The batch structures map directly to network packets:

```cpp
// Future: compact network packet structure
struct EntitySnapshotPacket {
    uint64_t tickNumber;
    uint16_t entityCount;
    // Followed by entityCount * CompactEntitySnapshot

    struct CompactEntitySnapshot {
        uint32_t entityId;
        int16_t x, y, z;        // Position as fixed-point relative to chunk
        int8_t vx, vy, vz;      // Velocity as scaled int8
        uint8_t yaw, pitch;     // Angles as 0-255
        uint8_t flags;          // onGround, animationId packed
    };
    // ~16 bytes per entity vs ~80 bytes uncompressed
};
```

---

## 25.5 Player Prediction (Graphics Thread)

The graphics thread runs player physics every frame for instant response. Player events flow through the existing `UpdateScheduler` infrastructure.

```cpp
namespace finevox {

/**
 * @brief Player state predicted locally in graphics thread
 *
 * Runs physics every frame for instant input response.
 * Sends batched position updates to game thread via UpdateScheduler.
 */
class PlayerPredicted {
public:
    PlayerPredicted(EntityId id, PhysicsSystem& physics, UpdateScheduler& scheduler);

    /**
     * @brief Process input and update prediction (called every frame)
     */
    void update(float dt, const InputState& input) {
        uint64_t inputSeq = ++inputSequence_;

        // Apply input to movement
        Vec3 moveDir = calculateMoveDirection(input);
        float speed = sprinting_ ? SPRINT_SPEED : WALK_SPEED;

        body_.setVelocity(Vec3(
            moveDir.x * speed,
            body_.velocity().y,
            moveDir.z * speed
        ));

        // Handle jump
        if (input.jumpRequested && body_.onGround()) {
            body_.setVelocity(body_.velocity() + Vec3(0, JUMP_VELOCITY, 0));
            // Queue jump event
            pendingEvents_.push_back(PlayerEvents::jump(id_));
        }

        // Run physics
        physics_.step(body_, dt);

        // Update look direction
        yaw_ = input.yaw;
        pitch_ = input.pitch;

        // Accumulate state for batched send (throttled)
        accumulateState(inputSeq);

        // Save state for potential rollback
        saveInputState(inputSeq);
    }

    /**
     * @brief Flush accumulated events to scheduler
     *
     * Call once per frame or at throttled rate.
     */
    void flushToScheduler() {
        if (pendingEvents_.empty()) return;
        scheduler_.pushExternalEvents(std::move(pendingEvents_));
        pendingEvents_.clear();
    }

    /**
     * @brief Apply correction from game thread
     */
    void applyCorrection(const GraphicsEvent& correction) {
        switch (correction.correctionReason) {
            case CorrectionReason::Teleport:
            case CorrectionReason::Knockback:
                // Immediate snap for significant events
                body_.setPosition(correction.position());
                body_.setVelocity(correction.velocity());
                body_.setOnGround(correction.onGround);
                break;

            case CorrectionReason::PhysicsDivergence:
            case CorrectionReason::MobPush:
                // Smooth interpolation for small corrections
                pendingCorrection_ = correction;
                correctionAlpha_ = 0.0f;
                break;

            case CorrectionReason::BlockChanged:
                // Rollback and replay inputs
                rollbackAndReplay(correction.inputSequence, correction);
                break;
        }
    }

    // Accessors for rendering
    [[nodiscard]] Vec3 position() const { return body_.position(); }
    [[nodiscard]] Vec3 eyePosition() const {
        return body_.position() + Vec3(0, EYE_HEIGHT, 0);
    }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }

private:
    EntityId id_;
    SimplePhysicsBody body_;
    PhysicsSystem& physics_;
    UpdateScheduler& scheduler_;

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    bool sprinting_ = false;
    bool sneaking_ = false;

    uint64_t inputSequence_ = 0;

    // Batched events to send
    std::vector<BlockEvent> pendingEvents_;
    Clock::time_point lastSendTime_;

    // State history for rollback (circular buffer)
    struct InputSnapshot {
        uint64_t sequence;
        Vec3 position;
        Vec3 velocity;
        bool onGround;
    };
    std::array<InputSnapshot, 64> inputHistory_;
    size_t historyHead_ = 0;

    // Pending smooth correction
    std::optional<GraphicsEvent> pendingCorrection_;
    float correctionAlpha_ = 1.0f;

    void accumulateState(uint64_t seq);
    void saveInputState(uint64_t seq);
    void rollbackAndReplay(uint64_t fromSeq, const GraphicsEvent& correction);
};

}  // namespace finevox
```

---

## 25.6 Entity Manager (Game Thread)

The game thread processes all entities at a fixed tick rate. Player events come through the existing `UpdateScheduler`, and entity state is published to `GraphicsEventQueue`.

```cpp
namespace finevox {

/**
 * @brief Manages all entities in the game thread
 *
 * Receives player events via UpdateScheduler (same path as block events).
 * Publishes entity state to GraphicsEventQueue for rendering.
 */
class EntityManager {
public:
    EntityManager(World& world, UpdateScheduler& scheduler, GraphicsEventQueue& graphicsQueue);

    /**
     * @brief Process one game tick (called at fixed 20 TPS)
     */
    void tick(float tickDt) {
        ++currentTick_;

        // Note: Player events are processed by UpdateScheduler
        // which calls our handlers for PlayerPosition, PlayerJump, etc.

        // 1. Update all entities (AI, animations)
        for (auto& [id, entity] : entities_) {
            entity->tick(tickDt, world_);
        }

        // 2. Run physics for all entities
        physicsPass(tickDt);

        // 3. Handle subchunk transfers
        processEntityTransfers();

        // 4. Validate player predictions, generate corrections if needed
        validatePlayerPredictions();

        // 5. Publish snapshots to graphics thread (batched)
        publishSnapshots();
    }

    // ========================================================================
    // Player Event Handlers (called by UpdateScheduler)
    // ========================================================================

    void handlePlayerPosition(const BlockEvent& event);
    void handlePlayerLook(const BlockEvent& event);
    void handlePlayerJump(const BlockEvent& event);
    void handlePlayerSprint(const BlockEvent& event, bool starting);
    void handlePlayerSneak(const BlockEvent& event, bool starting);

    // Entity lifecycle
    EntityId spawnEntity(EntityType type, Vec3 position);
    void despawnEntity(EntityId id);
    Entity* getEntity(EntityId id);

private:
    World& world_;
    UpdateScheduler& scheduler_;
    GraphicsEventQueue& graphicsQueue_;
    PhysicsSystem physics_;

    std::unordered_map<EntityId, std::unique_ptr<Entity>> entities_;
    EntityId nextEntityId_ = 1;
    uint64_t currentTick_ = 0;

    // Player authority state (for validation)
    struct PlayerAuthority {
        EntityId id;
        Vec3 lastReceivedPosition;
        Vec3 lastReceivedVelocity;
        uint64_t lastInputSequence;
    };
    std::unordered_map<EntityId, PlayerAuthority> playerAuthorities_;

    void physicsPass(float tickDt);
    void processEntityTransfers();
    void validatePlayerPredictions();
    void publishSnapshots();
};

void EntityManager::handlePlayerPosition(const BlockEvent& event) {
    // Extract player data from event
    EntityId playerId = event.playerData.playerId;
    Vec3 pos = event.playerData.position();
    Vec3 vel = event.playerData.velocity();
    bool ground = event.playerData.onGround;
    uint64_t seq = event.playerData.inputSequence;

    // Update authority tracking
    auto& auth = playerAuthorities_[playerId];
    auth.lastReceivedPosition = pos;
    auth.lastReceivedVelocity = vel;
    auth.lastInputSequence = seq;

    // Update player entity
    if (auto* player = getEntity(playerId)) {
        player->setPosition(pos);
        player->setVelocity(vel);
        player->setOnGround(ground);
    }
}

void EntityManager::validatePlayerPredictions() {
    for (auto& [playerId, auth] : playerAuthorities_) {
        auto* player = getEntity(playerId);
        if (!player) continue;

        Vec3 posError = player->position() - auth.lastReceivedPosition;
        float errorMagnitude = glm::length(posError);

        constexpr float CORRECTION_THRESHOLD = 0.1f;  // 10 cm

        if (errorMagnitude > CORRECTION_THRESHOLD) {
            graphicsQueue_.push(GraphicsEvent::playerCorrection(
                playerId,
                player->position(),
                player->velocity(),
                player->onGround(),
                auth.lastInputSequence,
                CorrectionReason::PhysicsDivergence
            ));
        }
    }
}

void EntityManager::publishSnapshots() {
    std::vector<GraphicsEvent> batch;
    batch.reserve(entities_.size());

    for (const auto& [id, entity] : entities_) {
        batch.push_back(GraphicsEvent::entitySnapshot(*entity, currentTick_));
    }

    graphicsQueue_.pushBatch(std::move(batch));
}

}  // namespace finevox
```

---

## 25.7 Entity Interpolation (Graphics Thread)

The graphics thread interpolates between game tick snapshots for smooth rendering.

```cpp
namespace finevox {

/**
 * @brief Manages entity snapshots for interpolation
 *
 * Receives batched snapshots from GraphicsEventQueue.
 * Provides interpolated state for rendering at any frame time.
 */
class EntityInterpolator {
public:
    /**
     * @brief Process batch of graphics events (call once per frame)
     */
    void processBatch(const std::vector<GraphicsEvent>& events) {
        for (const auto& event : events) {
            switch (event.type) {
                case GraphicsEventType::EntitySnapshot:
                    receiveSnapshot(event);
                    break;
                case GraphicsEventType::EntitySpawn:
                    onEntitySpawn(event);
                    break;
                case GraphicsEventType::EntityDespawn:
                    onEntityDespawn(event.entityId);
                    break;
                default:
                    break;
            }
        }
    }

    /**
     * @brief Get interpolated state for rendering
     */
    std::optional<EntityRenderState> getInterpolated(EntityId id, double renderTime) const;

private:
    struct EntityHistory {
        std::deque<GraphicsEvent> snapshots;
        uint16_t entityType;
    };

    std::unordered_map<EntityId, EntityHistory> entityHistory_;

    static constexpr size_t MAX_SNAPSHOTS = 4;
    static constexpr double INTERPOLATION_DELAY = 0.1;  // 100ms behind real time
    static constexpr double TICK_DURATION = 0.05;       // 50ms per tick

    void receiveSnapshot(const GraphicsEvent& snapshot);
    void onEntitySpawn(const GraphicsEvent& spawn);
    void onEntityDespawn(EntityId id);
    double tickToTime(uint64_t tick) const { return tick * TICK_DURATION; }
};

/**
 * @brief Interpolated entity state for rendering
 */
struct EntityRenderState {
    Vec3 position;
    float yaw;
    float pitch;
    float animationTime;
    uint8_t animationId;
    EntityType type;

    static EntityRenderState lerp(const GraphicsEvent& a, const GraphicsEvent& b, float alpha);

private:
    static float lerpAngle(float a, float b, float t) {
        float diff = std::fmod(b - a + 540.0f, 360.0f) - 180.0f;
        return a + diff * t;
    }
};

}  // namespace finevox
```

---

## 25.8 Multi-Queue Waiting (Future: Animation Thread)

With enough animation work, a dedicated animation thread may be needed. The infrastructure supports waiting on multiple queues:

```cpp
namespace finevox {

/**
 * @brief Wait on multiple AlarmQueue-compatible queues
 *
 * For threads that need to consume from multiple sources.
 */
class MultiQueueWaiter {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    /**
     * @brief Check function type - returns true if queue has work
     */
    using CheckFunc = std::function<bool()>;

    /**
     * @brief Register a queue for combined waiting
     */
    void addQueue(std::condition_variable* cv, std::mutex* mutex, CheckFunc hasWork);

    /**
     * @brief Wait until any registered queue has work
     *
     * Returns false on shutdown.
     */
    bool wait();

    void shutdown();

private:
    struct QueueEntry {
        std::condition_variable* cv;
        std::mutex* mutex;
        CheckFunc hasWork;
    };
    std::vector<QueueEntry> queues_;
    std::atomic<bool> shutdownFlag_{false};
};

}  // namespace finevox
```

For simpler cases, graphics thread can just poll multiple queues per frame without blocking.

---

## 25.9 Network Architecture Mapping

The message-based architecture maps directly to client/server:

| Local (Single-Player) | Networked (Multiplayer) |
|-----------------------|------------------------|
| `UpdateScheduler.pushExternalEvent()` | Client → Server packet |
| `GraphicsEventQueue.push()` | Server → Client packet |
| `PlayerPredicted` | Client-side prediction |
| `PlayerAuthority` | Server-side authority |
| `EntityInterpolator` | Client-side interpolation |
| `EntityManager` | Server entity simulation |
| Batch push/drain | Single network packet |

### Example Network Packet Flow

```
Client                              Server
──────                              ──────
Input events accumulate
    │
    ▼
PlayerPredicted.flushToScheduler()
    │
    ├─► Immediate local response
    │
    └─► Batch PlayerEvents ─────────────► Receive batch, process
                                              │
                                              ▼
                                         EntityManager.tick()
                                              │
                                              ├─► Handle player events
                                              ├─► Validate predictions
                                              │
                                              ▼
                                         publishSnapshots()
                                              │
        ◄──────── Batch GraphicsEvents ───────┘
    │
    ▼
EntityInterpolator.processBatch()
PlayerPredicted.applyCorrection() (if any)
```

---

## 25.10 Entity Base Class

Shared between game thread entities and used by physics system.

```cpp
namespace finevox {

/// Entity type enumeration
enum class EntityType : uint16_t {
    Player,

    // Passive mobs
    Pig, Cow, Sheep, Chicken,

    // Hostile mobs
    Zombie, Skeleton, Creeper, Spider,

    // Items and projectiles
    ItemDrop, Arrow, Fireball,

    // Vehicles
    Minecart, Boat,
};

/// Base entity class (game thread)
class Entity {
public:
    Entity(EntityId id, EntityType type);
    virtual ~Entity() = default;

    // Identity
    [[nodiscard]] EntityId id() const { return id_; }
    [[nodiscard]] EntityType type() const { return type_; }

    // Position and motion
    [[nodiscard]] Vec3 position() const { return position_; }
    [[nodiscard]] Vec3 velocity() const { return velocity_; }
    [[nodiscard]] float yaw() const { return yaw_; }
    [[nodiscard]] float pitch() const { return pitch_; }
    [[nodiscard]] bool onGround() const { return onGround_; }

    void setPosition(Vec3 pos) { position_ = pos; }
    void setVelocity(Vec3 vel) { velocity_ = vel; }
    void setYaw(float y) { yaw_ = y; }
    void setPitch(float p) { pitch_ = p; }
    void setOnGround(bool g) { onGround_ = g; }

    // Physics body access
    [[nodiscard]] virtual AABB boundingBox() const;
    [[nodiscard]] Vec3 halfExtents() const { return halfExtents_; }

    // Tick update (override in subclasses)
    virtual void tick(float dt, World& world) {}

    // Animation
    [[nodiscard]] float animationTime() const { return animationTime_; }
    [[nodiscard]] uint8_t animationId() const { return animationId_; }
    void setAnimation(uint8_t id);

    // Physics properties
    [[nodiscard]] virtual float maxStepHeight() const { return 0.625f; }
    [[nodiscard]] virtual bool hasGravity() const { return true; }
    [[nodiscard]] virtual float gravityMultiplier() const { return 1.0f; }

protected:
    EntityId id_;
    EntityType type_;

    Vec3 position_{0.0f};
    Vec3 velocity_{0.0f};
    Vec3 halfExtents_{0.3f, 0.9f, 0.3f};

    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    bool onGround_ = false;

    float animationTime_ = 0.0f;
    uint8_t animationId_ = 0;
};

}  // namespace finevox
```

---

## 25.11 Configuration

```cpp
namespace finevox {

struct EntityConfig {
    // Tick rate: uses world's TickConfig (no duplication)

    // Prediction settings
    float correctionThreshold = 0.1f;           // Meters of drift before correction
    float correctionLerpSpeed = 10.0f;          // Speed of smooth corrections
    size_t inputHistorySize = 64;               // Inputs to keep for rollback

    // Interpolation settings
    std::chrono::milliseconds interpolationDelay{100};  // Render delay
    size_t snapshotHistorySize = 4;             // Snapshots per entity

    // Batching
    std::chrono::milliseconds positionSendRate{50};  // Throttle player updates
    size_t maxBatchSize = 256;                  // Max events per batch

    // Physics: uses PhysicsSystem's config (no duplication)

    // Entity limits
    size_t maxEntitiesPerChunk = 50;
    size_t maxTotalEntities = 10000;
};

}  // namespace finevox
```

---

## 25.12 Implementation Phases

### Phase 1: Core Infrastructure
- [ ] Extend EventType with player event types
- [ ] Add PlayerEventData to BlockEvent
- [ ] Implement GraphicsEventQueue (AlarmQueue-based)
- [ ] Register player event handlers in UpdateScheduler

### Phase 2: Player Prediction
- [ ] PlayerPredicted class with batched event sending
- [ ] Integration with existing PhysicsSystem
- [ ] Input history for rollback

### Phase 3: Entity Manager
- [ ] EntityManager with entity lifecycle
- [ ] Player event handlers
- [ ] Batched snapshot publishing

### Phase 4: Entity Interpolation
- [ ] EntityInterpolator with snapshot history
- [ ] Smooth interpolation for rendering
- [ ] Graphics thread integration

### Phase 5: Corrections and Reconciliation
- [ ] Prediction validation
- [ ] Correction messages
- [ ] Rollback and replay
- [ ] Block correction for world changes

### Phase 6: Mob AI (Future)
- [ ] Pathfinding integration
- [ ] Basic AI behaviors
- [ ] Mob types

---

## See Also

- [08 - Physics](08-physics.md) - Collision detection and step-climbing
- [14 - Threading](14-threading.md) - Thread model overview
- [24 - Event System](24-event-system.md) - Block event processing, UpdateScheduler

---

[Next: Network Protocol](26-network-protocol.md)
