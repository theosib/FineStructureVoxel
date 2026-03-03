# System: Rendering

**Library:** `finevox_render` (`finevox::render::`)
**Headers:** `include/finevox/render/`
**Old docs:** [old_docs/06-rendering.md](../../old_docs/06-rendering.md), [old_docs/07-lod.md](../../old_docs/07-lod.md), [old_docs/22-phase6-lod-design.md](../../old_docs/22-phase6-lod-design.md)

---

## Overview

`WorldRenderer` draws all visible subchunks via `SubChunkView` GPU mesh handles. Push-based mesh rebuilds: block/light changes push requests to `MeshRebuildQueue`, `MeshWorkerPool` generates CPU-side meshes, graphics thread uploads to GPU. LOD system (0-4) selects mesh resolution by distance with hysteresis. Fluid meshes are rendered in a separate translucent pass.

---

## Key Types

| Type | Description |
|------|-------------|
| `WorldRenderer` | Top-level renderer; frustum culling, SubChunkView lifecycle, fluid pass |
| `SubChunkView` | Per-subchunk GPU mesh handle; owns opaque + fluid `RawMeshPtr` |
| `MeshBuilder` | CPU-side face culling, greedy meshing, AO, custom geometry |
| `MeshWorkerPool` | Thread pool for parallel CPU mesh generation |
| `MeshRebuildQueue` | `AlarmQueueWithData<SubChunkPos, MeshRebuildRequest>` — deduplicates by key |
| `MeshCacheEntry` | Per-subchunk: pending mesh, uploaded mesh, version/LOD tracking |
| `BlockAtlas` | Texture atlas for block face textures |
| `LODSubChunk` | Downsampled block storage for LOD levels 1-4 |
| `FrameFenceWaiter` | Overlaps mesh processing with GPU fence wait (dedicated thread) |
| `FluidMeshBuilder` | CPU-side fluid mesh; reuses ChunkVertex; stored as `fluidMesh_` in SubChunkView |

---

## WorldRenderer

```cpp
#include <finevox/render/world_renderer.hpp>

WorldRenderer renderer(device, world, blockAtlas);
renderer.setViewDistance(8);      // chunks in each direction
renderer.setLODEnabled(true);

// Per-frame
renderer.updateCamera(cameraPos, viewMatrix, projMatrix);  // double-precision pos
renderer.performCleanup();        // unload distant + enforce GPU memory budget
renderer.renderOpaque(frame);
renderer.renderTranslucent(frame); // fluid pass (back-to-front sorted)

// Stats
size_t gpuBytes = renderer.gpuMemoryUsed();
int rendered = renderer.renderedChunkCount();
int culled = renderer.culledChunkCount();
```

---

## ChunkVertex

```cpp
struct ChunkVertex {
    glm::vec3 position;   // view-relative (subtract camera pos before upload)
    glm::vec3 normal;
    glm::vec2 texcoord;
    float ao;             // ambient occlusion (0.2-1.0)
    float skyLight;       // 0.0-1.0
    float blockLight;     // 0.0-1.0
    glm::vec4 tileBounds; // for custom/fluid: used as tint color
};
```

**View-relative rendering**: all vertex positions are `worldPos - cameraPos` (double-precision subtraction, then stored as float). Eliminates float precision jitter at large world coordinates.

---

## MeshBuilder

```cpp
#include <finevox/render/mesh_builder.hpp>

MeshBuilder builder;
builder.setSmoothLighting(true);
builder.setAOEnabled(true);

// Build a subchunk mesh (called from worker thread)
builder.build(subchunk, neighborSubchunks, blockRegistry, lightProvider);
auto mesh = builder.takeMesh();  // SubChunkMeshData (opaque + transparent split)

// LOD mesh
builder.buildLODMesh(lodSubchunk, lodLevel, lightProvider);
```

**Greedy meshing** merges coplanar faces with identical: type, AO values, light values, texture. Confined to subchunk boundaries (simplifies frustum culling — each SubChunk = independent mesh unit).

**Custom mesh exclusion**: blocks with `BlockType::hasCustomMesh() == true` skip greedy merging and render individually via `addCustomFace()`.

---

## Push-Based Mesh Pipeline

```
Block/light change
    → MeshRebuildQueue.push(subchunkPos, request)  [deduplicated]
    → MeshWorkerPool picks up request
    → MeshBuilder generates CPU mesh
    → MeshCacheEntry.setPending(mesh, version)
    → Graphics thread: getMesh() sees pending version
    → Upload to GPU → markUploaded()
```

