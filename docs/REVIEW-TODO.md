# Comprehensive Design Review TODO

Generated: 2026-01-18

This document compares the current implementation against design documents and identifies:
1. Deviations between design docs and code
2. Redundancies with FineStructureVK
3. Features that should move to FineStructureVK
4. Documentation that needs updating

---

## Summary

| Category | Items |
|----------|-------|
| Doc Updates Needed (code is better) | 7 |
| Code Updates Needed (doc is better) | 3 |
| FineVK Redundancies Fixed | 1 |
| FineVK Candidates Implemented | 1 ✅ (high-precision camera) |
| FineVK Integration Doc Fixed | 1 ✅ |
| Phase Progress Updates | 4 |

---

## 1. Documentation vs Implementation Deviations

### 1.1 Doc Needs Update: Phase 4 Rendering Tests (17-implementation-phases.md)

**Location:** `docs/17-implementation-phases.md` lines 246-248

**Issue:** Phase 4.4 testing checklist shows these as incomplete:
```
- [ ] Render a static manually-placed world
- [ ] View-relative precision at large coordinates
- [ ] Frustum culling excludes off-screen chunks (use debug camera offset to verify)
```

**Reality:** All three are now working:
- `render_demo.cpp` renders a manually-placed world
- Large coordinate support with double-precision camera implemented
- Debug camera offset mode implemented and tested

**Action:** Mark these tests as complete in doc
- [ ] Update 17-implementation-phases.md Phase 4.4 testing checklist

---

### 1.2 Doc Needs Update: AI-NOTES.md Current Work State

**Location:** `docs/AI-NOTES.md` lines 196-226

**Issue:** Current work state says:
```
**Next task:** Phase 3.3 (Raycasting) and 3.4 (Entity Physics)
```

**Reality:** Phase 3 is complete, Phase 4 is nearly complete with working rendering.

**Action:** Update current work state section
- [ ] Update AI-NOTES.md "Current Work State" section to reflect Phase 4 progress

---

### 1.3 Doc Needs Update: Block System Design (04-core-data-structures.md)

**Location:** `docs/04-core-data-structures.md` Section 4.5

**Issue:** Design doc shows `Block` as an ephemeral struct with `shared_ptr<SubChunk>`:
```cpp
struct Block {
    std::shared_ptr<SubChunk> subchunk;
    BlockPos pos;
    uint16_t localIndex;
    // ...
};
```

**Reality:** Current implementation doesn't use `Block` wrapper class. Instead:
- Direct `SubChunk::getBlock(x,y,z)` returns `BlockTypeId`
- `World::getBlock()` returns `BlockTypeId`
- No ephemeral `Block` descriptor with `shared_ptr`

**Analysis:** The simplified implementation is better for now. The `Block` wrapper adds overhead and complexity that isn't needed until we implement features like displacement, custom data, etc.

Dev note: If the Block wrapper exists on the stack of the caller, the overhead will be low. This provides a container with everything in one place. To keep it low overhead, we don't copy extra data but point to it (regular pointer or smart pointer). But we can defer.

**Action:** Add note to doc explaining simplified current implementation
- [ ] Add "Implementation Note" to 04-core-data-structures.md Section 4.5 explaining current simplified approach

---

### 1.4 Doc Needs Update: BlockType Virtual Methods (04-core-data-structures.md)

**Location:** `docs/04-core-data-structures.md` Section 4.5

**Issue:** Design doc shows `BlockType` as an abstract base class with virtual methods:
```cpp
class BlockType {
public:
    virtual ~BlockType() = default;
    virtual std::string_view name() const = 0;
    virtual bool isSolid() const { return true; }
    virtual const Mesh* getDefaultMesh() const = 0;
    // etc.
};
```

**Reality:** Current `BlockType` in `block_type.hpp` is a data-only class:
```cpp
class BlockType {
public:
    BlockType& setCollisionShape(const CollisionShape& shape);
    BlockType& setOpaque(bool opaque);
    // etc. - builder pattern, no virtual methods
};
```

**Analysis:** The current data-driven approach is simpler and sufficient. Virtual methods would be needed for custom behavior (events, custom meshes), which aren't implemented yet.

Dev note: Agreed, but we will need this eventually when blocks have different kinds of functionality, receive events, can be scripted, etc.

**Action:** Update doc to reflect data-driven approach, note virtual methods for future
- [ ] Update 04-core-data-structures.md Section 4.5 to describe current BlockType design

---

### 1.5 Doc Needs Update: Rendering Architecture (06-rendering.md)

**Location:** `docs/06-rendering.md` Section 6.1

**Issue:** Design doc shows this architecture:
```
WorldRenderer
    +-- ChunkRenderer (one per loaded chunk)
    +-- EntityRenderer
    +-- SkyRenderer
    +-- UIRenderer
```

**Reality:** Current implementation has:
```
WorldRenderer (renders all subchunks)
    +-- SubChunkView (one per loaded subchunk)
```

