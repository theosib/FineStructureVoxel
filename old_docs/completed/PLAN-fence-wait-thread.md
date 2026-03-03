# Fence-Wait Thread: Overlap Mesh Processing with GPU Fence Wait

**Status: COMPLETE** — Implemented and integrated into render_demo.cpp. 16 tests passing.

## Context

Previously the graphics thread's main loop:
1. `renderer->beginFrame()` — **blocks on vkWaitForFences** until GPU finishes previous frame
2. Process GLFW user events
3. Process meshes as they arrive until a deadline (half frame period)
4. Render and submit frame

During step 1, the fence wait can be 0-16ms at 60fps with 3 frames in flight. Mesh workers continue producing completed meshes during this time, but they sit unprocessed in the upload queue.

**Goal:** Process meshes (and other queues) during the fence wait so there's no backlog when the frame becomes available. The fence completion triggers the deadline countdown — the deadline exists so that quick-turn effects of user input (from GLFW events) can be reflected in the same frame.

## Architecture

```
Fence Thread:                    Graphics Thread:
  waitForCurrentFrameFence()       Phase 1: Process meshes (no deadline)
  fenceReady = true                  wakeSignal.wait()
  wakeSignal.signal()                ├─ mesh arrived?  → upload
  wait for next kick                 └─ fence ready?   → move to Phase 2
                                   Phase 2: Acquire + events + deadline
                                     beginFrame(skipFenceWait=true)
                                     pollEvents()
                                     deadline = now + framePeriod/2
                                     while (now < deadline):
                                       wakeSignal.wait()
                                       processMeshes()
                                   Phase 3: Render + submit
                                     recordDrawCommands(frame)
                                     endFrame()
```

All Vulkan operations stay on the graphics thread. The only background operation is the fence wait (pure CPU blocking, not a Vulkan command). No mutex needed for Vulkan state.

## Changes: finevk (FineStructureVK)

### Minimal API additions to Window (and SimpleRenderer)

```cpp
class Window {
public:
    /// Wait for current frame slot's fence to be signaled (blocking, thread-safe).
    /// Can be called from any thread to synchronize with GPU completion.
    void waitForCurrentFrameFence();

    /// Begin frame acquisition.
    /// @param skipFenceWait If true, assumes fence already signaled via waitForCurrentFrameFence()
    FrameBeginResult beginFrame(bool skipFenceWait = false);
};
```

Implementation:

```cpp
void Window::waitForCurrentFrameFence() {
    // Thread-safe — just waits on the fence for current frame slot
    inFlightFences_[currentFrameIndex_]->wait();
}

FrameBeginResult Window::beginFrame(bool skipFenceWait) {
    if (!skipFenceWait) {
        waitForCurrentFrameFence();
    }

    // Reset fence
    inFlightFences_[currentFrameIndex_]->reset();

    // ... rest of existing beginFrame logic (acquire image, etc.)
}
```

SimpleRenderer delegates to Window for both methods.

**Design rationale:** finevk exposes C++ primitives. No WakeSignal dependency in finevk — it stays a pure Vulkan wrapper. finevox handles the queue integration.

## Changes: finevox (FineStructureVoxel)

### 1. New class: FrameFenceWaiter

Makes GPU fence completion look like a Queue to the multi-queue WakeSignal system. Follows the same attach/detach pattern as the queue classes.

**File:** `include/finevox/render/frame_fence_waiter.hpp`

```cpp
#pragma once

#include "finevox/core/wake_signal.hpp"
#include <finevk/finevk.hpp>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace finevox {

/**
 * @brief Wraps SimpleRenderer fence wait to integrate with WakeSignal
 *
 * Makes GPU fence completion look like a Queue to the multi-queue WakeSignal
 * system. Background thread waits on fence, signals WakeSignal when ready.
 *
 * Thread is not started from constructor — call setRenderer() then start()
 * to control initialization order.
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

    /// Set the renderer to wait on. Must be called before start().
    void setRenderer(finevk::SimpleRenderer* renderer);

    /// Start the background wait thread. Requires renderer to be set.
    void start();

    /// Stop the background thread. Blocks until thread exits.
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

    std::thread thread_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    WakeSignal* signal_ = nullptr;      // Attached WakeSignal (guarded by mutex_)
    std::atomic<bool> ready_{true};     // Lock-free read from graphics thread
    bool pending_ = false;              // Guarded by mutex_
    bool running_ = false;             // Guarded by mutex_
};

}  // namespace finevox
```

