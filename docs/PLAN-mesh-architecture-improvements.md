# Mesh Architecture Improvements Plan

## Current State

The light version tracking implementation is complete, but with push-based architecture, **versions become unnecessary**. The push model provides implicit ordering guarantees.

## Target Architecture: Fully Push-Based

### Data Flow

```
User Input (graphics thread)
    │
    ▼
Block Updates (game logic)
    │
    ├──────────────────────────────┐
    │                              │
    ▼                              ▼
Lighting Queue ◄─────────────  Mesh Request Queue
    │         (defer if empty)     │
    ▼                              │
Lighting Thread                    │
    │                              │
    ├── affected subchunks ────────┤
    │                              │
    ▼                              ▼
              Mesh Worker Threads
                      │
                      ▼
              Upload Queue ──────► Graphics Thread
                                  (sleep until deadline)
```

### Key Design Points

1. **No version polling** - Changes push through queues, not discovered by scanning
2. **Deferred to lighting thread** - When lighting queue is empty, block changes defer to lighting thread which:
   - Determines affected subchunks (even if zero lighting change)
   - Sends correct mesh requests
   - Single rebuild captures block + light changes
3. **Deadline-based sleep** - Graphics thread sleeps on upload queue until render deadline, not fixed duration
4. **2D GUI in separate thread** - GUI overlay generates graphics updates via its own queue

### Graphics Thread Loop

```
while (running) {
    // 1. Acquire swapchain image (blocks until vsync/image available)
    acquireNextImage();

    // 2. Process user input → sends to game logic thread
    processInputEvents();

    // 3. Sleep on queues until deadline
    deadline = now + frameTime - renderMargin;
    while (now < deadline) {
        timeout = deadline - now;

        // Wait on both mesh upload queue and GUI queue
        if (uploadQueue.waitForWork(timeout)) {
            // Upload pending meshes
            while (auto mesh = uploadQueue.tryPop()) {
                uploadToGPU(*mesh);
            }
        }
        if (guiQueue.waitForWork(0)) {  // non-blocking check
            processGuiUpdates();
        }
    }

    // 4. Render and submit frame
    recordCommandBuffer();
    submitFrame();
}
```

### Lighting Thread Role (Enhanced)

When block changes occur:
1. If lighting queue is empty → game logic defers the whole change to lighting thread
2. Lighting thread processes the block change:
   - Computes lighting propagation (may affect 0 or more subchunks)
   - Determines all subchunks needing remesh (at minimum, the modified one)
   - Pushes mesh requests for all affected subchunks
3. Single mesh rebuild captures both block and light changes

### Mesh Worker Thread Loop

```
while (running) {
    if (auto request = meshQueue.tryPop()) {
        MeshData mesh = buildMesh(request);
        uploadQueue.push(request.pos, mesh);
        continue;
    }
    meshQueue.waitForWork();
}
```

---

## Implementation Phases

### Phase 1: Upload Queue
- Create `MeshUploadQueue` for workers to push completed meshes
- Graphics thread polls this queue instead of mesh cache
- Workers push to upload queue after building

### Phase 2: Lighting Thread Push
- Lighting thread pushes mesh requests after processing
- Track affected subchunks during light propagation
- Handle edge case: block change with zero lighting impact

### Phase 3: Game Logic Deferral
- When lighting queue empty, defer block changes to lighting thread
- Lighting thread becomes the single source of mesh requests for block changes
- Eliminates double-rebuild race

### Phase 4: Deadline-Based Sleep
- Replace fixed polling with deadline-aware sleep
- Graphics thread sleeps on upload queue
- Wake on deadline or queue activity

### Phase 5: GUI Thread (Optional)
- Move 2D overlay rendering to separate thread
- GUI thread pushes to graphics queue
- Graphics thread merges 3D and 2D work

---

## Files to Change

### Phase 1
- `include/finevox/core/mesh_worker_pool.hpp` - Add upload queue
- `src/core/mesh_worker_pool.cpp` - Workers push to upload queue
- `src/render/world_renderer.cpp` - Poll upload queue instead of cache

### Phase 2
- `include/finevox/core/light_engine.hpp` - Track affected subchunks
- `src/core/light_engine.cpp` - Push mesh requests after lighting
- May need reference to mesh request queue

### Phase 3
- `src/core/world.cpp` or game logic - Conditional deferral to lighting thread
- `include/finevox/core/lighting_queue.hpp` - Query for empty state

### Phase 4
- `src/render/world_renderer.cpp` - Deadline-based sleep loop
- May need new queue primitive for multi-queue wait

---

## Queue Infrastructure

### WakeSignal (Multi-Queue Wait Primitive)

A `WakeSignal` allows a consumer to sleep until any of multiple sources produce work.
Pattern inspired by condition variables and event loops (similar to `select()`/`epoll()`).

```cpp
class WakeSignal {
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> signaled_{false};
    std::atomic<bool> shutdown_{false};

    // Deadline support for timed waits
    std::chrono::steady_clock::time_point deadline_{time_point::max()};

public:
    /// Signal that work is available (called by producers)
    void signal();

    /// Request shutdown - all waits return false
    void requestShutdown();

    /// Set a deadline for automatic wakeup
    void setDeadline(std::chrono::steady_clock::time_point when);
    void clearDeadline();

    /// Block until signaled, deadline reached, or shutdown
    /// @return false if shutdown requested, true otherwise
    bool wait();

    /// Block with explicit timeout
    /// @return false if shutdown requested
    bool waitFor(std::chrono::milliseconds timeout);
};
```