No `ChunkRenderer`, `EntityRenderer`, `SkyRenderer`, or `UIRenderer` classes exist.

**Analysis:** Simpler flat architecture is appropriate for current phase. Full architecture from doc is future work.

**Action:** Add "Current Implementation" note to doc
- [ ] Add implementation note to 06-rendering.md Section 6.1

---

### 1.6 Doc Needs Update: ChunkView vs SubChunkView (06-rendering.md)

**Location:** `docs/06-rendering.md` Section 6.4

**Issue:** Doc describes `ChunkView` with double-buffering and `MeshBuffer`:
```cpp
class ChunkView {
    MeshBuffer frontBuffer_;  // Used for rendering
    MeshBuffer backBuffer_;   // Used for rebuilding
    std::atomic<bool> rebuilding_{false};
};
```

**Reality:** Current `SubChunkView` is simpler - single buffer, no double-buffering:
```cpp
class SubChunkView {
    std::unique_ptr<finevk::RawMesh> mesh_;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    bool dirty_ = true;
};
```

**Analysis:** Double-buffering is Phase 5 (async mesh updates). Current implementation is correct for Phase 4.

**Action:** Add note that double-buffering is Phase 5
- [ ] Add "Phase 5" note to 06-rendering.md Section 6.4

---

### 1.7 Code Needs Update: Greedy Meshing Not Implemented

**Location:** `src/mesh.cpp`

**Issue:** Doc (06-rendering.md Section 6.2) describes greedy meshing:
> **Greedy Meshing** combines adjacent coplanar faces with the same texture to reduce vertex count

**Reality:** Current `MeshBuilder` uses simple face culling, not greedy meshing. Each visible face is a separate quad.

**Analysis:** This is correct - greedy meshing is Phase 5, current code is Phase 4.

**Design Note:** Greedy meshing should be confined to subchunk boundaries. This keeps frustum culling simple - each subchunk is an independent unit with its own mesh, no cross-boundary considerations needed.

**Action:** No code change needed, but verify Phase 5 scope
- [ ] Verify greedy meshing is clearly documented as Phase 5 in 17-implementation-phases.md ✓ (already correct)
- [ ] Add note to Phase 5 doc that greedy meshing is subchunk-local

---

### 1.8 Code Needs Update: View-Relative in Shader Comments

**Location:** `docs/06-rendering.md` Section 6.6

**Issue:** Doc shows shader using `viewProj` matrix:
```glsl
gl_Position = viewProj * vec4(worldPos, 1.0);
```

**Reality:** Current shader uses separate `projection * view`:
```glsl
gl_Position = camera.projection * camera.view * vec4(viewRelativePos, 1.0);
```

**Analysis:** The current implementation is better - it supports the view-relative rendering with camera at origin.

**Action:** Update doc shaders to match actual implementation
- [ ] Update 06-rendering.md Section 6.6 shader examples

---

### 1.9 Doc Needs Update: Mesh Update Pipeline Not Implemented (06-rendering.md)

**Location:** `docs/06-rendering.md` Section 6.5

**Issue:** Doc describes a 4-stage mesh update pipeline with priority queues and worker threads.

**Reality:** Current implementation is synchronous:
- `WorldRenderer::updateMeshes()` rebuilds on main thread
- Simple dirty list, no priority queue
- No worker threads

**Analysis:** This is Phase 5 scope, current code is Phase 4.

**Action:** Add note to doc that full pipeline is Phase 5
- [ ] Add "Phase 5" implementation note to 06-rendering.md Section 6.5

---

### 1.10 Code Needs Update: Missing BlockDisplacement Implementation

**Location:** `docs/04-core-data-structures.md` Section 4.5

**Issue:** Doc describes `BlockDisplacement` struct and face elision rules for displaced blocks.

**Reality:** `BlockDisplacement` is documented but not implemented in `SubChunk`:
- No `getDisplacement()` / `setDisplacement()` methods
- No `displacements_` sparse map
- MeshBuilder doesn't check displacement matching

**Analysis:** This is a future feature, not critical path.

**Action:** Either implement or mark as future in doc
- [ ] Add "Not Yet Implemented" note to BlockDisplacement section in 04-core-data-structures.md

---

## 2. FineStructureVK Redundancies

### 2.1 FIXED: Direct Vulkan Calls Replaced

**Status:** Already fixed in this session

**What was fixed:**
- Replaced `vkCmdSetViewport` / `vkCmdSetScissor` with `cmd.setViewportAndScissor()`
- Verified no other direct Vulkan function calls remain in FineVox code

---

## 3. Candidates for FineStructureVK

### 3.1 IMPLEMENTED: High-Precision Camera Support

**Status:** ✅ Implemented in FineVK and integrated in FineVox

