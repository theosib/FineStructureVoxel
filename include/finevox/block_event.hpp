#pragma once

/**
 * @file block_event.hpp
 * @brief Event types and BlockEvent data structure
 *
 * Design: [24-event-system.md] §24.2 BlockEvent
 */

#include "finevox/position.hpp"
#include "finevox/rotation.hpp"
#include "finevox/string_interner.hpp"
#include <cstdint>
#include <glm/glm.hpp>

// Use glm::vec3 for helper methods
using Vec3 = glm::vec3;

namespace finevox {

// Forward declaration
enum class TickType : uint8_t;

/// Unique entity identifier
using EntityId = uint64_t;

/// Invalid entity ID constant
constexpr EntityId INVALID_ENTITY_ID = 0;

// ============================================================================
// EventType - Types of block-related events
// ============================================================================

/**
 * @brief Types of events that can be processed by the event system
 */
enum class EventType : uint8_t {
    None = 0,

    // Block lifecycle events
    BlockPlaced,        // Block was placed/replaced in the world
    BlockBroken,        // Block is being broken/removed
    BlockChanged,       // Block state changed (rotation, data)

    // Tick events
    TickGame,           // Regular game tick (for registered blocks)
    TickScheduled,      // Scheduled tick fired
    TickRepeat,         // Repeating tick fired
    TickRandom,         // Random tick fired

    // Neighbor events
    NeighborChanged,    // Adjacent block changed
    BlockUpdate,        // Block should re-evaluate state (redstone-like propagation)

    // Interaction events (block-targeted)
    PlayerUse,          // Player right-clicked a block
    PlayerHit,          // Player left-clicked a block

    // Player state events (from graphics thread)
    PlayerPosition,     // Position/velocity update from prediction
    PlayerLook,         // Yaw/pitch changed
    PlayerJump,         // Jump action (discrete)
    PlayerStartSprint,  // Sprint began
    PlayerStopSprint,   // Sprint ended
    PlayerStartSneak,   // Sneak began
    PlayerStopSneak,    // Sneak ended

    // Chunk events
    ChunkLoaded,        // Chunk was loaded
    ChunkUnloaded,      // Chunk is being unloaded

    // Visual events
    RepaintRequested,   // Block needs visual update
};

// ============================================================================
// PlayerEventData - Player-specific event data for entity events
// ============================================================================

/**
 * @brief Player-specific event data
 *
 * Serialization-ready: all fixed-size POD fields.
 * Used with PlayerPosition, PlayerLook, PlayerJump, etc. event types.
 */
struct PlayerEventData {
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
    [[nodiscard]] Vec3 position() const { return Vec3(posX, posY, posZ); }
    [[nodiscard]] Vec3 velocity() const { return Vec3(velX, velY, velZ); }
    void setPosition(Vec3 p) { posX = p.x; posY = p.y; posZ = p.z; }
    void setVelocity(Vec3 v) { velX = v.x; velY = v.y; velZ = v.z; }
};

// ============================================================================
// BlockEvent - Unified event container for block-related events
// ============================================================================

/**
 * @brief Unified event container for block-related events
 *
 * Contains all data needed for any event type. Unused fields default
 * to "no value" sentinels to avoid unnecessary copying.
 *
 * Thread safety: Read-only after construction. Safe to pass between threads.
 *
 * Size: ~64 bytes, fits in a cache line.
 */
struct BlockEvent {
    // Event identification
    EventType type = EventType::None;

    // Location (always valid)
    BlockPos pos{0, 0, 0};
    LocalBlockPos localPos{0, 0, 0};  // Position within subchunk
    ChunkPos chunkPos{0, 0, 0};

    // Block information (valid for block events)
    BlockTypeId blockType;        // Current/new block type
    BlockTypeId previousType;     // Previous block type
    Rotation rotation;            // Block rotation (if applicable)

    // Interaction data (valid for PlayerUse/PlayerHit)
    Face face = Face::PosY;       // Which face was interacted with

    // For NeighborChanged (supports consolidation via bitmask)
    Face changedFace = Face::PosY;  // Primary face that changed (for single-face events)
    uint8_t neighborFaceMask = 0;   // Bitmask of all changed faces (1 << Face value)

