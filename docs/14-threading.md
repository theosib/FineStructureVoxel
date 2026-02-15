# 14. Threading Model

[Back to Index](INDEX.md) | [Previous: Batch Operations API](13-batch-operations.md)

---

## 14.1 Thread Responsibilities

| Thread | Responsibility |
|--------|----------------|
| **Graphics Thread** | Window events, input, mesh uploads, rendering, frame submission |
| **Fence Wait Thread** | Blocks on GPU fence, signals WakeSignal when frame slot available |
| **Mesh Workers (N)** | Greedy meshing, vertex generation, LOD mesh building |
| **Light Engine** | BFS light propagation (sky + block light) |
| **Audio Thread** | Sound mixing and playback (miniaudio internal) |

All Vulkan operations stay on the graphics thread. Background threads never issue Vulkan commands.

---

## 14.2 Concurrency Primitives

All queue classes live in `include/finevox/core/` and share a common pattern: WakeSignal attachment via `attach()`/`detach()`, non-blocking `tryPop()`, batch drain, and `shutdown()` for lifecycle.

**Planned migration:** These primitives will move into the `finenet` library and become a mandatory dependency. The API will remain the same; only the namespace/include paths change.

### 14.2.1 WakeSignal

Multi-queue wake mechanism. A single consumer blocks until *any* attached producer signals, a deadline fires, or shutdown is requested.

```cpp
class WakeSignal {
    void signal();                          // Wake the consumer
    bool wait();                            // Block until signaled/deadline/shutdown
    bool waitFor(duration timeout);         // Block with timeout
    void setDeadline(time_point when);      // Auto-wake at deadline
    void clearDeadline();
    void requestShutdown();                 // All wait() calls return false
};
```

### 14.2.2 Queue<T>

Thread-safe FIFO with alarm support and WakeSignal attachment. The primary queue type.

```cpp
template<typename T>
class Queue {
    void attach(WakeSignal* signal);        // Wire to multi-queue consumer
    void push(T item);                      // Push + signal
    void pushBatch(vector<T> items);        // Atomic batch push
    optional<T> tryPop();                   // Non-blocking
    vector<T> drainAll();                   // Pop everything
    vector<T> drainUpTo(size_t max);        // Pop up to N
    void setAlarm(time_point wakeTime);     // Timed wakeup
    void shutdown();                        // Signal consumers to exit
};
```

### 14.2.3 KeyedQueue<K,D>

Deduplicating queue with merge semantics. When a duplicate key is pushed, the data is merged with the existing entry (default: replace). Maintains FIFO order.

```cpp
template<typename K, typename D>
class KeyedQueue {
    // Same attach/detach/shutdown/alarm API as Queue
    bool push(K key, D data);              // Returns true if new key
    optional<pair<K,D>> tryPop();
    bool contains(K key);
};
```

### 14.2.4 SimpleQueue<T>

Lightweight FIFO without alarm/waitForWork — only WakeSignal attachment and non-blocking operations.

### 14.2.5 CoalescingQueue<K,D>

Older variant of KeyedQueue with same dedup semantics. Both exist; KeyedQueue adds alarm and blocking wait.

---

## 14.3 Graphics Thread — 3-Phase Render Loop

The graphics thread uses a 3-phase loop that overlaps mesh processing with GPU fence waiting:

```
Phase 1: Fence wait + mesh overlap
  ├── FrameFenceWaiter kicks background fence wait
  ├── WakeSignal.wait() — wakes on mesh arrival OR fence ready
  ├── Upload meshes as they arrive (no deadline)
  └── When fence ready → detach waiter, move to Phase 2

Phase 2: Input + world updates + deadline meshes
  ├── beginFrame(skipFenceWait=true)
  ├── pollEvents() — GLFW input
  ├── World updates (time, ticks, LOD, block updates)
  ├── Set deadline = now + framePeriod/2
  ├── Process meshes until deadline (catches input-driven rebuilds)
  └── When deadline → move to Phase 3

Phase 3: Render + submit
  ├── Record draw commands
  ├── endFrame()
  └── Re-attach fence waiter for next frame
```

