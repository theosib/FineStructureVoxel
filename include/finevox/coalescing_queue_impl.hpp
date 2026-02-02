#pragma once

/**
 * @file coalescing_queue_impl.hpp
 * @brief Implementation details for CoalescingQueue
 */

#include "finevox/wake_signal.hpp"

namespace finevox {

template<typename Key, typename Data, typename Hash>
void CoalescingQueue<Key, Data, Hash>::attach(WakeSignal* signal) {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = signal;

    // If queue already has items, notify immediately
    if (signal_ && !order_.empty()) {
        signal_->signal();
    }
}

template<typename Key, typename Data, typename Hash>
bool CoalescingQueue<Key, Data, Hash>::push(const Key& key, const Data& data) {
    WakeSignal* signalToNotify = nullptr;
    bool isNew = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return false;
        }

        auto it = items_.find(key);
        if (it != items_.end()) {
            // Key exists - merge data
            it->second = merge_(it->second, data);
            isNew = false;
        } else {
            // New key - add to queue
            order_.push_back(key);
            present_.insert(key);
            items_[key] = data;
            isNew = true;
        }

        signalToNotify = signal_;
    }

    // Signal outside lock
    if (signalToNotify) {
        signalToNotify->signal();
    }

    return isNew;
}

template<typename Key, typename Data, typename Hash>
bool CoalescingQueue<Key, Data, Hash>::push(Key&& key, Data&& data) {
    WakeSignal* signalToNotify = nullptr;
    bool isNew = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return false;
        }

        auto it = items_.find(key);
        if (it != items_.end()) {
            // Key exists - merge data
            it->second = merge_(it->second, std::move(data));
            isNew = false;
        } else {
            // New key - add to queue
            order_.push_back(key);
            present_.insert(key);
            items_.emplace(std::move(key), std::move(data));
            isNew = true;
        }

        signalToNotify = signal_;
    }

    if (signalToNotify) {
        signalToNotify->signal();
    }

    return isNew;
}

template<typename Key, typename Data, typename Hash>
size_t CoalescingQueue<Key, Data, Hash>::pushBatch(std::vector<std::pair<Key, Data>> items) {
    if (items.empty()) return 0;

    WakeSignal* signalToNotify = nullptr;
    size_t newCount = 0;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return 0;
        }

        for (auto& [key, data] : items) {
            auto it = items_.find(key);
            if (it != items_.end()) {
                it->second = merge_(it->second, std::move(data));
            } else {
                order_.push_back(key);
                present_.insert(key);
                items_.emplace(std::move(key), std::move(data));
                ++newCount;
            }
        }

        signalToNotify = signal_;
    }

    if (signalToNotify) {
        signalToNotify->signal();
    }

    return newCount;
}

template<typename Key, typename Data, typename Hash>
void CoalescingQueue<Key, Data, Hash>::shutdown() {
    WakeSignal* signalToNotify = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        signalToNotify = signal_;
    }

    if (signalToNotify) {
        signalToNotify->signal();
    }
}

}  // namespace finevox
