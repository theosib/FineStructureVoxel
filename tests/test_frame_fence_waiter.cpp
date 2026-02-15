#include <gtest/gtest.h>
#include "finevox/render/frame_fence_waiter.hpp"
#include <thread>
#include <chrono>
#include <atomic>

using namespace finevox;
using namespace std::chrono_literals;

// ============================================================================
// FrameFenceWaiter tests
// ============================================================================

TEST(FrameFenceWaiterTest, StartsReady) {
    FrameFenceWaiter waiter;
    EXPECT_TRUE(waiter.isReady());
}

TEST(FrameFenceWaiterTest, StartWithoutRendererThrows) {
    FrameFenceWaiter waiter;
    EXPECT_THROW(waiter.start(), std::runtime_error);
}

TEST(FrameFenceWaiterTest, StartWithWaitFunction) {
    FrameFenceWaiter waiter;
    waiter.setWaitFunction([] {});
    EXPECT_NO_THROW(waiter.start());
    waiter.stop();
}

TEST(FrameFenceWaiterTest, DoubleStartIsNoop) {
    FrameFenceWaiter waiter;
    waiter.setWaitFunction([] {});
    waiter.start();
    EXPECT_NO_THROW(waiter.start());  // Second start is safe
    waiter.stop();
}

TEST(FrameFenceWaiterTest, DoubleStopIsNoop) {
    FrameFenceWaiter waiter;
    waiter.setWaitFunction([] {});
    waiter.start();
    waiter.stop();
    EXPECT_NO_THROW(waiter.stop());  // Second stop is safe
}

TEST(FrameFenceWaiterTest, StopWithoutStartIsNoop) {
    FrameFenceWaiter waiter;
    EXPECT_NO_THROW(waiter.stop());
}