    // For tick events
    TickType tickType;  // Initialized in factory methods

    // Timestamp (for ordering and debugging)
    uint64_t timestamp = 0;

    // Entity data (for player/entity events)
    EntityId entityId = INVALID_ENTITY_ID;  // Which entity triggered this event
    PlayerEventData playerData;              // Player-specific data (for player events)

    // ========================================================================
    // Factory Methods
    // ========================================================================

    /**
     * @brief Create a block placed event
     * @param pos World position
     * @param newType Type of block being placed
     * @param oldType Type of block being replaced (usually air)
     * @param rot Rotation of the new block
     */
    static BlockEvent blockPlaced(BlockPos pos, BlockTypeId newType,
                                  BlockTypeId oldType, Rotation rot = Rotation::IDENTITY);

    /**
     * @brief Create a block broken event
     * @param pos World position
     * @param oldType Type of block being broken
     */
    static BlockEvent blockBroken(BlockPos pos, BlockTypeId oldType);

    /**
     * @brief Create a block changed event (state change, not place/break)
     * @param pos World position
     * @param oldType Previous block type
     * @param newType New block type
     */
    static BlockEvent blockChanged(BlockPos pos, BlockTypeId oldType, BlockTypeId newType);

    /**
     * @brief Create a neighbor changed event
     * @param pos World position of the block being notified
     * @param changedFace Which face's neighbor changed
     */
    static BlockEvent neighborChanged(BlockPos pos, Face changedFace);

    /**
     * @brief Create a tick event
     * @param pos World position
     * @param tickType Type of tick (Scheduled, Repeat, or Random)
     */
    static BlockEvent tick(BlockPos pos, TickType tickType);

    /**
     * @brief Create a player use (right-click) event
     * @param pos World position
     * @param face Which face was clicked
     */
    static BlockEvent playerUse(BlockPos pos, Face face);

    /**
     * @brief Create a player hit (left-click) event
     * @param pos World position
     * @param face Which face was clicked
     */
    static BlockEvent playerHit(BlockPos pos, Face face);

    /**
     * @brief Create a block update event (redstone-like propagation)
     *
     * Used by handlers to notify a block that it should re-evaluate its state.
     * Unlike NeighborChanged, this doesn't specify which neighbor changed.
     *
     * @param pos World position of block to update
     */
    static BlockEvent blockUpdate(BlockPos pos);

    // ========================================================================
    // Player Event Factory Methods
    // ========================================================================

    /**
     * @brief Create a player position update event
     * @param id Entity ID of the player
     * @param position Current position
     * @param velocity Current velocity
     * @param onGround Whether player is on ground
     * @param inputSequence Input sequence number for reconciliation
     */
    static BlockEvent playerPosition(EntityId id, Vec3 position, Vec3 velocity,
                                      bool onGround, uint64_t inputSequence);

    /**
     * @brief Create a player look direction event
     * @param id Entity ID of the player
     * @param yaw Yaw angle in degrees
     * @param pitch Pitch angle in degrees
     */
    static BlockEvent playerLook(EntityId id, float yaw, float pitch);

    /**
     * @brief Create a player jump event
     * @param id Entity ID of the player
     */
    static BlockEvent playerJump(EntityId id);

    /**
     * @brief Create a player sprint start/stop event
     * @param id Entity ID of the player
     * @param starting True if starting sprint, false if stopping
     */
    static BlockEvent playerSprint(EntityId id, bool starting);

    /**
     * @brief Create a player sneak start/stop event
     * @param id Entity ID of the player
     * @param starting True if starting sneak, false if stopping
     */
    static BlockEvent playerSneak(EntityId id, bool starting);

    // ========================================================================
    // Sentinel Checks
    // ========================================================================

    /**
     * @brief Check if blockType field is valid
     */
    [[nodiscard]] bool hasBlockType() const { return blockType.isValid(); }

    /**
     * @brief Check if previousType field is valid
     */
    [[nodiscard]] bool hasPreviousType() const { return previousType.isValid(); }

