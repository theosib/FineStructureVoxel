#pragma once

#include <list>
#include <unordered_map>
#include <optional>
#include <functional>
#include <chrono>

namespace finevox {

// LRU (Least Recently Used) cache
// Stores key-value pairs with automatic eviction of least recently used items
// when capacity is exceeded.
//
// Thread-safety: NOT thread-safe. Caller must provide synchronization.
//
template<typename Key, typename Value,
         typename Hash = std::hash<Key>,
         typename Equal = std::equal_to<Key>>
class LRUCache {
public:
    explicit LRUCache(size_t capacity) : capacity_(capacity) {}

    // Get value for key, moving it to front (most recently used)
    // Returns nullopt if not found
    std::optional<Value> get(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }

        // Move to front (most recently used)
        items_.splice(items_.begin(), items_, it->second);
        return it->second->value;
    }

    // Get pointer to value (for in-place modification)
    // Does NOT move to front - call touch() separately if needed
    Value* peek(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return nullptr;
        }
        return &(it->second->value);
    }

    // Move key to front without returning value
    void touch(const Key& key) {
        auto it = index_.find(key);
        if (it != index_.end()) {
            items_.splice(items_.begin(), items_, it->second);
        }
    }

    // Insert or update a key-value pair
    // If capacity exceeded, evicts least recently used item
    // Returns the evicted value if any
    std::optional<std::pair<Key, Value>> put(const Key& key, Value value) {
        std::optional<std::pair<Key, Value>> evicted;

        auto it = index_.find(key);
        if (it != index_.end()) {
            // Key exists - update value and move to front
            it->second->value = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
        } else {
            // New key - check capacity
            if (items_.size() >= capacity_) {
                // Evict least recently used (back of list)
                auto& lru = items_.back();
                evicted = std::make_pair(lru.key, std::move(lru.value));
                index_.erase(lru.key);
                items_.pop_back();
            }

            // Insert at front
            items_.push_front({key, std::move(value)});
            index_[key] = items_.begin();
        }

        return evicted;
    }

    // Remove a key
    // Returns the removed value if found
    std::optional<Value> remove(const Key& key) {
        auto it = index_.find(key);
        if (it == index_.end()) {
            return std::nullopt;
        }

        Value value = std::move(it->second->value);
        items_.erase(it->second);
        index_.erase(it);
        return value;
    }

    // Check if key exists
    [[nodiscard]] bool contains(const Key& key) const {
        return index_.contains(key);
    }

    // Current number of items
    [[nodiscard]] size_t size() const {
        return items_.size();
    }

    // Maximum capacity
    [[nodiscard]] size_t capacity() const {
        return capacity_;
    }

    // Check if empty
    [[nodiscard]] bool empty() const {
        return items_.empty();
    }

    // Check if at capacity
    [[nodiscard]] bool full() const {
        return items_.size() >= capacity_;
    }

    // Clear all items
    void clear() {
        items_.clear();
        index_.clear();
    }

    // Change capacity (may trigger evictions)
    // Returns evicted items
    std::vector<std::pair<Key, Value>> setCapacity(size_t newCapacity) {
        std::vector<std::pair<Key, Value>> evicted;

        while (items_.size() > newCapacity) {
            auto& lru = items_.back();
            evicted.emplace_back(lru.key, std::move(lru.value));
            index_.erase(lru.key);
            items_.pop_back();
        }

        capacity_ = newCapacity;
        return evicted;
    }

    // Iterate over items (most to least recently used)
    template<typename Func>
    void forEach(Func&& func) {
        for (auto& item : items_) {
            func(item.key, item.value);
        }
    }

    // Iterate over items (most to least recently used) const
    template<typename Func>
    void forEach(Func&& func) const {
        for (const auto& item : items_) {
            func(item.key, item.value);
        }
    }

    // Get the least recently used key (without removing)
    std::optional<Key> leastRecentKey() const {
        if (items_.empty()) {
            return std::nullopt;
        }
        return items_.back().key;
    }

    // Get the most recently used key (without removing)
    std::optional<Key> mostRecentKey() const {
        if (items_.empty()) {
            return std::nullopt;
        }
        return items_.front().key;
    }

private:
    struct Item {
        Key key;
        Value value;
    };

    size_t capacity_;
    std::list<Item> items_;  // Front = most recent, Back = least recent
    std::unordered_map<Key, typename std::list<Item>::iterator, Hash, Equal> index_;
};

}  // namespace finevox
