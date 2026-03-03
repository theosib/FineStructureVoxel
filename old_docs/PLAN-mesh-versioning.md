# Refactor: Version-Based Pull Model for Mesh Updates

## Overview

Replace the current push-based dirty notification system with a pull-based version comparison model. The render loop discovers stale meshes by comparing version numbers. Workers write results directly to shared state via atomic pointer swap.

## Key Design Principles

1. **Lazy evaluation** - Only compute meshes for chunks that are actually visible
2. **Self-healing** - Version comparison catches any missed updates or races
3. **Simple versioning** - Single atomic counter per SubChunk incremented on any block change
4. **FIFO ordering** - Render loop iterates near-to-far, so FIFO preserves approximate priority
5. **No result queue** - Workers atomically swap new mesh into shared state
6. **Deduplication** - Primary: check before push. Secondary: worker discards if already up-to-date

## Architecture Changes

### Phase 1: Add Version Tracking to SubChunk

**File: `include/finevox/subchunk.hpp`**
- Add `std::atomic<uint64_t> blockVersion_{1}` member (start at 1, 0 means "no mesh yet")
- Add `uint64_t blockVersion() const` accessor
- Remove `meshDirty_` boolean (replaced by version comparison)
- Remove `isMeshDirty()`, `markMeshDirty()`, `clearMeshDirty()` methods

**File: `src/subchunk.cpp`**
- In `setBlock()`: increment `blockVersion_` instead of setting `meshDirty_`

### Phase 2: Simplify MeshRebuildQueue to FIFO with Dedup

**File: `include/finevox/mesh_rebuild_queue.hpp`**
- Remove `MeshRebuildRequest` struct entirely - just queue `ChunkPos`
- Remove `PriorityWeights`, `dirtyTime`, `inFrustum`, all priority logic
- Replace with simple FIFO:
  - `std::unordered_set<uint64_t> inQueue_` for O(1) dedup check
  - `std::deque<ChunkPos> queue_` for FIFO order
- Keep: `push()`, `popWait()`, `shutdown()`, `contains()`, `empty()`, `size()`, `clear()`
- Remove: `updateCamera()`, `calculatePriority()`, `recalculatePriorities()`, `popBatch()`, `weights()`

**File: `src/mesh_rebuild_queue.cpp`**
- `push()`: Check `inQueue_`, if not present add to both set and deque, notify
- `popWait()`: Wait on CV, pop front of deque, remove from set
- Simple ~50 lines total

### Phase 3: Create ChunkMeshState for Atomic Mesh Storage

**New file: `include/finevox/chunk_mesh_state.hpp`**
```cpp
struct ChunkMesh {
    MeshData data;
    uint64_t builtForVersion;  // Which block version this was built from
};

// Per-chunk mesh state, atomically updated by workers
class ChunkMeshState {
    std::atomic<std::shared_ptr<ChunkMesh>> mesh_;
public:
    std::shared_ptr<ChunkMesh> get() const;
    void set(std::shared_ptr<ChunkMesh> newMesh);
    uint64_t version() const;  // Returns 0 if no mesh
};
```

### Phase 4: Simplify MeshWorkerPool - Direct State Update

**File: `include/finevox/mesh_worker_pool.hpp`**
- Remove result queue (`results_`, `resultMutex_`)
- Remove `popResult()`, `popResultBatch()`, `resultQueueSize()`
- Add: callback or state map for storing results
- Worker writes directly to `ChunkMeshState` via atomic swap

**File: `src/mesh_worker_pool.cpp`**
- `buildMesh()`:
  1. Check if `subchunk.blockVersion() == meshState.version()` → already up-to-date, skip
  2. Build mesh, capture blockVersion
  3. Atomic swap into ChunkMeshState
  4. Pop request from queue (dedup entry removed)

### Phase 5: Remove Push Notifications from World

**File: `include/finevox/world.hpp`**
- Remove `MeshDirtyCallback` typedef
- Remove `setMeshDirtyCallback()` method
- Remove `meshDirtyCallback_` member
- Remove `notifyMeshDirty()` method
- Keep `getAffectedSubChunks()` - useful for other purposes

**File: `src/world.cpp`**
- Remove `notifyMeshDirty()` call from `setBlock()`

### Phase 6: Delete GPUUploadQueue

- Remove `include/finevox/gpu_upload_queue.hpp`
- Remove `src/gpu_upload_queue.cpp`
- Remove from CMakeLists.txt
- Remove tests

GPU upload throttling happens naturally in the render loop by limiting how many meshes are checked/uploaded per frame.

### Phase 7: Update Render Loop Pattern

The render loop becomes:

```cpp
void WorldRenderer::update(const Camera& camera) {
    // 1. Iterate visible chunks near-to-far
    for (const auto& pos : getVisibleChunkPositions(camera)) {
        const SubChunk* subchunk = world_.getSubChunk(pos);
        if (!subchunk || subchunk->isEmpty()) continue;

        // 2. Get mesh state for this chunk
        ChunkMeshState& meshState = getMeshState(pos);
        auto mesh = meshState.get();

        // 3. Check if stale
        uint64_t blockVer = subchunk->blockVersion();
        uint64_t meshVer = mesh ? mesh->builtForVersion : 0;

        if (meshVer < blockVer) {
            // Queue rebuild (dedup handles if already queued)
            meshQueue_.push(pos);
        }

        // 4. Render with whatever mesh we have (may be stale, fine for one frame)
        if (mesh) {
            render(mesh->data);
        }
    }

    // Workers are continuously processing queue and updating ChunkMeshState
    // Next frame we'll see updated meshes automatically
}
```

## Data Flow Summary

```
Block Change:
  World::setBlock() → SubChunk::setBlock() → blockVersion_++

Render Loop (each frame, near-to-far):
  For each visible chunk:
    Compare subchunk.blockVersion() vs meshState.version()
    If stale → push to FIFO queue (dedup)
    Render using current mesh (might be stale)

Worker Thread (continuous):
  Pop ChunkPos from FIFO queue
  Check if already up-to-date → discard
  Build mesh, capture blockVersion
  Atomic swap into ChunkMeshState
```

## Benefits

1. **Simpler architecture** - No result queue, no GPU upload queue
2. **FIFO is enough** - Render order provides priority
3. **Self-healing** - Version comparison handles races
4. **Lock-free reads** - Render loop reads atomic shared_ptr
5. **Automatic dedup** - Queue dedup + version check = no wasted work

## Files to Delete

- `include/finevox/gpu_upload_queue.hpp`
- `src/gpu_upload_queue.cpp`
- `tests/test_gpu_upload_queue.cpp`

## Test Updates Required

- `test_subchunk.cpp`: Update for version API instead of meshDirty
- `test_mesh_rebuild_queue.cpp`: Rewrite for simple FIFO queue
- `test_mesh_worker_pool.cpp`: Update for direct state update model
- `test_world.cpp`: Remove mesh dirty callback tests

## Implementation Order

1. Add blockVersion to SubChunk (keep meshDirty temporarily)
2. Simplify MeshRebuildQueue to FIFO with dedup
3. Create ChunkMeshState for atomic mesh storage
4. Update MeshWorkerPool to write directly to state
5. Remove meshDirty and World callbacks
6. Delete GPUUploadQueue
7. Update all tests