**File:** `src/render/frame_fence_waiter.cpp`

```cpp
#include "finevox/render/frame_fence_waiter.hpp"
#include <stdexcept>

namespace finevox {

FrameFenceWaiter::~FrameFenceWaiter() {
    stop();
}

void FrameFenceWaiter::setRenderer(finevk::SimpleRenderer* renderer) {
    std::lock_guard<std::mutex> lock(mutex_);
    renderer_ = renderer;
}

void FrameFenceWaiter::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;  // Already started
    if (!renderer_) {
        throw std::runtime_error("FrameFenceWaiter::start() called without renderer");
    }
    running_ = true;
    thread_ = std::thread(&FrameFenceWaiter::threadFunc, this);
}

void FrameFenceWaiter::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_) return;  // Already stopped
        running_ = false;
    }
    cv_.notify_one();
    if (thread_.joinable()) {
        thread_.join();
    }
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
        renderer_->waitForCurrentFrameFence();

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
```

### 2. Add to CMakeLists.txt

Add `src/render/frame_fence_waiter.cpp` to the `finevox_render` library sources.

### 3. Restructure render_demo.cpp main loop

**Current flow:**
```
beginFrame() ← blocks on fence
pollEvents()
mesh processing loop (deadline-based WakeSignal wait)
render + submit
```

**New flow (illustrative — actual integration depends on where WakeSignal lives):**
```cpp
// === Setup (once, after renderer is created) ===
WakeSignal graphicsWake;
FrameFenceWaiter fenceWait;
fenceWait.setRenderer(renderer.get());
fenceWait.attach(&graphicsWake);
fenceWait.start();

meshUploadQueue.attach(&graphicsWake);

// === Per frame ===

// Phase 1: Kick async fence wait, process meshes while waiting
fenceWait.kickWait();  // Background thread waits on fence

while (!fenceWait.isReady()) {
    graphicsWake.wait();  // Proper CV sleep — wakes on mesh OR fence

    // Process any ready meshes (and other queues)
    while (auto mesh = meshQueue.tryPop()) {
        uploadMesh(*mesh);
    }
}

// Fence ready — detach so fence waiter doesn't wake us during render
fenceWait.detach();

// Phase 2: Acquire frame, process events, deadline meshes
auto frame = renderer->beginFrame(/*skipFenceWait=*/true);

window->pollEvents();  // GLFW events — may trigger new mesh rebuilds

auto framePeriod = recordFrameStart();
auto deadline = now() + framePeriod / 2;

// Continue processing meshes until deadline
// (catches quick-turn effects of user input in the same frame)
graphicsWake.setDeadline(deadline);
while (now() < deadline) {
    graphicsWake.wait();
    while (auto mesh = meshQueue.tryPop()) {
        uploadMesh(*mesh);
    }
}
graphicsWake.clearDeadline();

// Phase 3: Render and submit
recordDrawCommands(frame);
renderer->endFrame();

// Re-attach for next frame's fence wait
fenceWait.attach(&graphicsWake);
```

**Key design points:**
- The deadline starts when the fence completes and frame is acquired, not at the top of the loop
- GLFW events are processed after frame acquisition — this may trigger new mesh rebuilds (e.g. block placement)
- The deadline gives mesh workers time to respond to those input-driven changes before the frame renders
- All Vulkan operations (beginFrame, mesh uploads, draw commands, endFrame) stay on the graphics thread
- The graphics loop may wait on more queues than just meshes — the WakeSignal can live wherever makes most sense

### 4. Tests

