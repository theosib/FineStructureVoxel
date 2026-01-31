#pragma once

/**
 * @file simple_queue_impl.hpp
 * @brief Implementation details for SimpleQueue (included by simple_queue.hpp)
 */

#include "finevox/wake_signal.hpp"

namespace finevox {

template<typename T>
void SimpleQueue<T>::attach(WakeSignal* signal) {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = signal;

    // If queue already has items, notify immediately
    if (signal_ && !items_.empty()) {
        signal_->signal();
    }
}

template<typename T>
void SimpleQueue<T>::push(const T& item) {
    WakeSignal* signalToNotify = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return;  // Silently drop
        }

        items_.push_back(item);
        signalToNotify = signal_;
    }

    // Signal outside lock to avoid potential deadlock
    if (signalToNotify) {
        signalToNotify->signal();
    }
}

template<typename T>
void SimpleQueue<T>::push(T&& item) {
    WakeSignal* signalToNotify = nullptr;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (shutdown_) {
            return;
        }

        items_.push_back(std::move(item));
        signalToNotify = signal_;
    }

    if (signalToNotify) {
        signalToNotify->signal();
    }
}

template<typename T>
void SimpleQueue<T>::shutdown() {
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