**FineVK Changes (from FINEVOX_RESPONSE.md):**
- Added `Camera::moveTo(glm::dvec3)` overload - automatically uses double-precision mode
- Added `Camera::move(glm::dvec3)` overload
- Added `Camera::positionD()` - returns double-precision position
- Added `Camera::hasHighPrecisionPosition()` - check if using doubles
- Added `CameraState::viewRelative` - view matrix with camera at origin (rotation only)

**FineVox Changes:**
- `WorldRenderer::updateCamera()` now uses `cameraState.viewRelative` from FineVK
- `render_demo.cpp` uses `camera.moveTo(dvec3)` and `camera.positionD()` directly
- Removed custom `CameraController` class, replaced with simple `CameraInput` for key states

**Usage Pattern:**
```cpp
finevk::Camera camera;
camera.moveTo(glm::dvec3(1000000.0, 64.0, 1000000.0));  // Auto-switches to double mode
camera.setOrientation(forward, up);
camera.updateState();

// For rendering
worldRenderer.updateCamera(camera.state(), camera.positionD());
```

---

### 3.2 Consider: Push Constants Helper

**Location:** `src/world_renderer.cpp`

**Issue:** Using push constants requires knowing the struct size:
```cpp
pipelineLayout_->pushConstants(cmd.handle(), VK_SHADER_STAGE_VERTEX_BIT, pushConstants);
```

**Consideration:** FineVK's `PipelineLayout::pushConstants` is already templated and handles size.

**Status:** No action needed - FineVK already provides good abstraction.

---

## 4. Phase Progress Updates

### 4.1 Update Phase 3 Status

**Location:** `docs/17-implementation-phases.md`

**Issue:** Phase 3 shows some items as in-progress but they're complete.

**Action:**
- [ ] Mark Phase 3 as fully complete in 17-implementation-phases.md

---

### 4.2 Update Phase 4 Status

**Location:** `docs/17-implementation-phases.md`

**Issue:** Phase 4 checklist needs updating:
- [x] ChunkVertex, MeshData, MeshBuilder - done
- [x] SubChunkView - done
- [x] WorldRenderer with view-relative - done
- [x] Frustum culling - done
- [x] BlockAtlas - done
- [x] Basic shaders - done
- [x] Debug features - done (debug camera offset)
- [x] Large coordinate support - done (not originally in Phase 4)

**Action:**
- [ ] Update Phase 4 checklist in 17-implementation-phases.md
- [ ] Add large coordinate support as completed item

---

### 4.3 Update Phase 4 Testing Status

**Location:** `docs/17-implementation-phases.md`

**Action:**
- [ ] Mark render demo tests as complete
- [ ] Mark view-relative precision test as complete
- [ ] Mark frustum culling test as complete

---

### 4.4 Clarify Phase 5 Scope

**Location:** `docs/17-implementation-phases.md`

**Issue:** Phase 5 scope should be clearly delineated from Phase 4:
- Greedy meshing (Phase 5)
- Mesh worker threads (Phase 5)
- Priority queue (Phase 5)
- Double-buffering (Phase 5)

**Action:**
- [ ] Review and confirm Phase 5 scope is clear

---

## 5. Minor Documentation Fixes

### 5.1 FIXED: FineStructureVK Integration Doc (15-finestructurevk-integration.md)

**Status:** ✅ Updated with correct FineVK APIs

**Changes made:**
- Replaced incorrect API examples with verified FineVK patterns
- Added correct Material builder pattern (`.uniform<T>()`, `.texture()`)
- Added correct GraphicsPipeline builder pattern (`.cullBack()`, path-based shader loading)
- Added Camera double-precision usage pattern with `positionD()` and `viewRelative`
- Updated frame rendering section to show view-relative implementation

---

### 5.2 Update INDEX.md Last Updated Date

**Action:**
- [ ] Update "Last Updated" date in INDEX.md

---

## Action Items Summary

### High Priority (affects correctness/clarity)
1. [ ] Update 17-implementation-phases.md Phase 4 status and testing
2. [ ] Update AI-NOTES.md current work state
3. [ ] Add "Not Yet Implemented" note for BlockDisplacement

### Medium Priority (documentation accuracy)
4. [ ] Update 06-rendering.md Section 6.6 shader examples
5. [ ] Add implementation notes to 04-core-data-structures.md for Block, BlockType
6. [ ] Add "Phase 5" notes to 06-rendering.md Sections 6.4, 6.5
7. [ ] Add implementation note to 06-rendering.md Section 6.1

### Low Priority (polish)
8. [ ] Update INDEX.md last updated date
9. [ ] Verify FineVK API examples in 15-finestructurevk-integration.md
10. [ ] Document large world coordinates pattern (future FineVK consideration)

---

## Notes for Future Sessions

When implementing new features, check this review for:
- BlockDisplacement - documented but not implemented
- Greedy meshing - Phase 5
- Mesh worker threads - Phase 5
- Double-buffering - Phase 5
- EntityRenderer, SkyRenderer, UIRenderer - future

The current codebase is intentionally simpler than the full design docs. The design docs describe the target architecture; the implementation is building toward it incrementally.
