# Plan: Remove Legacy Pull Architecture

## Current State: Two Overlapping Architectures

The codebase has two mesh rebuild architectures running in parallel:

### Push Path (production, Phase 14+)
```
Block mutation (game thread)
    |
    +---> World::enqueueLightingUpdateWithRemesh()
              |
              +-- [if queue empty] --> LightEngine enqueue (triggerMeshRebuild=true)
              |                            |
              |                            v
              |                        LightEngine worker thread
              |                            |
              |                            v
              |                        flushAffectedChunks()
              |                            |
              |                            v
              |                        meshRebuildQueue_.push(pos, request)
              |
              +-- [if queue busy] --> meshRebuildQueue_.push(pos, request)  [immediate]
                                      + LightEngine enqueue (triggerMeshRebuild=false)

MeshRebuildQueue (KeyedQueue<ChunkPos, MeshRebuildRequest>)
    |
    v
MeshWorkerPool threads (N workers)
    |  pop request, build mesh
    v
MeshUploadQueue (Queue<MeshUploadData>)
    |
    v
Graphics thread: worldRenderer.updateMeshes()
    |  pop upload, send to GPU
    v
SubChunkView: upload/update Vulkan buffers
```

### Pull Path (legacy, being removed)
```
Graphics thread (every frame):
    for each SubChunkView in views_:
        if subchunk.blockVersion() != view.lastBuiltVersion_:
            rebuild mesh (sync: inline, async: push to queue)
        if subchunk.lightVersion() != view.lastBuiltLightVersion_:
            rebuild mesh

    for each pos in dirtyChunks_:
        rebuild mesh
```

### The Hybrid Problem