### Generic Queue Types

Two queue types, both compatible with WakeSignal:

**SimpleQueue<T>** - FIFO queue, no deduplication
```cpp
template<typename T>
class SimpleQueue {
    std::mutex mutex_;
    std::deque<T> items_;
    WakeSignal* signal_ = nullptr;  // Optional, set via attach()

public:
    void attach(WakeSignal* signal);  // Connect to wake signal
    void detach();

    void push(T item);              // Signals if attached
    std::optional<T> tryPop();      // Non-blocking pop
    bool empty() const;
    size_t size() const;
    void shutdown();                // Wake consumers, reject further pushes
};
```

**CoalescingQueue<Key, Data>** - Deduplicating queue with merge
```cpp
template<typename Key, typename Data, typename Hash = std::hash<Key>>
class CoalescingQueue {
    using MergeFn = std::function<Data(const Data& existing, const Data& incoming)>;

    std::mutex mutex_;
    std::deque<Key> order_;                      // Insertion order
    std::unordered_map<Key, Data, Hash> items_;  // Dedup map
    MergeFn merge_;
    WakeSignal* signal_ = nullptr;

public:
    explicit CoalescingQueue(MergeFn merge);

    void attach(WakeSignal* signal);
    void detach();

    void push(Key key, Data data);  // Merges if exists, signals if attached
    std::optional<std::pair<Key, Data>> tryPop();
    bool empty() const;
    size_t size() const;
    void shutdown();
};
```

Both queue types:
- Call `signal_->signal()` on push when attached
- Support graceful shutdown
- Thread-safe with fine-grained locking

---

## Lighting Request Changes

### Deferred Flag

Add a `deferred` flag to lighting requests:

```cpp
struct LightingRequest {
    BlockPos pos;
    // ... existing fields ...

    bool deferred = false;  // NEW
    // If true: lighting thread MUST send mesh request for this subchunk
    //          even if there are zero lighting changes
    // If false: only send mesh request if lighting actually changed
};
```

**Usage:**
- Game logic sets `deferred = true` when lighting queue is empty (deferring block change)
- Lighting thread checks flag after processing:
  - If `deferred` and no lighting changes → still send mesh request for the source subchunk
  - If lighting changes → send mesh requests for all affected subchunks

---

## Updated Data Flow

```
User Input (graphics thread)
    │
    ▼
Block Updates (game logic)
    │
    ├── if lighting queue empty ──► Lighting Queue (deferred=true)
    │                                    │
    │                                    ▼
    │                              Lighting Thread
    │                                    │
    │                     ┌──────────────┴──────────────┐
    │                     │                             │
    │              lighting changes?              deferred flag?
    │                     │                             │
    │                     ▼                             ▼
    │              affected subchunks           source subchunk
    │                     │                             │
    │                     └──────────────┬──────────────┘
    │                                    │
    │                                    ▼
    └── if lighting queue busy ──► Mesh Request Queue (CoalescingQueue)
                                         │
                                         ▼
                                   Mesh Workers
                                         │
                                         ▼
                                   Upload Queue (SimpleQueue)
                                         │
                                         ▼
    ┌────────────────────────────────────┘
    │
    ▼
Graphics Thread
    │
    ├── uploadQueue.attach(&wakeSignal)
    ├── guiQueue.attach(&wakeSignal)
    │
    └── loop:
        wakeSignal.wait()
        while (mesh = uploadQueue.tryPop()) upload(mesh)
        while (gui = guiQueue.tryPop()) process(gui)
```

---

## Implementation Phases (Revised)

### Phase 0: Queue Infrastructure
- Implement `WakeSignal` with deadline support
- Implement `SimpleQueue<T>` with attach/detach
- Implement `CoalescingQueue<Key, Data>` with merge function
- Unit tests for all queue types

### Phase 1: Upload Queue
- Add `SimpleQueue<MeshUploadData>` to MeshWorkerPool
- Workers push completed meshes to upload queue
- Graphics thread polls upload queue

### Phase 2: Lighting Request Deferral
- Add `deferred` flag to `LightingRequest`
- Lighting thread tracks affected subchunks
- Lighting thread sends mesh requests (respecting deferred flag)
- Game logic defers to lighting when queue empty

### Phase 3: WakeSignal Integration
- Graphics thread creates WakeSignal
- Attaches upload queue (and later GUI queue)
- Replace polling with deadline-based `wait()` loop

### Phase 4: Remove Version Tracking (Cleanup)
- Remove `blockVersion`/`lightVersion` from MeshRebuildRequest
- Remove version tracking from MeshCacheEntry
- Remove staleness checking (now push-based)

---

## Files to Change

### Phase 0
- `include/finevox/core/wake_signal.hpp` - NEW: WakeSignal class
- `include/finevox/core/simple_queue.hpp` - NEW: SimpleQueue template
- `include/finevox/core/coalescing_queue.hpp` - NEW: CoalescingQueue template
- `tests/test_queue_primitives.cpp` - NEW: Unit tests

### Phase 1
- `include/finevox/core/mesh_worker_pool.hpp` - Add upload queue
- `src/core/mesh_worker_pool.cpp` - Workers push to upload queue

### Phase 2
- `include/finevox/core/lighting_queue.hpp` - Add deferred flag
- `src/core/light_engine.cpp` - Track affected chunks, send mesh requests
- Game logic (World or similar) - Conditional deferral

### Phase 3
- `src/render/world_renderer.cpp` - WakeSignal-based wait loop

### Phase 4
- Various files - Remove version tracking code
