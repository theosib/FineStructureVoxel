#pragma once

/**
 * @file graphics_event_queue.hpp
 * @brief Event queue for game thread to graphics thread communication
 *
 * Design: [25-entity-system.md] §25.3 Graphics Event Queue
 *
 * GraphicsMessage wraps either:
 *   - EntitySnapshot (POD, high frequency, every tick per entity)
 *   - finescript::Value (flexible, for spawn/despawn/correction/animation events)
 */

#include "finevox/core/queue.hpp"
#include "finevox/core/entity_state.hpp"
#include "finevox/core/position.hpp"
#include <finescript/value.h>

#include <chrono>
#include <optional>
#include <vector>

namespace finevox {

// Forward declaration
class Entity;

// ============================================================================
// EntitySnapshot - POD snapshot for high-frequency entity state updates
// ============================================================================

/**
 * @brief Lightweight POD snapshot published every tick per visible entity.
 * Kept as a plain struct for zero-overhead batching.
 */
struct EntitySnapshot {
    EntityState entity;
    uint64_t timestamp = 0;
    uint64_t tickNumber = 0;

    /// Create from an Entity (convenience)
    static EntitySnapshot fromEntity(const Entity& ent, uint64_t tick);
};

// ============================================================================
// GraphicsMessage - Discriminated wrapper for the graphics queue
// ============================================================================

/**
 * @brief Message sent from game thread to graphics thread.
 *
 * Snapshot: POD entity state for interpolation (hot path).
 * Event: finescript::Value map for spawn/despawn/correction/animation (cold path).
 */
struct GraphicsMessage {
    enum class Kind : uint8_t { Snapshot, Event };

    Kind kind = Kind::Snapshot;
    EntitySnapshot snapshot;       // valid when kind == Snapshot
    finescript::Value event;       // valid when kind == Event (nil otherwise)

    /// Create a snapshot message
    static GraphicsMessage fromSnapshot(EntitySnapshot snap) {
        GraphicsMessage msg;
        msg.kind = Kind::Snapshot;
        msg.snapshot = std::move(snap);
        return msg;
    }

    /// Create an event message from a finescript::Value
    static GraphicsMessage fromEvent(finescript::Value val) {
        GraphicsMessage msg;
        msg.kind = Kind::Event;
        msg.event = std::move(val);
        return msg;
    }
};

// ============================================================================
// GraphicsEventQueue - Thread-safe queue for game→graphics messages
// ============================================================================

using GraphicsEventQueue = Queue<GraphicsMessage>;

}  // namespace finevox