Even `updateMeshesAsync()` (lines 295-314) polls every view every frame comparing versions:
```cpp
for (auto& [pos, view] : views_) {
    if (view->needsRebuild(currentBlockVersion, currentLightVersion, lodRequest))
        meshRebuildQueue_->push(pos, request);
}
```
This polling is **redundant** for block/light changes (already pushed by game thread and LightEngine). It currently serves **two purposes**:
1. **Safety net** for missed pushes (shouldn't happen if push path is correct)
2. **LOD transitions** when camera moves (no push mechanism exists for this)

---

## What Gets Removed

### 1. Synchronous mesh rebuild path (`updateMeshes` sync branch)
- **world_renderer.cpp lines 169-290**: The entire sync path (poll views, poll dirty list, build mesh inline, upload)
- This is the code that runs when `meshWorkerPool_ == nullptr`

### 2. `dirtyChunks_` vector and related methods
- **world_renderer.hpp**: `std::vector<ChunkPos> dirtyChunks_` member
- **world_renderer.cpp**: `markDirty()`, `markColumnDirty()`, `markAllDirty()` implementations
- **world_renderer.cpp**: `unloadAll()` clearing `dirtyChunks_`
- **world_renderer.cpp**: The dirtyChunks_ scan in `updateMeshesAsync()` (lines 317-340)

### 3. `SubChunkView` legacy dirty interface
- **subchunk_view.hpp**: `markDirty()`, `clearDirty()`, `isDirty()` (deprecated methods)

### 4. Async meshing toggle
- **world_renderer.hpp**: `enableAsyncMeshing()`, `disableAsyncMeshing()`, `asyncMeshingEnabled()`
- Async meshing becomes the **only** path (always enabled)
- `MeshWorkerPool` created unconditionally in WorldRenderer constructor (or init)

### 5. Version-comparison polling loop in `updateMeshesAsync()`
- **world_renderer.cpp lines 295-314**: The per-frame all-views version polling loop

### 6. Version tracking in SubChunkView (for block/light staleness)
- **subchunk_view.hpp**: `lastBuiltVersion_`, `lastBuiltLightVersion_` members
- **subchunk_view.hpp**: `setLastBuiltVersion()`, `setLastBuiltLightVersion()`, `lastBuiltVersion()`, `lastBuiltLightVersion()`
- **subchunk_view.hpp**: `needsBlockRebuild()`, `needsLightRebuild()`, `needsRebuild()` overloads that use versions

**Rationale**: If a chunk is modified while a mesh is being built, the modifier already pushes a new rebuild request to the queue. Under light load this may cause one redundant rebuild (harmless). Under load, the consolidating queue merges requests. No need to detect staleness on upload — the push path guarantees the fresh request is already queued.

### 7. Version fields in MeshRebuildRequest and MeshUploadData
- **mesh_rebuild_queue.hpp**: `targetVersion`, `targetLightVersion` fields removed from `MeshRebuildRequest`
- **mesh_worker_pool.hpp**: `blockVersion`, `lightVersion` fields removed from `MeshUploadData`
- Merge function simplifies to: keep highest priority + latest LOD request

**What stays**: `SubChunk::blockVersion()` / `lightVersion()` atomics remain in core (useful for debugging, persistence, other consumers). They just aren't read by the render system anymore.

### 8. render_demo.cpp changes
- Remove F6 toggle (sync/async meshing switch)
- Replace all `markAllDirty()` calls with `rebuildAllMeshes()`
- Remove sync meshing fallback in main loop (lines 1210-1212)

---

## What Gets Kept

### 1. `SubChunk::blockVersion()` / `lightVersion()` atomics
- Remain in core — incremented on mutation
- NOT read by render system after this change
- Useful for debugging, persistence, and other consumers

### 2. `SubChunkView::lastBuiltLOD()` / `setLastBuiltLOD()`
- Still needed for LOD change detection during render

### 3. LOD-related `SubChunkView` methods
- `satisfiesLODRequest()`, `needsLODChange()` — used during render to detect wrong LOD

---

## What Gets Added / Changed

### 1. LOD checking during render loop

The render loop already iterates all visible views (line 438) to issue draw calls. LOD checking piggybacks on this — essentially free since we already look at each view:

```cpp
void WorldRenderer::render(finevk::CommandBuffer& cmd) {
    // ... existing setup ...

    for (auto& [pos, view] : views_) {
        if (!view->hasGeometry()) continue;

        // Frustum culling (existing)
        if (!config_.disableFrustumCulling && !isInFrustum(pos)) { ... }

        // View distance culling (existing)
        if (!isInViewDistance(pos)) { ... }

        // LOD check — piggyback on render iteration
        if (lodEnabled_) {
            float distBlocks = LODConfig::distanceToChunk(highPrecisionCameraPos_, pos);
            LODRequest lodRequest = lodConfig_.getRequestForDistance(distBlocks);
            if (!lodRequest.accepts(view->lastBuiltLOD())) {
                uint32_t priority = (distBlocks < 32.0f) ? 0 : 100;
                meshRebuildQueue_->push(pos, MeshRebuildRequest(priority, lodRequest));
            }
        }

        // ... existing push constants, bind, draw ...
    }
}
```

The fuzzy LOD matching (hysteresis) prevents constant re-queuing. A chunk at a LOD boundary won't flip-flop because `LODRequest::accepts()` allows either neighboring level. The consolidating queue merges any duplicate requests.

### 2. Safety net: lighting thread background scan

The lighting thread is the natural home for the safety net. It already owns light verification and produces remesh requests as a side effect. Lighting is server-side (used by game mechanics like mob spawning), so this scan stays in the lighting thread even in networked architectures.

**Alarm-based timing**: Instead of `wait_for` timeouts, the lighting queue uses alarm support (same pattern as `Queue<BlockEvent>` in the game thread). The alarm wakes the thread periodically for background scan work, while real lighting updates wake it immediately:

```cpp
// LightingQueue additions:
void setAlarm(std::chrono::steady_clock::time_point tp);
// dequeueBatch() already waits on CV — now also respects alarm time_point
```

**Inbox/outbox double-buffered scan tracking**: Two `unordered_set<ChunkPos>` track which subchunks need scanning:

- **inbox**: The current scan agenda. The thread pulls entries from this set one at a time.
- **outbox**: Accumulates subchunks that have been scanned or touched by real lighting work.
- When the inbox is empty, swap inbox ↔ outbox.
- If a subchunk pulled from inbox no longer exists in the world, it's dropped (stale entry).
- Any subchunk touched by real lighting work (in `flushAffectedChunks()`) is inserted into the outbox, so newly loaded or modified chunks are always picked up in the next cycle.

This gives natural deduplication (set semantics), random ordering (hash iteration), and ensures every loaded chunk is eventually visited.

```cpp
// LightEngine members:
std::unordered_set<ChunkPos> scanInbox_;   // current scan agenda
std::unordered_set<ChunkPos> scanOutbox_;  // accumulates for next cycle
```

**Lighting thread loop changes**:

```cpp
void LightEngine::lightingThreadLoop() {
    using Clock = std::chrono::steady_clock;
    auto nextScanTime = Clock::now() + computeScanInterval();
    queue_.setAlarm(nextScanTime);

    while (/* not stopped */) {
        auto batch = queue_.dequeueBatch(batchSize_);

        if (!batch.empty()) {
            // Process real lighting updates (existing code)
            batchAffectedChunks_.clear();
            for (const auto& update : batch) {
                processLightingUpdate(update);
            }
            // flushAffectedChunks() pushes remesh AND adds positions to scanOutbox_
            flushAffectedChunks();
        }

        // Background scan: if alarm fired, scan one entry
        if (Clock::now() >= nextScanTime) {
            backgroundScanStep();
            nextScanTime = Clock::now() + computeScanInterval();
            queue_.setAlarm(nextScanTime);
        }
    }
}
```

**Adaptive scan interval**: The alarm interval is derived from the total tracked subchunk count (inbox + outbox) so every subchunk is visited at a steady rate regardless of world size. Using the combined size keeps the interval stable throughout a cycle — if we used inbox alone, the interval would stretch as entries move from inbox to outbox. Target: one full cycle every ~8 minutes (`SCAN_CYCLE_SECONDS = 480`). Clamped to a minimum (100ms — don't spin on tiny worlds) and maximum (2s — don't go completely idle on huge worlds):

```cpp
std::chrono::milliseconds LightEngine::computeScanInterval() const {
    size_t count = scanInbox_.size() + scanOutbox_.size();
    if (count == 0) count = 1;

    // ms per entry = (cycle_seconds * 1000) / count
    int64_t ms = (SCAN_CYCLE_SECONDS * 1000) / static_cast<int64_t>(count);
    ms = std::clamp(ms, int64_t(100), int64_t(2000));
    return std::chrono::milliseconds(ms);
}
```

**Background scan step**:

```cpp
void LightEngine::backgroundScanStep() {
    if (!meshRebuildQueue_) return;

    // If inbox is empty, swap with outbox (start new cycle)
    if (scanInbox_.empty()) {
        // Move semantics — zero copy (unordered_set swap is O(1))
        scanInbox_ = std::move(scanOutbox_);
        scanOutbox_.clear();  // outbox is now moved-from; reset to empty

        // Seed outbox with all currently loaded subchunks for next cycle
        // (picks up newly loaded chunks)
        for (const auto& pos : world_.getAllSubChunkPositions()) {
            scanOutbox_.insert(pos);
        }
    }

    if (scanInbox_.empty()) return;

    // Pull one entry from inbox
    auto it = scanInbox_.begin();
    ChunkPos pos = *it;
    scanInbox_.erase(it);

    // Copy to outbox (so it appears in next cycle)
    scanOutbox_.insert(pos);

    // Drop stale entries (subchunk no longer loaded)
    if (!world_.hasSubChunk(pos)) return;

    // Skip subchunks too far from any player to matter
    if (!isNearAnyPlayer(pos)) return;

    // Push background-priority remesh
    meshRebuildQueue_->push(pos, MeshRebuildRequest::background());
}
```

**Player distance culling**: Subchunks far from all players are skipped during the scan. They'll remain in the inbox/outbox cycle and be checked again next time, but no remesh is pushed for them. This keeps the scan cost proportional to the player-relevant world, not total loaded world:

```cpp
bool LightEngine::isNearAnyPlayer(ChunkPos pos) const {
    // playerPositions_ updated periodically by game thread (or read from EntityManager)
    for (const auto& playerPos : playerPositions_) {
        float dist = LODConfig::distanceToChunk(playerPos, pos);
        if (dist <= scanRadius_) return true;  // scanRadius_ = view distance + margin
    }
    return playerPositions_.empty();  // if no players tracked, scan everything
}
```

**Scan rate**: With adaptive intervals, a world with 1000 near-player subchunks completes a full cycle in ~8 minutes (480ms per entry). A tiny 10-chunk world checks one entry every 2s (clamped max). A huge 50,000-chunk world checks one entry every 100ms (clamped min), completing a cycle in ~83 minutes — acceptable for a pure safety net.

**`flushAffectedChunks()` integration**: After flushing remesh requests for real lighting work, add each affected position to `scanOutbox_`:

```cpp
void LightEngine::flushAffectedChunks() {
    for (const auto& pos : batchAffectedChunks_) {
        meshRebuildQueue_->push(pos, MeshRebuildRequest::immediate());
        scanOutbox_.insert(pos);  // ensure it appears in next scan cycle
    }
    batchAffectedChunks_.clear();
}
```

**Network-forward design**: In a networked architecture, the server runs this scan authoritatively (lighting is server-side) and pushes corrections one-way to clients. Clients don't scan — they receive authoritative remesh commands. The scan stays in the lighting thread on the server; the remesh output path (meshRebuildQueue) is identical.

### 3. `WorldRenderer::rebuildAllMeshes()`

Replaces `markAllDirty()` — directly pushes rebuild requests for all loaded chunks:

```cpp
void WorldRenderer::rebuildAllMeshes() {
    // Push all existing views
    for (auto& [pos, view] : views_) {
        meshRebuildQueue_->push(pos,
            MeshRebuildRequest::normal(LODRequest::exact(view->lastBuiltLOD())));
    }
    // Also queue any world subchunks not yet in views_
    for (const auto& pos : world_.getAllSubChunkPositions()) {
        if (views_.find(pos) == views_.end()) {
            meshRebuildQueue_->push(pos, MeshRebuildRequest::normal());
        }
    }
}
```

### 4. Simplified `MeshRebuildRequest`

Remove version fields entirely. Merge function simplifies:

```cpp
struct MeshRebuildRequest {
    uint32_t priority = 100;
    LODRequest lodRequest = LODRequest::exact(LODLevel::LOD0);

    MeshRebuildRequest() = default;
    MeshRebuildRequest(uint32_t prio, LODRequest lod)
        : priority(prio), lodRequest(lod) {}

    static MeshRebuildRequest immediate(LODRequest lod = LODRequest::exact(LODLevel::LOD0)) {
        return MeshRebuildRequest(0, lod);
    }
    static MeshRebuildRequest normal(LODRequest lod = LODRequest::exact(LODLevel::LOD0)) {
        return MeshRebuildRequest(100, lod);
    }
    static MeshRebuildRequest background(LODRequest lod = LODRequest::exact(LODLevel::LOD0)) {
        return MeshRebuildRequest(1000, lod);
    }
};

inline MeshRebuildRequest mergeMeshRebuildRequest(
    const MeshRebuildRequest& existing,
    const MeshRebuildRequest& newReq)
{
    return MeshRebuildRequest(
        std::min(existing.priority, newReq.priority),  // Keep highest urgency
        newReq.lodRequest  // Use latest LOD request
    );
}
```

### 5. Simplified `MeshUploadData`

Remove version fields:

```cpp
struct MeshUploadData {
    ChunkPos pos;
    MeshData mesh;
    LODLevel lodLevel = LODLevel::LOD0;
};
```

### 6. `WorldRenderer` always creates `MeshWorkerPool`

Instead of lazily creating via `enableAsyncMeshing()`, the worker pool is always created during `WorldRenderer::init()`. The `enableAsyncMeshing()` / `disableAsyncMeshing()` methods are removed.

### 7. `updateMeshes()` simplification

The single entry point becomes:

```cpp
void WorldRenderer::updateMeshes(uint32_t maxUpdates) {
    if (!initialized_) return;

    // Pop completed meshes from upload queue -> GPU
    uint32_t uploads = 0;
    while (maxUpdates == 0 || uploads < maxUpdates) {
        auto uploadData = meshWorkerPool_->tryPopUpload();
        if (!uploadData) break;

        const ChunkPos& pos = uploadData->pos;
        SubChunkView* view = getOrCreateView(pos);

        if (uploadData->mesh.isEmpty()) {
            view->release();
            view->setLastBuiltLOD(uploadData->lodLevel);
            ++uploads;
            continue;
        }

        if (view->canUpdateInPlace(uploadData->mesh)) {
            view->update(*renderer_->commandPool(), uploadData->mesh);
        } else {
            view->upload(*device_, *renderer_->commandPool(), uploadData->mesh,
                         config_.meshCapacityMultiplier);
        }
        view->setLastBuiltLOD(uploadData->lodLevel);
        ++uploads;
    }
}
```

No more polling. No more dirty list. No more version comparisons. Just drain the upload queue. LOD changes detected during `render()`. Safety net runs in the lighting thread.

---

## End-to-End Push Architecture (After Cleanup)

```
GRAPHICS THREAD                    GAME THREAD                 LIGHTING THREAD       MESH WORKERS
==============                     ===========                 ===============       ============
pollEvents()
playerController.update()
  |
  +--[sendPlayerState]-----------> commandQueue
  +--[breakBlock/placeBlock]-----> commandQueue
                                       |
                                   drainAll()
                                       |
                                   executeCommand()
                                       |
                                   world.breakBlock()/placeBlock()
                                       |
                                   updateScheduler.pushExternalEvent()
                                       |
                                   scheduler.processEvents()
                                       |
                                   world.setBlock() + enqueueLightingUpdateWithRemesh()
                                       |
                                       +------[LightingUpdate]-------> lightEngine.enqueue()
                                       |                                     |
                                       +------[MeshRebuildRequest]---->  meshRebuildQueue_.push()
                                       |  (if queue busy)                    |  (if deferred)
                                       |                                     v
                                       |                               propagateLight()
                                       |                               flushAffectedChunks()
                                       |                                     |
                                       |                                     v
                                       |                              meshRebuildQueue_.push()
                                       |                                     |
                                       |                               [alarm fires]
                                       |                               backgroundScanStep()
                                       |                               (inbox/outbox sets)
                                       |                                     |
                                       |                                     v
                                       |                              meshRebuildQueue_.push()
                                       |                              (background priority)
                                       |                                     |
                                       |                                     v
                                       |                              +--------------+
                                       |                              | MeshRebuild  |
                                       |                              | Queue        |
                                       +----------------------------->| (KeyedQueue) |
                                                                      +--------------+
                                                                             |
                                                                         pop request
                                                                             |
                                                                             v
                                                                      [Worker N] buildMesh()
                                                                             |
                                                                             v
                                                                      MeshUploadQueue.push()
                                                                             |
updateMeshes()                                                               |
  |                                                                          |
  +-- pop uploadQueue <------------------------------------------------------+
  |     |
  |     +-- upload to GPU
  |
render()
  |
  +-- for each view: draw
  |     +-- LOD check --> meshRebuildQueue_.push() if wrong LOD
```

### Thread ownership (after cleanup):
| Resource | Owner Thread | Access Pattern |
|----------|-------------|---------------|
| World (blocks) | Game thread (writes) | shared_mutex (reads from any thread) |
| UpdateScheduler | Game thread | Single-threaded |
| LightEngine queue | Game thread (push) | LightEngine thread (pop + background scan) |
| MeshRebuildQueue | Game/Light/Graphics threads (push) | Worker threads (pop) |
| MeshUploadQueue | Worker threads (push) | Graphics thread (pop) |
| GraphicsEventQueue | Game thread (push) | Graphics thread (pop) |
| SoundEventQueue | Graphics thread (push) | Audio consumer (pop) |
| GameCommandQueue | Graphics thread (push) | Game thread (pop) |
| SubChunkView (GPU) | Graphics thread only | Single-threaded |

---

## Implementation Steps

### Step 1: Simplify `MeshRebuildRequest` and `MeshUploadData`
- Remove `targetVersion` and `targetLightVersion` from `MeshRebuildRequest`
- Update factory methods (`immediate()`, `normal()`, `background()`) to drop version params
- Simplify merge function to just priority + LOD
- Remove `blockVersion` and `lightVersion` from `MeshUploadData`
- Update all push sites in world.cpp and light_engine.cpp to use new API
- Update MeshWorkerPool worker loop to not pass versions

### Step 2: Make `MeshWorkerPool` non-optional
- `WorldRenderer::init()` always creates worker pool
- Remove `enableAsyncMeshing()`, `disableAsyncMeshing()`, `asyncMeshingEnabled()`
- Public API: `meshWorkerPool()` always returns non-null after init
- Keep `wakeSignal()` (always valid after init)

### Step 3: Add LOD checking in render loop
- In `render()`, after distance/culling checks, check LOD and push rebuild if wrong
- Use fuzzy matching (hysteresis) to avoid re-queuing

### Step 4: Add lighting thread background scan
- Add alarm support to `LightingQueue` (same pattern as `Queue<BlockEvent>::setAlarm()`)
- Add `scanInbox_` and `scanOutbox_` (`unordered_set<ChunkPos>`) members to `LightEngine`
- Inbox→outbox transition uses `std::move` (zero-copy O(1) swap)
- Update `lightingThreadLoop()` to use alarm-based timing — alarm fires trigger `backgroundScanStep()`
- `backgroundScanStep()`: pull one entry from inbox, push background remesh; move outbox→inbox when inbox empty
- `computeScanInterval()`: adaptive — `SCAN_CYCLE_SECONDS * 1000 / (inbox.size() + outbox.size())`, clamped to [100ms, 2s]
- Skip subchunks far from all players (`isNearAnyPlayer()` with view distance + margin)
- `flushAffectedChunks()`: add affected positions to `scanOutbox_` after pushing remesh requests
- Drop stale inbox entries (subchunk no longer loaded in world)
- `playerPositions_` updated periodically by game thread or read from EntityManager

### Step 5: Add `rebuildAllMeshes()`
- Replaces `markAllDirty()`
- Pushes all loaded views + world subchunks not yet in views_ to meshRebuildQueue_

### Step 6: Simplify `updateMeshes()`
- Remove sync path (lines 169-290)
- Remove version-polling loop (lines 295-314)
- Remove dirtyChunks_ scanning (lines 317-340)
- New body: just drain upload queue (no version checks, no heartbeat)

### Step 7: Remove legacy members
- `dirtyChunks_` vector
- `markDirty()`, `markColumnDirty()`, `markAllDirty()` methods
- `SubChunkView::markDirty()`, `clearDirty()`, `isDirty()`
- `SubChunkView::lastBuiltVersion_`, `lastBuiltLightVersion_` and their accessors
- `SubChunkView::needsBlockRebuild()`, `needsLightRebuild()`, all version-based `needsRebuild()` overloads
- Keep `lastBuiltLOD_` and LOD-related methods

### Step 8: Update render_demo.cpp
- Remove F6 key handler (no more sync/async toggle)
- Replace `markAllDirty()` calls with `rebuildAllMeshes()` (lines 644, 816, 848, 907, 945)
- Remove sync meshing fallback (lines 1210-1212) — always use deadline-based path
- Remove `asyncMeshingEnabled()` checks
- Worker pool setup moves into WorldRenderer init (no more conditional in render_demo)
- Simplify MeshRebuildQueue wiring (no longer conditional on async mode)

### Step 9: Update tests
- Update `test_mesh_worker_pool.cpp` if it references version fields
- Remove any tests that exercise `markDirty()` on WorldRenderer (if any)
- Version tracking tests in `test_subchunk.cpp` stay (versions still used in core)
- Add test for `LightingQueue` alarm-based wakeup behavior
- Add test for inbox/outbox scan cycle (swap, stale entry drop)

### Step 10: Build + test
- `cmake --build .` — clean compile
- `ctest` — all tests pass
- `./render_demo --worldgen` — verify block break/place, LOD transitions, lighting, no regressions

---

## Risk Assessment

**Low risk:**
- Sync path removal: nobody uses it in production (async is default)
- `dirtyChunks_` removal: redundant with push path
- `markDirty` removal: only used by render_demo for things that `rebuildAllMeshes()` handles
- Always-on worker pool: small overhead even when idle (threads sleep on queue)
- Version field removal from requests: workers always read current world state anyway

**Low risk (with lighting thread scan):**
- Version polling removal: replaced by lighting thread background scan
- If a push is ever missed, scan catches it within minutes (very low overhead)
- Under the push architecture, missed pushes shouldn't happen — scan is pure insurance
- Catches both lighting staleness AND mesh staleness (rebuilds from current world state)

**Low risk:**
- LOD during render: cheaper than separate scan (we already iterate views). Fuzzy matching prevents churn.

**Network-forward:**
- In networked mode, server does authoritative scan → pushes corrections one-way to clients
- Clients don't need their own scan — they receive authoritative updates
- Same meshRebuildQueue output path, just different input source

---

## Verification Checklist

- [ ] Block break/place triggers mesh rebuild (push path)
- [ ] Lighting changes trigger mesh rebuild (push path via LightEngine)
- [ ] LOD transitions work when walking (detected during render)
- [ ] Greedy meshing toggle rebuilds all meshes
- [ ] LOD toggle rebuilds all meshes
- [ ] Lighting mode toggle rebuilds all meshes
- [ ] Initial world load shows all chunks
- [ ] No visual glitches from stale builds
- [ ] Background scan runs on alarm when lighting thread is idle
- [ ] Inbox/outbox swap works correctly (new cycle picks up all loaded chunks)
- [ ] Stale scan entries dropped (unloaded chunks skipped)
- [ ] Real lighting work populates outbox (flushAffectedChunks integration)
- [ ] Clean shutdown (no deadlocks — alarm doesn't block stop)
- [ ] All existing tests pass