    /**
     * @brief Check if this is a block lifecycle event (place/break/change)
     */
    [[nodiscard]] bool isBlockEvent() const {
        return type == EventType::BlockPlaced ||
               type == EventType::BlockBroken ||
               type == EventType::BlockChanged;
    }

    /**
     * @brief Check if this is a player block interaction event
     */
    [[nodiscard]] bool isBlockInteractionEvent() const {
        return type == EventType::PlayerUse || type == EventType::PlayerHit;
    }

    /**
     * @brief Check if this is a player state event (from graphics thread)
     */
    [[nodiscard]] bool isPlayerStateEvent() const {
        return type == EventType::PlayerPosition ||
               type == EventType::PlayerLook ||
               type == EventType::PlayerJump ||
               type == EventType::PlayerStartSprint ||
               type == EventType::PlayerStopSprint ||
               type == EventType::PlayerStartSneak ||
               type == EventType::PlayerStopSneak;
    }

    /**
     * @brief Check if this is any player-related event
     */
    [[nodiscard]] bool isPlayerEvent() const {
        return isBlockInteractionEvent() || isPlayerStateEvent();
    }

    /**
     * @brief Check if this event has a valid entity ID
     */
    [[nodiscard]] bool hasEntityId() const {
        return entityId != INVALID_ENTITY_ID;
    }

    /**
     * @brief Check if this is a tick event
     */
    [[nodiscard]] bool isTickEvent() const {
        return type == EventType::TickGame ||
               type == EventType::TickScheduled ||
               type == EventType::TickRepeat ||
               type == EventType::TickRandom;
    }

    /**
     * @brief Check if this is a neighbor/update event
     */
    [[nodiscard]] bool isNeighborEvent() const {
        return type == EventType::NeighborChanged || type == EventType::BlockUpdate;
    }

    /**
     * @brief Check if this event is valid (has a type)
     */
    [[nodiscard]] bool isValid() const { return type != EventType::None; }

    // ========================================================================
    // Face Mask Helpers (for NeighborChanged consolidation)
    // ========================================================================

    /**
     * @brief Check if a specific neighbor face is marked as changed
     */
    [[nodiscard]] bool hasNeighborChanged(Face f) const {
        return neighborFaceMask & (1 << static_cast<uint8_t>(f));
    }

    /**
     * @brief Add a face to the neighbor change mask
     */
    void addNeighborFace(Face f) {
        neighborFaceMask |= (1 << static_cast<uint8_t>(f));
    }

    /**
     * @brief Iterate over all changed neighbor faces
     * @param func Callback called for each changed face
     */
    template<typename Func>
    void forEachChangedNeighbor(Func&& func) const {
        for (int i = 0; i < 6; ++i) {
            if (neighborFaceMask & (1 << i)) {
                func(static_cast<Face>(i));
            }
        }
    }

    /**
     * @brief Get count of changed neighbor faces
     */
    [[nodiscard]] int changedNeighborCount() const {
        int count = 0;
        for (int i = 0; i < 6; ++i) {
            if (neighborFaceMask & (1 << i)) ++count;
        }
        return count;
    }
};

// ============================================================================
// TickConfig - Configuration for game tick and random tick behavior
// ============================================================================

/**
 * @brief Configuration for the tick system
 *
 * Controls how often game ticks occur and how many random ticks are
 * generated per subchunk per game tick.
 */
struct TickConfig {
    /// Interval between game ticks in milliseconds
    /// Default: 50ms (20 ticks per second, like Minecraft)
    uint32_t gameTickIntervalMs = 50;

    /// Number of random tick attempts per subchunk per game tick
    /// Each attempt selects a random block position
    /// Default: 3 (like Minecraft's randomTickSpeed)
    uint32_t randomTicksPerSubchunk = 3;

    /// Optional RNG seed for random ticks (0 = use system random)
    /// Useful for deterministic testing
    uint64_t randomSeed = 0;

    /// Whether game ticks are enabled
    bool gameTicksEnabled = true;

    /// Whether random ticks are enabled
    bool randomTicksEnabled = true;
};

}  // namespace finevox
