#pragma once

#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
#include <algorithm>

namespace finevox {

// A thread-safe FIFO queue with alarm-based wakeup support.
//
// Designed for mesh worker threads that need to:
// 1. Process explicit work requests immediately (push/tryPop)
// 2. Wake at scheduled times to scan for stale chunks (setAlarm/waitForWork)
// 3. Block efficiently when no work is available
//
// Alarm semantics:
// - setAlarm() schedules a wakeup at a future time
// - If an alarm is already pending, keep the LATER one (worker is busy anyway)
// - If the worker is awake when alarm fires, discard it (worker will find work)
// - waitForWork() blocks until: push, alarm fires, or shutdown
//
// Usage:
//   AlarmQueue<Request> queue;
//
//   // Producer thread (graphics thread):
//   queue.push(request);  // Wake worker immediately
//   queue.setAlarm(now + 10ms);  // Schedule background scan
//
//   // Worker thread:
//   while (running) {
//       if (auto req = queue.tryPop()) {
//           process(*req);
//           continue;
//       }
//       // No explicit work, block until something happens
//       queue.waitForWork();
//   }
//
template<typename T>
class AlarmQueue {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    AlarmQueue() = default;

    // Non-copyable, non-movable (owns mutex/condition_variable)
    AlarmQueue(const AlarmQueue&) = delete;
    AlarmQueue& operator=(const AlarmQueue&) = delete;
    AlarmQueue(AlarmQueue&&) = delete;
    AlarmQueue& operator=(AlarmQueue&&) = delete;

    // ========================================================================
    // Push operations
    // ========================================================================

    // Push an item to the queue (thread-safe).
    // Wakes any thread blocked in waitForWork().
    void push(const T& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(item);
        }
        condition_.notify_all();
    }

    // Push with move semantics
    void push(T&& item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(item));
        }
        condition_.notify_all();
    }

    // ========================================================================
    // Pop operations
    // ========================================================================

    // Try to pop the front element (non-blocking, thread-safe).
    // Returns nullopt if queue is empty.
    std::optional<T> tryPop() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (queue_.empty()) {
            return std::nullopt;
        }

        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    // ========================================================================
    // Alarm operations
    // ========================================================================

    // Set an alarm to wake at the specified time.
    //
    // If an alarm is already pending:
    // - Keep the LATER time (rationale: if we're setting a new alarm while
    //   one exists, the worker is busy and doesn't need the earlier wake)
    //
    // If wakeTime is in the past or now, the next waitForWork() will
    // return immediately.
    void setAlarm(TimePoint wakeTime) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!alarmPending_ || wakeTime > alarmTime_) {
            alarmTime_ = wakeTime;
            alarmPending_ = true;
        }
        // Note: We don't notify here. The alarm is passive - it only
        // affects threads that are already waiting in waitForWork().
        // If a thread is currently processing work, setting an alarm
        // doesn't need to interrupt it.

        // However, if a thread is already waiting and the new alarm
        // is sooner than what it's waiting for... but our policy says
        // keep the LATER alarm, so this shouldn't happen in practice.

        // Actually, we should notify in case a thread is waiting with
        // no alarm and we just set one:
        condition_.notify_all();
    }

    // Cancel any pending alarm.
    void clearAlarm() {
        std::lock_guard<std::mutex> lock(mutex_);
        alarmPending_ = false;
    }

    // Check if an alarm is pending.
    [[nodiscard]] bool hasAlarm() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return alarmPending_;
    }

    // ========================================================================
    // Wait operations
    // ========================================================================

    // Block until one of:
    // 1. An item is pushed to the queue
    // 2. A pending alarm fires (time reached)
    // 3. shutdown() is called
    //
    // Does NOT pop any items - caller should use tryPop() after waking.
    // Returns true if woken normally, false if shutdown was signaled.
    bool waitForWork() {
        std::unique_lock<std::mutex> lock(mutex_);

        while (true) {
            // Check exit conditions
            if (shutdownFlag_) {
                return false;
            }

            if (!queue_.empty()) {
                // Work available
                return true;
            }

            if (alarmPending_) {
                // Wait until alarm time or other wake condition
                auto status = condition_.wait_until(lock, alarmTime_);

                if (status == std::cv_status::timeout) {
                    // Alarm fired - clear it and return
                    alarmPending_ = false;
                    return true;
                }
                // Spurious wake or push/shutdown - loop back to check
            } else {
                // No alarm - wait indefinitely
                condition_.wait(lock);
                // Loop back to check conditions
            }
        }
    }

    // Wait with a maximum timeout (useful for periodic health checks).
    // Returns true if woken normally or timeout, false if shutdown.
    bool waitForWork(std::chrono::milliseconds maxWait) {
        std::unique_lock<std::mutex> lock(mutex_);

        auto deadline = Clock::now() + maxWait;

        while (true) {
            if (shutdownFlag_) {
                return false;
            }

            if (!queue_.empty()) {
                return true;
            }

            // Determine what to wait for
            TimePoint waitUntil = deadline;
            if (alarmPending_ && alarmTime_ < deadline) {
                waitUntil = alarmTime_;
            }

            auto status = condition_.wait_until(lock, waitUntil);

            if (status == std::cv_status::timeout) {
                // Check if it was the alarm
                if (alarmPending_ && Clock::now() >= alarmTime_) {
                    alarmPending_ = false;
                }
                // Either way, timeout means return
                return !shutdownFlag_;
            }
            // Spurious wake - loop back
        }
    }

    // ========================================================================
    // Shutdown support
    // ========================================================================

    // Signal shutdown - wakes all waiting threads.
    // After shutdown, waitForWork() returns false.
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

    // Clear all elements and alarm.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.clear();
        alarmPending_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdownFlag_ = false;

    std::deque<T> queue_;

    // Alarm state
    bool alarmPending_ = false;
    TimePoint alarmTime_;
};

