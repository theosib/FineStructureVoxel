#pragma once

/**
 * @file simple_queue.hpp
 * @brief Thread-safe FIFO queue with WakeSignal support
 *
 * SimpleQueue is a basic FIFO queue that can optionally signal a WakeSignal
 * when items are pushed. This allows a consumer to wait on multiple queues
 * using a single WakeSignal.
 *
 * Design: [PLAN-mesh-architecture-improvements.md] Queue Infrastructure
 */

#include <deque>
#include <optional>
#include <mutex>
#include <atomic>
#include <vector>

namespace finevox {

// Forward declaration
class WakeSignal;

/**
 * @brief Thread-safe FIFO queue with optional wake signaling
 *
 * SimpleQueue provides basic FIFO semantics with thread-safe push and pop.
 * When attached to a WakeSignal, pushes will signal the consumer.
 *
 * Unlike CoalescingQueue, SimpleQueue does not deduplicate - every push
 * results in a new item in the queue.
 *
 * Usage:
 *   SimpleQueue<MeshData> queue;
 *   queue.attach(&wakeSignal);
 *
 *   // Producer:
 *   queue.push(meshData);  // Signals wakeSignal
 *
 *   // Consumer:
 *   while (auto item = queue.tryPop()) {
 *       process(*item);
 *   }
 *
 * @tparam T Type of items in the queue (must be movable)
 */
template<typename T>
class SimpleQueue {
public:
    SimpleQueue() = default;
    ~SimpleQueue() = default;

    // Non-copyable, non-movable (owns mutex)
    SimpleQueue(const SimpleQueue&) = delete;
    SimpleQueue& operator=(const SimpleQueue&) = delete;
    SimpleQueue(SimpleQueue&&) = delete;
    SimpleQueue& operator=(SimpleQueue&&) = delete;

    // ========================================================================
    // WakeSignal attachment
    // ========================================================================

    /**
     * @brief Attach this queue to a WakeSignal
     *
     * When attached, push() will call signal_->signal() to wake consumers.
     * A queue can only be attached to one WakeSignal at a time.
     *
     * If the queue already has items, the signal is notified immediately.
     *
     * @param signal Pointer to WakeSignal (must outlive queue or be detached)
     */
    void attach(WakeSignal* signal);

    /**
     * @brief Detach from current WakeSignal
     *
     * After detaching, push() no longer signals anyone.
     */
    void detach() {
        std::lock_guard<std::mutex> lock(mutex_);
        signal_ = nullptr;
    }

    /**
     * @brief Check if attached to a WakeSignal
     */
    [[nodiscard]] bool isAttached() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return signal_ != nullptr;
    }

    // ========================================================================
    // Push operations
    // ========================================================================

    /**
     * @brief Push an item to the back of the queue
     *
     * If attached to a WakeSignal, signals after adding the item.
     * If shutdown has been called, the item is silently dropped.
     *
     * @param item Item to add
     */
    void push(const T& item);

    /**
     * @brief Push an item (move version)
     */
    void push(T&& item);

    /**
     * @brief Push multiple items atomically (one lock, one signal)
     *
     * More efficient than multiple push() calls for bulk operations.
     *
     * @param items Items to add
     */
    void pushBatch(std::vector<T> items);

    /**
     * @brief Push multiple items from iterators
     */
    template<typename Iterator>
    void pushBatch(Iterator begin, Iterator end);

    // ========================================================================
    // Pop operations
    // ========================================================================

    /**
     * @brief Try to pop the front item (non-blocking)
     *
     * @return The front item if available, nullopt if empty
     */
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (items_.empty()) {
            return std::nullopt;
        }

        T item = std::move(items_.front());
        items_.pop_front();
        return item;
    }

    /**
     * @brief Drain all items at once (non-blocking)
     *
     * Returns all items for batch processing.
     *
     * @return Vector of all items in queue order
     */
    std::vector<T> drainAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> result;
        result.reserve(items_.size());
        while (!items_.empty()) {
            result.push_back(std::move(items_.front()));
            items_.pop_front();
        }
        return result;
    }

    /**
     * @brief Drain up to maxItems (non-blocking)
     *
     * Useful for processing in bounded batches.
     *
     * @param maxItems Maximum number of items to drain
     * @return Vector of drained items
     */
    std::vector<T> drainUpTo(size_t maxItems) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> result;
        size_t count = std::min(maxItems, items_.size());
        result.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            result.push_back(std::move(items_.front()));
            items_.pop_front();
        }
        return result;
    }

    // ========================================================================
    // Query operations
    // ========================================================================

    /**
     * @brief Check if queue is empty
     */
    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    /**
     * @brief Get number of items in queue
     */
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    // ========================================================================
    // Shutdown support
    // ========================================================================

    /**
     * @brief Signal shutdown
     *
     * After shutdown:
     * - push() silently drops items
     * - tryPop() continues to work until queue is drained
     * - If attached, signals the WakeSignal
     */
    void shutdown();

    /**
     * @brief Check if shutdown was called
     */
    [[nodiscard]] bool isShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

    /**
     * @brief Reset shutdown state
     */
    void resetShutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = false;
    }

    /**
     * @brief Clear all items
     */
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::deque<T> items_;
    WakeSignal* signal_ = nullptr;
    bool shutdown_ = false;
};

}  // namespace finevox

// Include implementation (needs WakeSignal definition)
#include "finevox/core/simple_queue_impl.hpp"
