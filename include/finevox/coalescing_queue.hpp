#pragma once

/**
 * @file coalescing_queue.hpp
 * @brief Thread-safe deduplicating queue with merge semantics
 *
 * CoalescingQueue is a FIFO queue that deduplicates by key. When a key is
 * pushed that already exists in the queue, the data is merged using a
 * caller-provided function instead of adding a duplicate entry.
 *
 * Design: [PLAN-mesh-architecture-improvements.md] Queue Infrastructure
 */

#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>
#include <vector>

namespace finevox {

// Forward declaration
class WakeSignal;

/**
 * @brief Thread-safe deduplicating queue with merge semantics
 *
 * CoalescingQueue maintains FIFO order while deduplicating by key.
 * When a duplicate key is pushed, the data is merged with the existing
 * entry using a merge function.
 *
 * Example: mesh rebuild queue
 * - Key: ChunkPos (subchunk position)
 * - Data: MeshRebuildRequest (priority, LOD, versions)
 * - Merge: keep higher priority, latest versions
 *
 * Usage:
 *   CoalescingQueue<ChunkPos, Request> queue([](const Request& a, const Request& b) {
 *       return Request{std::min(a.priority, b.priority), b.version};
 *   });
 *   queue.attach(&wakeSignal);
 *
 *   queue.push(pos, Request{100, 1});  // Added
 *   queue.push(pos, Request{50, 2});   // Merged: {50, 2}
 *
 * @tparam Key Key type for deduplication (must be hashable)
 * @tparam Data Associated data type
 * @tparam Hash Hash function for Key (default: std::hash<Key>)
 */
template<typename Key, typename Data, typename Hash = std::hash<Key>>
class CoalescingQueue {
public:
    using MergeFn = std::function<Data(const Data& existing, const Data& incoming)>;

    /**
     * @brief Create queue with default merge (replace with incoming)
     */
    CoalescingQueue()
        : merge_([](const Data&, const Data& incoming) { return incoming; }) {}

    /**
     * @brief Create queue with custom merge function
     *
     * @param merge Function that combines existing and incoming data
     */
    explicit CoalescingQueue(MergeFn merge)
        : merge_(std::move(merge)) {}

    ~CoalescingQueue() = default;

    // Non-copyable, non-movable
    CoalescingQueue(const CoalescingQueue&) = delete;
    CoalescingQueue& operator=(const CoalescingQueue&) = delete;
    CoalescingQueue(CoalescingQueue&&) = delete;
    CoalescingQueue& operator=(CoalescingQueue&&) = delete;

    // ========================================================================
    // WakeSignal attachment
    // ========================================================================

    /**
     * @brief Attach this queue to a WakeSignal
     *
     * When attached, push() will signal the WakeSignal.
     */
    void attach(WakeSignal* signal);

    /**
     * @brief Detach from current WakeSignal
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
     * @brief Push a key-data pair
     *
     * If the key already exists, the data is merged.
     * If attached, signals the WakeSignal.
     *
     * @param key Deduplication key
     * @param data Associated data
     * @return true if newly added, false if merged with existing
     */
    bool push(const Key& key, const Data& data);

    /**
     * @brief Push with move semantics
     */
    bool push(Key&& key, Data&& data);

    /**
     * @brief Push multiple key-data pairs atomically
     *
     * Items are merged if keys already exist.
     *
     * @param items Pairs of (key, data)
     * @return Number of newly added keys (vs merged)
     */
    size_t pushBatch(std::vector<std::pair<Key, Data>> items);

    // ========================================================================
    // Pop operations
    // ========================================================================

    /**
     * @brief Try to pop the front item (non-blocking)
     *
     * @return Pair of (key, data) if available, nullopt if empty
     */
    std::optional<std::pair<Key, Data>> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (order_.empty()) {
            return std::nullopt;
        }

        Key key = std::move(order_.front());
        order_.pop_front();
        present_.erase(key);

        auto it = items_.find(key);
        Data data = std::move(it->second);
        items_.erase(it);

        return std::make_pair(std::move(key), std::move(data));
    }

    /**
     * @brief Drain all items at once (non-blocking)
     *
     * @return Vector of (key, data) pairs in queue order
     */
    std::vector<std::pair<Key, Data>> drainAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<Key, Data>> result;
        result.reserve(order_.size());

        while (!order_.empty()) {
            Key key = std::move(order_.front());
            order_.pop_front();
            present_.erase(key);

            auto it = items_.find(key);
            Data data = std::move(it->second);
            items_.erase(it);

            result.emplace_back(std::move(key), std::move(data));
        }
        return result;
    }

    /**
     * @brief Drain up to maxItems (non-blocking)
     *
     * @param maxItems Maximum number of items to drain
     * @return Vector of (key, data) pairs in queue order
     */
    std::vector<std::pair<Key, Data>> drainUpTo(size_t maxItems) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<Key, Data>> result;
        size_t count = std::min(maxItems, order_.size());
        result.reserve(count);

        for (size_t i = 0; i < count; ++i) {
            Key key = std::move(order_.front());
            order_.pop_front();
            present_.erase(key);

            auto it = items_.find(key);
            Data data = std::move(it->second);
            items_.erase(it);

            result.emplace_back(std::move(key), std::move(data));
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
        return order_.empty();
    }

    /**
     * @brief Get number of unique items in queue
     */
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.size();
    }

    /**
     * @brief Check if a key is currently in the queue
     */
    [[nodiscard]] bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return present_.count(key) > 0;
    }

    /**
     * @brief Get data for a key (nullopt if not in queue)
     */
    [[nodiscard]] std::optional<Data> getData(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = items_.find(key);
        if (it != items_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // ========================================================================
    // Shutdown support
    // ========================================================================

    /**
     * @brief Signal shutdown
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
        order_.clear();
        present_.clear();
        items_.clear();
    }

private:
    mutable std::mutex mutex_;

    std::deque<Key> order_;                              // Insertion order
    std::unordered_set<Key, Hash> present_;              // Fast lookup
    std::unordered_map<Key, Data, Hash> items_;          // Key -> Data

    MergeFn merge_;
    WakeSignal* signal_ = nullptr;
    bool shutdown_ = false;
};

}  // namespace finevox

// Include implementation
#include "finevox/coalescing_queue_impl.hpp"
