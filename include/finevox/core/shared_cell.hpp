#pragma once

/**
 * @file shared_cell.hpp
 * @brief Plain array cells read by threads that never wait for the writer
 *
 * World storage has one rule: writers may lock, readers never do. A reader may see a value
 * one update old, but never freed memory. These are for the fixed arrays holding that data
 * (blocks, light, rotations, heights), which must stay plain arrays for the sake of size
 * and of the code that serializes them. On arm64 and x86 a relaxed load is an ordinary load.
 */

#include <atomic>

namespace finevox {

template <typename T>
[[nodiscard]] inline T sharedLoad(const T& cell, std::memory_order order = std::memory_order_relaxed) {
    return std::atomic_ref<T>(const_cast<T&>(cell)).load(order);
}

template <typename T>
inline void sharedStore(T& cell, T value, std::memory_order order = std::memory_order_relaxed) {
    std::atomic_ref<T>(cell).store(value, order);
}

}  // namespace finevox