```cpp
// Stale detection
MeshCacheEntry& entry = cache[subchunkPos];
if (entry.blockVersion != sc.blockVersion() || entry.lightVersion != sc.lightVersion()
    || entry.lodLevel != requestedLOD) {
    // rebuild needed
    rebuildQueue.push(subchunkPos, request);
}

// After GPU upload
entry.markUploaded();
```

---

## LOD System

LOD levels 0-4: 1×, 2×, 4×, 8×, 16× block grouping (sampling most common solid block):

```cpp
// LODConfig — configurable distance thresholds
LODConfig config;
config.setDistance(LODLevel::LOD0, 0, 128);    // within 128 blocks: full detail
config.setDistance(LODLevel::LOD1, 128, 256);  // 128-256: 2× downsampled
config.setDistance(LODLevel::LOD2, 256, 512);  // etc.

// LODRequest — encodes target LOD + hysteresis
LODRequest request = config.getRequestForDistance(distance);
bool accept = request.accepts(currentLOD);  // hysteresis: don't switch at exact boundary

// Debug controls
renderer.setLODBias(+1);      // push all LODs one level lower detail
renderer.setForceLOD(2);      // force all chunks to LOD2
renderer.setLODDebugMode(LODDebugMode::ColorByLOD);
```

Non-cube blocks render as full cubes at LOD > 0 (acceptable — transition distances far enough).

---

## GPU Memory Management

```cpp
// WorldRendererConfig
config.gpuMemoryBudget = 512 * 1024 * 1024;  // 512 MB
config.unloadDistanceMultiplier = 1.2f;        // unload at 1.2× render distance
config.maxUnloadsPerFrame = 16;                // prevent GPU stalls

renderer.gpuMemoryUsed();                      // current usage
renderer.unloadDistantChunks(cameraPos);       // hysteresis-based
renderer.enforceMemoryBudget();                // unload furthest out-of-view when over budget
renderer.performCleanup();                     // both of the above
```

---

## Fog System

```cpp
// FogConfig (in WorldRendererConfig)
config.fog.enabled = true;
config.fog.start = 200.0f;    // blocks from camera
config.fog.end = 256.0f;      // full fog at this distance
config.fog.color = glm::vec4(0.6f, 0.7f, 0.8f, 1.0f);
config.fog.dynamicColor = true;  // blend with sky color

// Runtime
renderer.setFogEnabled(true);
renderer.setFogDistances(start, end);
renderer.setFogColor(color);
float factor = renderer.getFogFactor(distance);  // 0.0-1.0 linear blend

// Underwater fog override (from updateCamera)
if (cameraInFluid) {
    renderer.setFogColor(fluidType.fogColor);
    renderer.setFogDensity(fluidType.fogDensity);
}
```

---

## Fluid Rendering

Fluids use a separate render pass (back-to-front sorted, depth-write OFF):

```cpp
// FluidMeshBuilder generates separate mesh per subchunk
FluidMeshBuilder fluidBuilder;
fluidBuilder.build(subchunk, neighbors, fluidRegistry, lightProvider);
auto fluidMesh = fluidBuilder.takeMesh();

// SubChunkView holds both
view.setOpaqueMesh(opaqueMesh);
view.setFluidMesh(fluidMesh);   // RawMeshPtr

// MeshUploadData carries both
struct MeshUploadData {
    RawMeshPtr mesh;        // opaque
    RawMeshPtr fluidMesh;   // translucent
};
```

**fluid.frag shader**: tint-based (uses `tileBounds` as tint color, no atlas sampling). Pipeline: `alphaBlending()` + `depthWrite(false)` + `cullNone()`. Shares `pipelineLayout_` with opaque pipeline.

---

## FrameFenceWaiter

Overlaps mesh processing with GPU fence wait to reduce frame idle time:

```cpp
// 3-phase render loop:
// Phase 1: fence wait (GPU) + mesh processing (CPU) overlap
// Phase 2: input/world updates + deadline meshes
// Phase 3: render

FrameFenceWaiter fenceWaiter(meshWorkerPool);
fenceWaiter.start();
// ... in render loop:
fenceWaiter.waitForFenceAndMeshes(deadline);
fenceWaiter.stop();  // 2-phase shutdown: requestStop() then join()
```

---

## Gotchas

- `frame.extent` is a field (not function), `frame.frameIndex()` is a function
- `renderer->endFrame()` on renderer; `frame.beginRenderPass()` / `frame.endRenderPass()` on frame
- `RenderSurface::deferDelete()` for GPU-safe resource cleanup (not immediate delete)
- LOD meshes don't calculate AO (default 1.0f)
- Frustum culling uses double-precision AABB for view-relative math
- `BlockAtlas` must be initialized with all block textures before first `buildMesh()` call
- `MeshWorkerPool` reads SubChunk with lock-free access (palette indices are stable — no writes during mesh gen)
