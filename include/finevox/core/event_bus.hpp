#pragma once

/**
 * @file event_bus.hpp
 * @brief Typed publish/subscribe event routing built on Queue<T>
 *
 * EventBus provides typed event subscription and dispatch. Events are
 * dispatched synchronously on the calling thread (game thread).
 *
 * For cross-thread event publishing, use queueEvent() which stores the
 * event in a Queue<GameEventHolder> and dispatches when dispatchQueued()
 * is called from the game thread.
 *
 * Thread safety:
 * - subscribe/unsubscribe: NOT thread-safe (call during setup or from game thread)
 * - publish: NOT thread-safe (call from game thread)
 * - queueEvent: Thread-safe (can be called from any thread)
 * - dispatchQueued: NOT thread-safe (call from game thread)
 */

#include "finevox/core/game_event.hpp"
#include "finevox/core/queue.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace finevox {

/// Subscription handle. Call unsubscribe() to remove.
using EventSubscription = uint64_t;
constexpr EventSubscription INVALID_SUBSCRIPTION = 0;

class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // Non-copyable, non-movable (owns Queue which has mutex)
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    // ========================================================================
    // Subscription
    // ========================================================================

    /**
     * @brief Subscribe to events of a specific type
     *
     * The handler is called with a const reference to the concrete event
     * when publish<T>() or dispatchQueued() fires.
     *
     * @tparam T Concrete event type
     * @param handler Callback invoked with const T&
     * @return Subscription handle for unsubscribing
     */
    template<typename T>
    EventSubscription subscribe(std::function<void(const T&)> handler) {
        EventTypeId typeId = registerEventType<T>();
        RawHandler raw = [h = std::move(handler)](const void* data) {
            h(*static_cast<const T*>(data));
        };
        return subscribeRaw(typeId, std::move(raw));
    }

    /**
     * @brief Unsubscribe a handler
     * @param sub Subscription handle from subscribe()
     */
    void unsubscribe(EventSubscription sub);

    // ========================================================================
    // Publishing (game thread only)
    // ========================================================================

    /**
     * @brief Publish an event synchronously to all subscribers
     *
     * Dispatches immediately on the calling thread.
     *
     * @tparam T Concrete event type
     * @param event The event to publish
     */
    template<typename T>
    void publish(const T& event) {
        EventTypeId typeId = registerEventType<T>();
        publishRaw(typeId, &event);
    }

    /**
     * @brief Publish from a type-erased GameEventHolder
     */
    void publish(const GameEventHolder& holder);

    // ========================================================================
    // Cross-Thread Queuing (uses Queue<GameEventHolder>)
    // ========================================================================

    /**
     * @brief Queue an event from any thread for later dispatch
     *
     * Thread-safe. Uses the existing Queue<T> infrastructure.
     * The event is dispatched when dispatchQueued() is called.
     *
     * @tparam T Concrete event type
     * @param event The event to queue (moved into holder)
     */
    template<typename T>
    void queueEvent(T event) {
        crossThreadQueue_.push(
            GameEventHolder::create<T>(std::move(event)));
    }

    /**
     * @brief Dispatch all queued cross-thread events
     *
     * Must be called from the game thread. Drains the cross-thread
     * queue and publishes each event to subscribers.
     *
     * @return Number of events dispatched
     */
    size_t dispatchQueued();

    /**
     * @brief Access the cross-thread queue directly
     *
     * Useful for attaching a WakeSignal for multi-queue coordination.
     */
    Queue<GameEventHolder>& crossThreadQueue() { return crossThreadQueue_; }

    /**
     * @brief Shutdown the cross-thread queue
     *
     * Signals the queue to stop accepting events and wakes any waiters.
     */
    void shutdown();

    // ========================================================================
    // Statistics
    // ========================================================================

    /// Total number of active subscriptions
    [[nodiscard]] size_t subscriberCount() const;

    /// Number of subscriptions for a specific event type
    [[nodiscard]] size_t subscriberCount(EventTypeId typeId) const;

    /// Number of pending cross-thread events
    [[nodiscard]] size_t queuedEventCount() const;

private:
    using RawHandler = std::function<void(const void*)>;

    struct SubscriberEntry {
        EventSubscription id;
        RawHandler handler;
    };

    EventSubscription subscribeRaw(EventTypeId typeId, RawHandler handler);
    void publishRaw(EventTypeId typeId, const void* data);

    // Handlers grouped by event type
    std::unordered_map<EventTypeId, std::vector<SubscriberEntry>> subscribers_;
    EventSubscription nextSubscriptionId_ = 1;

    // Cross-thread event queue (reuses existing Queue<T> infrastructure)
    Queue<GameEventHolder> crossThreadQueue_;
};

} // namespace finevox