**Key design points:**
- All Vulkan operations on graphics thread (beginFrame, mesh uploads, draw, endFrame)
- Fence wait is the *only* background operation — pure CPU blocking, no Vulkan commands
- The WakeSignal unifies mesh queue arrivals and fence completion into a single wait point
- Deadline starts after frame acquisition, not at loop top

### FrameFenceWaiter

Wraps GPU fence wait to integrate with WakeSignal. Lives in `finevox_render`.

```cpp
class FrameFenceWaiter {
    void setRenderer(SimpleRenderer* renderer);
    void setWaitFunction(function<void()> fn);  // For testing
    void setWaitTimeout(uint64_t timeoutNs);    // Default 100ms
    void start();                                // Launch thread
    void kickWait();                             // Begin async fence wait
    bool isReady() const;                        // Lock-free poll
    void attach(WakeSignal* signal);             // Signal on completion
    void detach();                               // Stop signaling
    void requestStop();                          // Non-blocking stop request
    void join();                                 // Block until thread exits
    void stop();                                 // requestStop() + join()
};
```

**Timeout-based fence wait:** The renderer path loops with a configurable timeout (default 100ms), checking `running_` between iterations. This ensures clean shutdown without needing to spoof the GPU fence. Custom wait functions (for testing) are called directly without timeout.

---

## 14.4 Two-Phase Shutdown Pattern

For parallel thread shutdown, use requestStop() on all threads first, then join() them all:

```cpp
// Phase 1: Signal all threads to stop (non-blocking)
fenceWaiter.requestStop();
audioEngine.stop();        // AudioEngine has its own shutdown
lightEngine.stop();        // LightEngine uses stop() (no split API yet)

// Phase 2: Join all threads
fenceWaiter.join();
```

This parallelizes shutdown wait across threads — each thread can finish its current work concurrently rather than sequentially.

**Classes supporting two-phase shutdown:**
- `FrameFenceWaiter`: requestStop() + join()
- `WakeSignal`: requestShutdown() wakes all waiters

**Classes using single stop():**
- `LightEngine`: stop() blocks until thread exits
- `MeshWorkerPool`: stop() blocks until all workers exit

---

## 14.5 Thread Communication Patterns

### Producer → Consumer via Queue + WakeSignal

```
Mesh Workers ──push()──→ MeshUploadQueue ──attach()──→ WakeSignal
                                                           ↑
FrameFenceWaiter ──signal()────────────────────────────────┘
                                                           │
Graphics Thread ←──wait()──────────────────────────────────┘
```

The graphics thread calls `wakeSignal.wait()` which blocks until:
1. A mesh worker pushes to the upload queue (queue signals WakeSignal)
2. The fence waiter completes (directly signals WakeSignal)
3. A deadline fires (WakeSignal's internal deadline)
4. Shutdown is requested

### Block Changes → Mesh Rebuilds

```
Game Thread ──markDirty()──→ KeyedQueue<SubChunkKey, RebuildData>
                                       │
                             Mesh Workers pop + build
                                       │
                             MeshUploadQueue ──→ Graphics Thread
```

KeyedQueue deduplicates: if the same subchunk is dirtied multiple times before a worker picks it up, only one rebuild happens with merged data.

---

## 14.6 Networking Integration (Planned)

When finenet is integrated, the threading model extends naturally:

```
Network Thread ──push()──→ Incoming Message Queue ──attach()──→ WakeSignal
                                                                    ↑
Mesh Workers ──push()──→ MeshUploadQueue ──attach()─────────────────┘
                                                                    ↑
FrameFenceWaiter ──signal()─────────────────────────────────────────┘
                                                                    │
Graphics Thread ←──wait()───────────────────────────────────────────┘
```

The graphics thread's WakeSignal will unify mesh uploads, fence completion, *and* network message arrival into a single wait point. No additional polling or busy-waiting needed.

**Local transport (single-player):** finenet's local transport uses in-process MPSC queues — the same Queue/WakeSignal primitives. Single-player code uses the identical Connection API as multiplayer; only the transport differs (in-process queues vs. UDP/TCP sockets).

**QueueBridge<T>:** A generic adapter that drains a local Queue<T> and sends messages to a network channel, or receives network messages and pushes to a local queue. This wires game subsystems (sound events, entity snapshots, block changes) to network channels without subsystem-specific networking code.

---

[Next: FineStructureVK Integration](15-finestructurevk-integration.md)