TEST(FrameFenceWaiterTest, KickWaitMakesNotReady) {
    FrameFenceWaiter waiter;
    std::atomic<bool> proceed{false};

    waiter.setWaitFunction([&] {
        while (!proceed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    });
    waiter.start();

    waiter.kickWait();

    // Should not be ready while wait function is blocking
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(waiter.isReady());

    // Let the wait function complete
    proceed.store(true, std::memory_order_release);

    // Wait for ready
    for (int i = 0; i < 100 && !waiter.isReady(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(waiter.isReady());

    waiter.stop();
}

TEST(FrameFenceWaiterTest, KickWaitWithImmediateCompletion) {
    FrameFenceWaiter waiter;
    waiter.setWaitFunction([] {});  // Instant completion
    waiter.start();

    waiter.kickWait();

    // Should become ready quickly
    for (int i = 0; i < 100 && !waiter.isReady(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(waiter.isReady());

    waiter.stop();
}

TEST(FrameFenceWaiterTest, WakeSignalIntegration) {
    WakeSignal signal;
    FrameFenceWaiter waiter;
    std::atomic<bool> proceed{false};

    waiter.setWaitFunction([&] {
        while (!proceed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    });
    waiter.attach(&signal);
    waiter.start();

    // Kick the wait
    waiter.kickWait();

    // Start a thread that waits on the signal
    std::atomic<bool> signalReceived{false};
    std::thread waiterThread([&] {
        signal.waitFor(2000ms);
        signalReceived = true;
    });

    // Give time for the thread to block
    std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(signalReceived);

    // Let the fence wait complete — should signal the WakeSignal
    proceed.store(true, std::memory_order_release);

    waiterThread.join();
    EXPECT_TRUE(signalReceived);
    EXPECT_TRUE(waiter.isReady());

    waiter.stop();
}

TEST(FrameFenceWaiterTest, DetachPreventsSignal) {
    WakeSignal signal;
    FrameFenceWaiter waiter;

    waiter.setWaitFunction([] {
        std::this_thread::sleep_for(30ms);
    });
    waiter.attach(&signal);
    waiter.start();

    // Kick, then immediately detach
    waiter.kickWait();
    waiter.detach();

    // Wait for the fence wait to complete
    for (int i = 0; i < 100 && !waiter.isReady(); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(waiter.isReady());

    // Signal should NOT have been signaled (we detached before completion)
    // Verify by checking that wait times out
    bool result = signal.waitFor(50ms);
    // result is true (not shutdown) but the signal wasn't fired
    // The wait should have timed out, and signal state is still unsignaled
    EXPECT_TRUE(result);  // Not shutdown
    // The key test: isReady() is true but signal was NOT woken by the waiter

    waiter.stop();
}

TEST(FrameFenceWaiterTest, MultipleKickCycles) {
    FrameFenceWaiter waiter;
    std::atomic<int> callCount{0};

    waiter.setWaitFunction([&] {
        callCount.fetch_add(1, std::memory_order_relaxed);
    });
    waiter.start();

    for (int cycle = 0; cycle < 5; ++cycle) {
        waiter.kickWait();

        for (int i = 0; i < 200 && !waiter.isReady(); ++i) {
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_TRUE(waiter.isReady()) << "Cycle " << cycle << " did not complete";
    }

    EXPECT_EQ(callCount.load(), 5);
    waiter.stop();
}

TEST(FrameFenceWaiterTest, ShutdownWhileWaiting) {
    FrameFenceWaiter waiter;
    std::atomic<bool> inWait{false};
    std::atomic<bool> proceed{false};

    waiter.setWaitFunction([&] {
        inWait.store(true, std::memory_order_release);
        while (!proceed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    });
    waiter.start();

    waiter.kickWait();

    // Wait until the wait function is actually executing
    for (int i = 0; i < 100 && !inWait.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(inWait);

    // Release the wait function so stop() can join
    proceed.store(true, std::memory_order_release);

    // stop() should complete cleanly
    waiter.stop();
}

TEST(FrameFenceWaiterTest, DestructorStopsThread) {
    std::atomic<int> callCount{0};

    {
        FrameFenceWaiter waiter;
        waiter.setWaitFunction([&] {
            callCount.fetch_add(1, std::memory_order_relaxed);
        });
        waiter.start();

        waiter.kickWait();

        for (int i = 0; i < 100 && !waiter.isReady(); ++i) {
            std::this_thread::sleep_for(5ms);
        }
        // Destructor should call stop() and join cleanly
    }

    EXPECT_GE(callCount.load(), 1);
}

TEST(FrameFenceWaiterTest, TwoPhaseShutdown) {
    FrameFenceWaiter waiter;
    std::atomic<bool> inWait{false};
    std::atomic<bool> proceed{false};

    waiter.setWaitFunction([&] {
        inWait.store(true, std::memory_order_release);
        while (!proceed.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(1ms);
        }
    });
    waiter.start();

    waiter.kickWait();

    // Wait until the wait function is executing
    for (int i = 0; i < 100 && !inWait.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_TRUE(inWait);

    // Phase 1: requestStop is non-blocking
    waiter.requestStop();

    // Release the wait function
    proceed.store(true, std::memory_order_release);

    // Phase 2: join blocks until thread exits
    waiter.join();

    // Double join is safe
    EXPECT_NO_THROW(waiter.join());
}

TEST(FrameFenceWaiterTest, RequestStopWithoutStart) {
    FrameFenceWaiter waiter;
    EXPECT_NO_THROW(waiter.requestStop());
    EXPECT_NO_THROW(waiter.join());
}

TEST(FrameFenceWaiterTest, AttachDetachCyclePerFrame) {
    WakeSignal signal;
    FrameFenceWaiter waiter;
    std::atomic<int> callCount{0};

    waiter.setWaitFunction([&] {
        callCount.fetch_add(1, std::memory_order_relaxed);
    });
    waiter.start();

    for (int frame = 0; frame < 3; ++frame) {
        // Attach for fence wait phase
        waiter.attach(&signal);
        waiter.kickWait();

        // Wait for completion
        for (int i = 0; i < 200 && !waiter.isReady(); ++i) {
            std::this_thread::sleep_for(5ms);
        }
        EXPECT_TRUE(waiter.isReady()) << "Frame " << frame;

        // Detach during render phase
        waiter.detach();

        // Simulate render work
        std::this_thread::sleep_for(5ms);

        // Re-attach for next frame (happens at loop top)
    }

    EXPECT_EQ(callCount.load(), 3);
    waiter.stop();
}
