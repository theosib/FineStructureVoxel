#pragma once

/**
 * @file frame_fence_waiter.hpp
 * @brief Wraps SimpleRenderer fence wait to integrate with WakeSignal
 *
 * Makes GPU fence completion look like a Queue to the multi-queue WakeSignal
 * system. A background thread waits on the fence and signals a WakeSignal
 * when ready, allowing the graphics thread to process meshes (and other
 * queues) during the fence wait instead of blocking.
 *
 * Design: [PLAN-fence-wait-thread.md]
 */

#include "finevox/core/wake_signal.hpp"

#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

namespace finevk {
class SimpleRenderer;
}

namespace finevox {

/**
 * @brief Wraps SimpleRenderer fence wait to integrate with WakeSignal
 *
 * Makes GPU fence completion look like a Queue to the multi-queue WakeSignal
 * system. Starts background thread to wait on fence, signals WakeSignal when
 * ready.
 *
 * Thread is not started from constructor — call setRenderer() then start()
 * to control initialization order.
 *
 * Supports two-phase shutdown: call requestStop() on all threads first,
 * then join() them all. This parallelizes shutdown wait across threads.
 *
 * Usage:
 *   FrameFenceWaiter fenceWait;
 *   fenceWait.setRenderer(renderer);
 *   fenceWait.attach(&wakeSignal);
 *   fenceWait.start();
 *
 *   // Each frame:
 *   fenceWait.kickWait();
 *   while (!fenceWait.isReady()) {
 *       wakeSignal.wait();
 *       processMeshes();
 *   }
 *   fenceWait.detach();   // Stop waking during render
 *   // ... render ...
 *   fenceWait.attach(&wakeSignal);  // Re-attach for next frame
 */
class FrameFenceWaiter {
public:
    FrameFenceWaiter() = default;
    ~FrameFenceWaiter();

    // Non-copyable, non-movable
    FrameFenceWaiter(const FrameFenceWaiter&) = delete;
    FrameFenceWaiter& operator=(const FrameFenceWaiter&) = delete;
    FrameFenceWaiter(FrameFenceWaiter&&) = delete;
    FrameFenceWaiter& operator=(FrameFenceWaiter&&) = delete;

    /// Set the renderer to wait on. Must be called before start()
    /// (unless setWaitFunction() is used instead).
    void setRenderer(finevk::SimpleRenderer* renderer);

    /// Set a custom wait function (for testing without a real renderer).
    /// If set, this is called instead of renderer->waitForCurrentFrameFence().
    void setWaitFunction(std::function<void()> fn);

    /// Set the fence wait timeout in nanoseconds. Default is 100ms.
    /// The thread loops with this timeout, checking for shutdown between iterations.
    /// Only applies to the renderer path; custom wait functions are called directly.
    void setWaitTimeout(uint64_t timeoutNs);

    /// Start the background wait thread.
    /// Requires either setRenderer() or setWaitFunction() to have been called.
    void start();

    /// Signal the thread to stop (non-blocking).
    /// Call this on all threads first, then join() them for parallel shutdown.
    void requestStop();

    /// Block until the background thread exits.
    /// Requires requestStop() to have been called first.
    void join();

    /// Stop the background thread. Equivalent to requestStop() + join().
    /// Safe to call multiple times. Called automatically from destructor.
    void stop();

    /// Attach to WakeSignal (same pattern as Queue)
    void attach(WakeSignal* signal);

    /// Detach from WakeSignal
    void detach();

    /// Start async fence wait on background thread.
    /// When fence is ready, signals attached WakeSignal (if any).
    /// Resets ready state internally — no separate reset() needed.
    void kickWait();

    /// Check if fence is ready (non-blocking, lock-free)
    [[nodiscard]] bool isReady() const {
        return ready_.load(std::memory_order_acquire);
    }

private:
    void threadFunc();

    finevk::SimpleRenderer* renderer_ = nullptr;
    std::function<void()> waitFn_;      // Custom wait function (testing)
    uint64_t waitTimeoutNs_ = 100'000'000;  // 100ms default

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    WakeSignal* signal_ = nullptr;      // Attached WakeSignal (guarded by mutex_)
    std::atomic<bool> ready_{true};     // Lock-free read from graphics thread
    bool pending_ = false;              // Guarded by mutex_
    bool running_ = false;             // Guarded by mutex_
};

}  // namespace finevox
