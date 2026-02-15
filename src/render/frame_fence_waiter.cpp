#include "finevox/render/frame_fence_waiter.hpp"

#include <finevk/high/simple_renderer.hpp>
#include <stdexcept>

namespace finevox {

FrameFenceWaiter::~FrameFenceWaiter() {
    stop();
}

void FrameFenceWaiter::setRenderer(finevk::SimpleRenderer* renderer) {
    std::lock_guard<std::mutex> lock(mutex_);
    renderer_ = renderer;
}

void FrameFenceWaiter::setWaitFunction(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(mutex_);
    waitFn_ = std::move(fn);
}

void FrameFenceWaiter::setWaitTimeout(uint64_t timeoutNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    waitTimeoutNs_ = timeoutNs;
}

void FrameFenceWaiter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;  // Already started
    if (!renderer_ && !waitFn_) {
        throw std::runtime_error("FrameFenceWaiter::start() called without renderer or wait function");
    }
    running_ = true;
    thread_ = std::thread(&FrameFenceWaiter::threadFunc, this);
}

void FrameFenceWaiter::requestStop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;
        running_ = false;
    }
    cv_.notify_one();
}

void FrameFenceWaiter::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void FrameFenceWaiter::stop() {
    requestStop();
    join();
}

void FrameFenceWaiter::attach(WakeSignal* signal) {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = signal;
}

void FrameFenceWaiter::detach() {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_ = nullptr;
}

void FrameFenceWaiter::kickWait() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = true;
        ready_.store(false, std::memory_order_release);
    }
    cv_.notify_one();
}

void FrameFenceWaiter::threadFunc() {
    while (true) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] {
                return pending_ || !running_;
            });
            if (!running_) break;
            pending_ = false;
        }

        // Block on fence (the expensive wait)
        if (waitFn_) {
            // Custom wait function (testing) — call directly
            waitFn_();
        } else {
            // Renderer path — loop with timeout so shutdown isn't blocked
            uint64_t timeout = waitTimeoutNs_;
            while (!renderer_->waitForCurrentFrameFence(timeout)) {
                // Check if we should shut down between timeout iterations
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_) return;
            }
        }

        // Signal completion
        ready_.store(true, std::memory_order_release);

        WakeSignal* signalToNotify = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            signalToNotify = signal_;
        }

        if (signalToNotify) {
            signalToNotify->signal();
        }
    }
}

}  // namespace finevox