**File:** `tests/test_frame_fence_waiter.cpp`

- `kickWait() with immediate completion` — verify isReady() becomes true
- `WakeSignal integration` — verify signal is fired when fence completes
- `Multiple kick cycles` — kickWait + isReady loop
- `Attach/detach` — verify signal only fires when attached
- `Shutdown while waiting` — verify clean exit, no deadlock
- `Shutdown with no pending work` — no deadlock

Note: Tests will need a mock or stub for `SimpleRenderer::waitForCurrentFrameFence()` since we don't have a real Vulkan device in unit tests. A simple approach: test with a thin wrapper or a fake renderer that just sleeps for a configurable duration.

## Critical Files

| File | Repo | Change |
|------|------|--------|
| `include/finevk/window/window.hpp` | FineStructureVK | Add `waitForCurrentFrameFence()`, `beginFrame(bool skipFenceWait)` |
| `src/window/window.cpp` | FineStructureVK | Implement both |
| `include/finevox/render/frame_fence_waiter.hpp` | FineStructureVoxel | New file |
| `src/render/frame_fence_waiter.cpp` | FineStructureVoxel | New file |
| `examples/render_demo.cpp` | FineStructureVoxel | Restructure main loop |
| `tests/test_frame_fence_waiter.cpp` | FineStructureVoxel | New test file |
| `CMakeLists.txt` | FineStructureVoxel | Add source + test |

## Verification

1. **Build finevk:** `cd FineStructureVK/build && cmake --build .`
2. **Build finevox:** `cd FineStructureVoxel/build && cmake --build .`
3. **Run tests:** `cd FineStructureVoxel/build && ctest` — all existing tests pass + new FrameFenceWaiter tests
4. **Run render_demo:** `./build/render_demo --worldgen`
   - Visual rendering unchanged
   - Verify with debug prints that mesh uploads happen DURING fence wait
   - All existing controls (F1-F6, G, V, M, L, C, T, B) work
   - Toggle async meshing (F6) — both modes work
   - Performance: should see smoother chunk loading, fewer frame drops during teleport

## Implementation Order

1. **finevk:** Add `waitForCurrentFrameFence()` + `beginFrame(bool skipFenceWait)` — build + test finevk
2. **finevox:** Create `FrameFenceWaiter` class + tests — build + test (can mock renderer for unit tests)
3. **finevox:** Restructure render_demo.cpp main loop — integration test with `--worldgen`

## Implementation Notes (Post-Completion)

### Additions beyond original plan:

1. **Timeout-based fence wait**: Added `waitForCurrentFrameFence(uint64_t timeoutNs)` overload to finevk Window and SimpleRenderer. FrameFenceWaiter loops with 100ms timeout in the renderer path, checking `running_` between iterations. This ensures clean shutdown (worst case 100ms) without needing to spoof GPU fences.

2. **Two-phase shutdown**: `stop()` split into `requestStop()` (non-blocking) + `join()` (blocking). Enables parallel thread shutdown — signal all threads to stop first, then join them all.

3. **setWaitFunction()**: Custom wait function for testing without a real Vulkan renderer. Tests use lambdas with atomics to control fence completion timing.

4. **setWaitTimeout()**: Configurable timeout for the renderer fence wait loop (default 100ms).

### Files actually created/modified:

| File | Change |
|------|--------|
| `include/finevox/render/frame_fence_waiter.hpp` | New: class with timeout, two-phase shutdown |
| `src/render/frame_fence_waiter.cpp` | New: implementation |
| `tests/test_frame_fence_waiter.cpp` | New: 16 tests |
| `examples/render_demo.cpp` | 3-phase render loop, fence waiter integration |
| `CMakeLists.txt` | Added source + test |
| finevk `window.hpp` | Added `waitForCurrentFrameFence(uint64_t)` overload |
| finevk `window.cpp` | Implementation |
| finevk `simple_renderer.hpp/.cpp` | Delegating timeout overload |

### Test count: 1214 total (16 new fence waiter tests)
