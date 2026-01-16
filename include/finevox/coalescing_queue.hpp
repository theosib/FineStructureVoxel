#pragma once

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <optional>
#include <functional>
#include <mutex>

namespace finevox {

// A queue that coalesces duplicate entries
// When the same key is pushed multiple times, only one instance is kept in the queue.
// Useful for batching operations where only the latest state matters.
//
// Example use cases:
// - Dirty chunk tracking: multiple block changes to same chunk = one remesh
// - Light propagation: multiple updates to same position = one calculation
//
// Thread-safe version: CoalescingQueueTS
//
template<typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class CoalescingQueue {
public:
    CoalescingQueue() = default;

    // Push a key to the queue
    // If key already exists, this is a no-op (the existing entry remains)
    // Returns true if the key was newly added, false if it already existed
    bool push(const Key& key) {
        if (present_.contains(key)) {
            return false;
        }
        queue_.push(key);
        present_.insert(key);
        return true;
    }

    // Push a key, returns true if newly added
    bool push(Key&& key) {
        if (present_.contains(key)) {
            return false;
        }
        present_.insert(key);
        queue_.push(std::move(key));
        return true;
    }

    // Pop the front element
    // Returns nullopt if queue is empty
    std::optional<Key> pop() {
        if (queue_.empty()) {
            return std::nullopt;
        }
        Key key = std::move(queue_.front());
        queue_.pop();
        present_.erase(key);
        return key;
    }

    // Check if a key is in the queue
    [[nodiscard]] bool contains(const Key& key) const {
        return present_.contains(key);
    }

    // Check if queue is empty
    [[nodiscard]] bool empty() const {
        return queue_.empty();
    }

    // Number of elements in queue
    [[nodiscard]] size_t size() const {
        return queue_.size();
    }

    // Clear all elements
    void clear() {
        while (!queue_.empty()) {
            queue_.pop();
        }
        present_.clear();
    }

    // Remove a specific key from the queue
    // This is O(n) as we need to rebuild the queue
    // Use sparingly - prefer just letting items naturally pop
    bool remove(const Key& key) {
        if (!present_.contains(key)) {
            return false;
        }
        present_.erase(key);

        // Rebuild queue without the removed key
        std::queue<Key> newQueue;
        while (!queue_.empty()) {
            Key k = std::move(queue_.front());
            queue_.pop();
            if (present_.contains(k)) {
                newQueue.push(std::move(k));
            }
        }
        queue_ = std::move(newQueue);
        return true;
    }

private:
    std::queue<Key> queue_;
    std::unordered_set<Key, Hash, Equal> present_;
};

// Thread-safe version of CoalescingQueue
template<typename Key, typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class CoalescingQueueTS {
public:
    CoalescingQueueTS() = default;

    bool push(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.push(key);
    }

    bool push(Key&& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.push(std::move(key));
    }

    std::optional<Key> pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.pop();
    }

    [[nodiscard]] bool contains(const Key& key) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.contains(key);
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
    }

    bool remove(const Key& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.remove(key);
    }

    // Batch pop: pop up to maxCount items at once
    // Returns vector of popped items
    std::vector<Key> popBatch(size_t maxCount) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Key> result;
        result.reserve(std::min(maxCount, queue_.size()));

        for (size_t i = 0; i < maxCount; ++i) {
            auto item = queue_.pop();
            if (!item) break;
            result.push_back(std::move(*item));
        }

        return result;
    }

private:
    mutable std::mutex mutex_;
    CoalescingQueue<Key, Hash, Equal> queue_;
};

// CoalescingQueue with associated data
// Each key can have data associated with it. When a key is pushed again,
// the data is updated according to a merge function.
//
// Example: tracking dirty chunks with priority
// - Key: ChunkPos, Data: priority level
// - Merge: keep highest priority
//
template<typename Key, typename Data,
         typename Hash = std::hash<Key>, typename Equal = std::equal_to<Key>>
class CoalescingQueueWithData {
public:
    // MergeFunc takes (existing_data, new_data) and returns merged result
    using MergeFunc = std::function<Data(const Data&, const Data&)>;

    // Default merge: replace with new data
    CoalescingQueueWithData() : merge_([](const Data&, const Data& newData) { return newData; }) {}

    // Custom merge function
    explicit CoalescingQueueWithData(MergeFunc merge) : merge_(std::move(merge)) {}

    // Push with data
    // Returns true if key was newly added, false if merged with existing
    bool push(const Key& key, const Data& data) {
        auto it = dataMap_.find(key);
        if (it != dataMap_.end()) {
            it->second = merge_(it->second, data);
            return false;
        }
        queue_.push(key);
        present_.insert(key);
        dataMap_[key] = data;
        return true;
    }

    // Pop front with its data
    std::optional<std::pair<Key, Data>> pop() {
        if (queue_.empty()) {
            return std::nullopt;
        }
        Key key = std::move(queue_.front());
        queue_.pop();
        present_.erase(key);

        auto it = dataMap_.find(key);
        Data data = std::move(it->second);
        dataMap_.erase(it);

        return std::make_pair(std::move(key), std::move(data));
    }

    // Get data for a key (nullopt if not in queue)
    [[nodiscard]] std::optional<Data> getData(const Key& key) const {
        auto it = dataMap_.find(key);
        if (it != dataMap_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool contains(const Key& key) const {
        return present_.contains(key);
    }

    [[nodiscard]] bool empty() const {
        return queue_.empty();
    }

    [[nodiscard]] size_t size() const {
        return queue_.size();
    }

    void clear() {
        while (!queue_.empty()) {
            queue_.pop();
        }
        present_.clear();
        dataMap_.clear();
    }

private:
    std::queue<Key> queue_;
    std::unordered_set<Key, Hash, Equal> present_;
    std::unordered_map<Key, Data, Hash, Equal> dataMap_;
    MergeFunc merge_;
};

}  // namespace finevox
