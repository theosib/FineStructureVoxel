#pragma once

/**
 * @file blocking_queue.hpp
 * @brief Thread-safe FIFO queue with deduplication (deprecated)
 *
 * Design: [24-event-system.md] §24.3 AlarmQueue
 * @deprecated Use AlarmQueue from alarm_queue.hpp instead
 */

#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <algorithm>

namespace finevox {

// DEPRECATED: Use AlarmQueue or AlarmQueueWithData from alarm_queue.hpp instead.
// AlarmQueue provides the same functionality plus alarm-based wakeup support.
//
// BlockingQueue will be removed in a future version.
//
// Migration:
//   - BlockingQueue<T> -> AlarmQueue<T>
//   - BlockingQueueWithData<K,V> -> AlarmQueueWithData<K,V>
//   - popWait() -> waitForWork() then tryPop()
//   - pop() -> tryPop()
//
// A thread-safe FIFO queue with deduplication and optional blocking operations.
//
// Features:
// - FIFO ordering (first-in, first-out)
// - O(1) deduplication - pushing a key already in queue is a no-op
// - Thread-safe for concurrent push/pop from multiple threads
// - Blocking popWait() with condition variable
// - Graceful shutdown support
// - Batch pop for efficiency
//
// Use cases:
// - Save queue (non-blocking, polling)
// - Any producer-consumer pattern needing deduplication
//
template<typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class BlockingQueue {
public:
    BlockingQueue() = default;

    // Non-copyable, non-movable (owns mutex/condition_variable)
    BlockingQueue(const BlockingQueue&) = delete;
    BlockingQueue& operator=(const BlockingQueue&) = delete;
    BlockingQueue(BlockingQueue&&) = delete;
    BlockingQueue& operator=(BlockingQueue&&) = delete;

    // ========================================================================
    // Push operations
    // ========================================================================

    // Push a key to the queue (thread-safe).
    // Returns true if newly added, false if already queued (deduplicated).
    bool push(const Key& key) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (present_.contains(key)) {
                return false;  // Already in queue
            }

            present_.insert(key);
            queue_.push_back(key);
        }

        condition_.notify_one();
        return true;
    }

    // Push with move semantics
    bool push(Key&& key) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (present_.contains(key)) {
                return false;
            }

            present_.insert(key);
            queue_.push_back(std::move(key));
        }

        condition_.notify_one();
        return true;
    }

    // ========================================================================
    // Pop operations
    // ========================================================================

    // Pop the front element (non-blocking, thread-safe).
    // Returns nullopt if queue is empty.
    std::optional<Key> pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        Key key = std::move(queue_.front());
        queue_.pop_front();
        present_.erase(key);
        return key;
    }

    // Pop the front element (blocking, thread-safe).
    // Waits until data is available or shutdown is signaled.
    // Returns nullopt only if shutdown was called and queue is empty.
    std::optional<Key> popWait() {
        std::unique_lock<std::mutex> lock(mutex_);

        condition_.wait(lock, [this]() {
            return !queue_.empty() || shutdownFlag_;
        });

        if (shutdownFlag_ && queue_.empty()) {
            return std::nullopt;
        }

        Key key = std::move(queue_.front());
        queue_.pop_front();
        present_.erase(key);
        return key;
    }

    // Pop up to maxCount items at once (non-blocking, thread-safe).
    // Returns vector of popped items (may be empty).
    std::vector<Key> popBatch(size_t maxCount) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::vector<Key> batch;
        batch.reserve(std::min(maxCount, queue_.size()));

        while (batch.size() < maxCount && !queue_.empty()) {
            Key key = std::move(queue_.front());
            queue_.pop_front();
            present_.erase(key);
            batch.push_back(std::move(key));
        }

        return batch;
    }

    // ========================================================================
    // Shutdown support
    // ========================================================================

    // Signal shutdown - wakes all waiting threads.
    // After shutdown, popWait() returns nullopt when queue is empty.
    // Idempotent - safe to call multiple times.
    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdownFlag_ = true;
        }
        condition_.notify_all();
    }

    // Check if shutdown was signaled.
    [[nodiscard]] bool isShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdownFlag_;
    }

    // Reset shutdown state (allows reuse after shutdown).
    void resetShutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdownFlag_ = false;
    }

    // ========================================================================
    // Query operations
    // ========================================================================

    // Check if a key is in the queue.
    [[nodiscard]] bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return present_.contains(key);
    }

    // Check if queue is empty.
    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    // Number of elements in queue.
    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    // ========================================================================
    // Modification operations
    // ========================================================================

    // Clear all elements.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        present_.clear();
    }

    // Remove a specific key from the queue.
    // Returns true if it was present and removed.
    // Note: O(n) operation - prefer letting items naturally pop when possible.
    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!present_.contains(key)) {
            return false;
        }

        present_.erase(key);

        // Find and remove from deque
        auto it = std::find_if(queue_.begin(), queue_.end(),
                               [&key](const Key& k) { return Equal{}(k, key); });
        if (it != queue_.end()) {
            queue_.erase(it);
        }

        return true;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdownFlag_ = false;

    std::deque<Key> queue_;
    std::unordered_set<Key, Hash, Equal> present_;
};