// ============================================================================
// AlarmQueueWithData - AlarmQueue with key deduplication and associated data
// ============================================================================

// Like BlockingQueueWithData but with alarm support.
// Keys are deduplicated; pushing an existing key merges the data.
//
template<typename Key, typename Data,
         typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class AlarmQueueWithData {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using MergeFunc = std::function<Data(const Data&, const Data&)>;

    // Default merge: replace with new data
    AlarmQueueWithData()
        : merge_([](const Data&, const Data& newData) { return newData; }) {}

    // Custom merge function
    explicit AlarmQueueWithData(MergeFunc merge)
        : merge_(std::move(merge)) {}

    // Non-copyable, non-movable
    AlarmQueueWithData(const AlarmQueueWithData&) = delete;
    AlarmQueueWithData& operator=(const AlarmQueueWithData&) = delete;
    AlarmQueueWithData(AlarmQueueWithData&&) = delete;
    AlarmQueueWithData& operator=(AlarmQueueWithData&&) = delete;

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

        condition_.notify_all();
        return true;
    }

    // Try to pop front with its data (non-blocking).
    std::optional<std::pair<Key, Data>> tryPop() {
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

    // Pop front with its data (non-blocking).
    // Convenience alias for tryPop() - matches BlockingQueueWithData interface.
    std::optional<std::pair<Key, Data>> pop() {
        return tryPop();
    }

    // Pop front with its data (blocking).
    // Waits until data is available or shutdown is signaled.
    // Returns nullopt only if shutdown was called and queue is empty.
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

    // Set alarm (same semantics as AlarmQueue)
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

    [[nodiscard]] bool hasAlarm() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return alarmPending_;
    }

    // Block until work available, alarm fires, or shutdown.
    // Does NOT pop - use tryPop() after.
    bool waitForWork() {
        std::unique_lock<std::mutex> lock(mutex_);

        while (true) {
            if (shutdownFlag_) {
                return false;
            }

            if (!queue_.empty()) {
                return true;
            }

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

    [[nodiscard]] bool isShutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdownFlag_;
    }

    void resetShutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdownFlag_ = false;
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
        alarmPending_ = false;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    bool shutdownFlag_ = false;

    std::deque<Key> queue_;
    std::unordered_set<Key, Hash, Equal> present_;
    std::unordered_map<Key, Data, Hash, Equal> dataMap_;
    MergeFunc merge_;

    bool alarmPending_ = false;
    TimePoint alarmTime_;
};

}  // namespace finevox