// ============================================================================
// BlockingQueueWithData - Same as BlockingQueue but with associated data
// ============================================================================

// A blocking queue where each key has associated data.
// When a key is pushed again, the data is updated according to a merge function.
//
// Example: tracking dirty chunks with priority
// - Key: ChunkPos, Data: priority level
// - Merge: keep highest priority
//
template<typename Key, typename Data,
         typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class BlockingQueueWithData {
public:
    // MergeFunc takes (existing_data, new_data) and returns merged result
    using MergeFunc = std::function<Data(const Data&, const Data&)>;

    // Default merge: replace with new data
    BlockingQueueWithData()
        : merge_([](const Data&, const Data& newData) { return newData; }) {}

    // Custom merge function
    explicit BlockingQueueWithData(MergeFunc merge)
        : merge_(std::move(merge)) {}

    // Push with data.
    // Returns true if key was newly added, false if merged with existing.
    bool push(const Key& key, const Data& data) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = dataMap_.find(key);
            if (it != dataMap_.end()) {
                it->second = merge_(it->second, data);
                return false;
            }

            queue_.push_back(key);
            present_.insert(key);
            dataMap_[key] = data;
        }

        condition_.notify_one();
        return true;
    }

    // Pop front with its data (non-blocking).
    std::optional<std::pair<Key, Data>> pop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        Key key = std::move(queue_.front());
        queue_.pop_front();
        present_.erase(key);

        auto it = dataMap_.find(key);
        Data data = std::move(it->second);
        dataMap_.erase(it);

        return std::make_pair(std::move(key), std::move(data));
    }

    // Pop front with its data (blocking).
    std::optional<std::pair<Key, Data>> popWait() {
        std::unique_lock<std::mutex> lock(mutex_);

        condition_.wait(lock, [this]() {
            return !queue_.empty() || shutdownFlag_;
        });

        if (shutdownFlag_ && queue_.empty()) {
            return std::nullopt;
        }

        Key key = std::move(queue_.front());
        queue_.pop_front();
        present_.erase(key);

        auto it = dataMap_.find(key);
        Data data = std::move(it->second);
        dataMap_.erase(it);

        return std::make_pair(std::move(key), std::move(data));
    }

    // Get data for a key (nullopt if not in queue).
    [[nodiscard]] std::optional<Data> getData(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = dataMap_.find(key);
        if (it != dataMap_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdownFlag_ = true;
        }
        condition_.notify_all();
    }

    [[nodiscard]] bool isShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdownFlag_;
    }

    [[nodiscard]] bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return present_.contains(key);
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        present_.clear();
        dataMap_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdownFlag_ = false;

    std::deque<Key> queue_;
    std::unordered_set<Key, Hash, Equal> present_;
    std::unordered_map<Key, Data, Hash, Equal> dataMap_;
    MergeFunc merge_;
};

}  // namespace finevox
