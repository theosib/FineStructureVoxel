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
| `MeshRebuildQueue` | `KeyedQueue<ChunkPos, MeshRebuildRequest>`: deduplicates by key; pops what's ahead of the eye first (`meshViewPriority`: distance, stretched up to 4x for what's behind, except within 24 blocks; requests under priority 100 share one band). `WorldRenderer` moves the focus and calls `reprioritize()` when the view moves 4 blocks or turns 10 degrees, at most every 100 ms |
| `MeshCacheEntry` | Per-subchunk: pending mesh, uploaded mesh, version/LOD tracking |
| `BlockAtlas` | Texture atlas for block face textures; tiles 32 to 128 pixels, as the pictures are (texture-packs.md) |
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
    uint32_t tint;        // RGBA8, R in the low byte, multiplies the texture; white if unused
    glm::uvec4 blend;     // neighbours' textures spread over the face, two layers; zero if none
};
```

**Tint**: `BlockTintProvider` (`setTintProvider` on `WorldRenderer`, `MeshWorkerPool` and
`MeshBuilder`) gives a face its colour; `ContentPackLoader::tintProvider` blends biome colours from
the subchunks' biome ids. The shader brings it to linear before multiplying, so a colour multiplies
as it looks. Greedy meshing only merges faces whose tints match.

**Blending**: where a cube's face meets a neighbour's face in the same plane, the block with the
higher `blend:` priority (`BlockBlend`, a `.model` key) spreads its texture up to its width over the
other's, with a ragged border. Nothing is drawn over anything: each face is drawn once, and
`chunk.frag` mixes a neighbour's tile into it near the shared edge, so there is no z-fighting to
manage. `MeshBuilder::faceBlend` decides what spreads onto each visible face:

- only a strictly higher priority spreads, so two of a kind, or two of equal priority, meet as before;
- the neighbour must be a plain cube showing a face the same way, at the same level. A step up or
  down, or a neighbour covered over, is a hard edge, which falls on a real edge of the geometry;
- a diagonal neighbour counts only if one of the two cells beside both is open, or grass outside a
  wall's corner would show in the corner inside it;
- the two highest priorities are kept, the higher drawn on top.

The vertex carries, per layer, the tile's top-left UV, which of the 8 neighbours spread (`BLEND_*`,
in the texture's own directions, so the shader needs no face table), the width, and the tint as
RGB565. The shader takes the distance from each fragment to the nearest of those neighbours'
cells: across to an edge, straight to a corner. Both sides of a border work out the same distance,
and the ragged edge is value noise in world space with some of the texture's own shading, so the
spread runs on unbroken across faces and subchunks. Faces blended alike still merge, as the shader
works per cell (`cellCoord`), and a corner beside an edge of the same layer is dropped as
redundant so a straight border's row merges. The cell coordinate is read `centroid`: under MSAA an
edge pixel is shaded at its centre, past the face, where a coordinate just over 1 wraps to 0 and
drew a dotted line of the neighbour's texture along the edge.

It's off unless a client sets a provider (`WorldRenderer::setBlendProvider`). `GameClient` sets one
where the game has `blending: true` in `game.conf`, or the texture pack in use says so, from the
blocks' own `blend:` with the pack's over them (texture-packs.md). A pack's `blending: false` turns
it off. On smoothed ground a step the surface runs over (a `Link`) isn't a hard edge: the face
across it, a block up or down, is the neighbour.
Meshing a face costs 8 more block lookups, about what AO costs. Every vertex is 16 bytes bigger
whether it's on or not. LOD meshes and fluids don't blend.

**Smooth ground** (`ground_smoothing.hpp`): a cube with `smooth: true` in its `.model` has its open
top and underside drawn as a curved surface over the steps around it, in a game with `solid_slopes`
(physics.md, "Solid slopes"), whose bodies stand on the tops as drawn. Without it the ground is
drawn as the cubes it collides as.

- **The shape.** Each face is a quadratic B-spline over the column heights, held as a 3x3 Bezier
  patch (`GroundPatch`): the middle is the face's own height, each edge the mean of it and the
  neighbour across, each corner the mean of the four columns there. A staircase comes out a
  straight 45 degree slope through the middle of every tread, one step an S-curve two blocks long
  at 45 degrees at the riser, and a single face's patch is a parabola, drawn in 3 pieces each way
  it curves (`GROUND_CURVED_STEPS`) and 1 where it's straight.
- **What's across an edge** (`GroundEdge`): a `Link` is another smooth face a block up, level or
  down, with room over it. `Up` and `Down` are steps of two or more: the face goes half a block
  toward them, so both sides round off and a block of wall stays between. `Pin` is anything else
  (built blocks): the edge stays level, with a wall wherever the corners dip below or rise above it.
  Where they dip, the wall is the built block's own side showing down to the ground, drawn with its
  texture (`setGroundSmoothing`'s block lookup), not the ground's.
  Plants count as open, and are raised to the surface under them. So does water: a bank runs down
  into it, and the fluid mesh draws the water's surface over the bank where it dips below it
  (fluids.md, "Banks").
- **Faces meet exactly.** A corner is worked out by the columns' link group, which is the same from
  every face in it (`cornerHeight`), so linked faces share their edges bit for bit. Any difference
  left (thin layers, which keep their top and bottom on their own halves) is closed by a strip on
  the higher side. Side faces of ground blocks are cut to run between the surfaces around them,
  including the surface over them, which can dip a block and a half where steps wind round a corner.
  `GroundSmoothing.MeshHasNoHolesOnRandomGround` checks with rays from open air on random hills,
  overhangs, posts and ponds: whatever they hit first faces them (`SMOOTH_SEEDS=1-1000` runs more).
- **What a face reads** reaches 3 cells across and 6 up or down (`GROUND_REACH_*`). The mesher keeps
  that much around a subchunk in a lazily filled grid, and `ClientWorld::setMeshReach` rebuilds the
  meshes that far from changed cells, not only the six face neighbours.
- **Bodies** are drawn on it: `EntityRenderState::followGround` lifts a body on the ground, or coming
  down a step, to `groundSurfaceY` under its middle, which looks two cells down so one that has just
  stepped up onto a block's edge stays on the slope. In the air it eases back to where it is.

`WorldRenderer::setSmoothProvider` turns it on, and `GameClient` does that where the game has
`solid_slopes`. On a hilly Keystone view it costs about a fifth more triangles.
LOD meshes don't smooth.

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

## Visual Capture & Golden-Image Tests

Two companion components in `finevox_render` provide headless GPU → PNG
capture and byte-tolerant golden comparison.

| Component | Header | Purpose |
|-----------|--------|---------|
| `FrameCapture` | `finevox/render/frame_capture.hpp` | Readback from an `OffscreenSurface` + PNG encode via vendored `stb_image_write` |
| `VisualTestHarness` | `finevox/render/visual_test_harness.hpp` | Load golden PNG, per-channel byte compare with tolerance, regeneration escape hatch |

```cpp
// Headless capture path (tests / future F12 hotkey)
auto surface = finevk::OffscreenSurface::create(device)
    .extent(256, 256).colorFormat(VK_FORMAT_R8G8B8A8_UNORM)
    .enableDepth().build();
auto pool = finevk::StagingPool::create(device).build();

surface->beginFrame();
surface->beginRenderPass({0.02f, 0.02f, 0.05f, 1.0f});
particleRenderer.render(*surface->currentCommandBuffer());
surface->endRenderPass();
surface->endFrame();

// Save full image:
FrameCapture::captureSurface(*surface, *pool, "out.png");

// Or a small ROI (faster readback, smaller golden):
FrameCapture::CaptureRect roi{80, 80, 96, 96};
std::vector<uint8_t> rgba;
uint32_t w, h;
FrameCapture::readbackRGBA(*surface, *pool, roi, rgba, w, h);
```

**Golden-image tests** render a deterministic scene into an
`OffscreenSurface`, read an ROI back, and compare against a committed PNG:

```cpp
auto r = VisualTestHarness::compareAgainstGolden(
    rgba, width, height, "tests/golden/sparks_basic.png",
    /*maxAbsDiffPerChannel=*/4);
EXPECT_TRUE(r.passed) << r.failureReason;
```

Currently wired: `tests/test_visual_sparks.cpp` — billboard-mode matrix
covering all three `BillboardMode` branches.  All three scenes share: fixed
RNG seed, 3 ticks at 30 TPS, 96×96 ROI, tolerance 4.  Scene variations:
`GoldenBasic` (VelocityStretched, on-axis camera), `GoldenCameraFacing`
(CameraFacing, off-axis pose), `GoldenAxisLocked` (AxisLocked, same off-axis
pose so the two goldens differ because of billboard math, not camera pose).
`DeterministicAcrossRuns` re-runs the basic scene twice in one process and
byte-compares — catches state leaks independent of whether the golden matches.

**Regenerating goldens.**  If a golden is missing or pixels legitimately
change, re-run the test with `FINEVOX_UPDATE_GOLDENS=1`:

```sh
FINEVOX_UPDATE_GOLDENS=1 ./finevox_render_tests \
    --gtest_filter='VisualSparks.*'
```

This writes the current output as the new golden and passes.  Commit the
updated PNG.  Failing tests also dump the last actual result to
`tests/golden/<name>.actual.png` (gitignored) for side-by-side inspection.

Goldens hold across platforms: Apple's and Intel's drivers land within 1/255 of each other.  That
depends on everything random in a test giving the same numbers everywhere, so no
`std::uniform_int_distribution`, and no two random draws in the arguments of one call, since GCC
and Clang evaluate those in opposite orders.  See `randomUnitFloat` in `particle_type.hpp`.

**Renderer generality.**  Both `ParticleRenderer` and `WorldRenderer` accept
any `finevk::RenderSurface*` — either a `SimpleRenderer` (on-screen / window)
or an `OffscreenSurface` (headless capture / tests).  `SimpleRenderer` and
`OffscreenSurface` both derive from `RenderSurface`, so window call sites pass
through unchanged.  Each renderer's one former `SimpleRenderer::defaultSampler()`
dependency is now a locally-owned `finevk::Sampler` member: `ParticleRenderer`
builds a linear sampler; `WorldRenderer` builds one that replicates
`defaultSampler()` exactly (linear min/mag, linear mipmaps, repeat addressing,
anisotropy when the device enables it) so block-atlas filtering is byte-for-byte
identical to prior window rendering.

**Scene-level visual tests (phase 2b).**  With `WorldRenderer` on the
`RenderSurface` interface, a full world render can be driven headlessly into an
`OffscreenSurface`.  `tests/test_visual_scene.cpp` constructs a small
deterministic world (a stone platform with a stacked block cluster), fixes the
camera pose, drives the async mesh pipeline to quiescence *synchronously*, then
renders one frame and golden-compares a 96×96 ROI
(`tests/golden/scene_basic_blocks.png`).  This is Tier-2 coverage: block
meshing, greedy merge, AO, and per-face shading through the real GPU pipeline.

*Driving async meshing to quiescence in a test.*  `WorldRenderer` schedules
rebuilds onto `MeshRebuildQueue`; `MeshWorkerPool` threads build them and push
to the upload queue; `updateMeshes()` drains uploads to the GPU.  The test calls
`rebuildAllMeshes()`, then loops `updateMeshes()` (with a 5 ms sleep) until the
input queue is empty, the upload queue is empty, and `loadedMeshCount()` reaches
the world's subchunk count — requiring several consecutive "settled" passes and
bounded by a 10 s timeout so a stall fails loudly.  Determinism comes from using
no light provider (constant baked sky-light, no BFS to settle), disabled fog,
and order-independent per-subchunk mesh content; the golden mints at
`maxAbsDiff = 0` and repeats identically 10/10.  A `DeterministicAcrossRuns`
companion re-renders in a fresh context and byte-compares to guard determinism
independently of the committed golden.

**Live F12 screenshot (Phase 1d).**  `render_demo` binds `key_f12:
screenshot` (see `resources/input/default.bindings`).  The action sets a
deferred flag; the engine's `onPostRender` callback reads the just-presented
swap-chain image via `renderer.swapChain()->image(window.currentImageIndex())`
and forwards it to `FrameCapture::captureImage(Image&, StagingPool&, path,
VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)`.  Output lands at
`screenshots/YYYYMMDD-HHMMSS.png` (gitignored).  `FrameCapture`'s public
API is now `finevk::Image&`-primary; the `OffscreenSurface&` overloads are
thin one-liner forwarders that pass `surface.colorImage()` through.

**Visual test policy.**

Visual tests render real GPU output and byte-compare against committed PNGs.
They are slow, fragile under driver drift, and expensive to maintain, so we
keep the curated set deliberately tiny.  Two CTest labels partition them:

| Label | Purpose | How it grows |
|-------|---------|--------------|
| `visual-curated` | Hand-picked matrix covering distinct render code paths (one per billboard mode, one per renderer, etc.).  Runs on every local build. | Only when a planner-approved phase adds a new branch that is not already covered (e.g., a new billboard mode, a new fluid pass variant). |
| `visual-regression` | Targeted tests pinned to specific bugs we have already fixed.  Empty today.  Reserved for future use. | When a visual bug is fixed and we want a permanent tripwire against its return — one test per incident, named after the bug. |

```sh
ctest -L visual-curated       # small; run on every build
ctest -L visual-regression    # empty today; reserved
```

Token-economy rationale: every golden is a blob the next reviewer has to
diff-inspect on failure.  Prefer deleting a redundant curated test over
adding a marginal one.  If a change would push the curated count above ~10,
stop and reconsider scope.

Current curated set (post-phase-2b, 5 tests): `VisualSparks.GoldenBasic`
(velocity-stretched), `VisualSparks.GoldenCameraFacing`,
`VisualSparks.GoldenAxisLocked`, `VisualSparks.DeterministicAcrossRuns`, and
`VisualScene.GoldenBasicBlocks` (world render path — blocks / meshing / AO).
`VisualScene.DeterministicAcrossRuns` is intentionally *not* curated: it commits
no golden (it compares two in-process renders), so it is not driver-fragile and
rides in the general test bucket.

**Pitfalls.**

- `OffscreenSurface` forward-declares `CommandBuffer`, `Fence`, and
  `Sampler` but stores them as `unique_ptr` members — any TU that
  constructs or destroys one must include the full headers
  (`finevk/device/command.hpp`, `finevk/rendering/sync.hpp`,
  `finevk/device/sampler.hpp`).
- `OffscreenSurface::framesInFlight() == 1` — descriptor pools / uniform
  buffers sized to `framesInFlight()` collapse to single-buffered
  automatically.  No code change needed, but don't hard-code a
  frames-in-flight count when porting.
- `FINEVOX_UPDATE_GOLDENS=1` is a sharp tool: if the scene is accidentally
  broken, rerunning with it codifies the breakage.  Always diff the new
  golden against the old one before committing.

## Gotchas

- `frame.extent` is a field (not function), `frame.frameIndex()` is a function
- `renderer->endFrame()` on renderer; `frame.beginRenderPass()` / `frame.endRenderPass()` on frame
- `RenderSurface::deferDelete()` for GPU-safe resource cleanup (not immediate delete)
- LOD meshes don't calculate AO (default 1.0f)
- Frustum culling uses double-precision AABB for view-relative math
- `BlockAtlas` must be initialized with all block textures before first `buildMesh()` call
- `MeshWorkerPool` reads SubChunk with lock-free access (palette indices are stable — no writes during mesh gen)
- Two workers can build the same subchunk at once, when it's asked for again mid-build. Each upload
  carries the ticket its build began with (`MeshUploadData::ticket`), and one older than the mesh
  shown is dropped: before that, a slow stale build could land last and stay

---

## Region Tint

`WorldRenderer::setRegionTint` dims the world by a grid laid over the map, without touching the
blocks: each pixel in `chunk.frag` and `fluid.frag` blends toward a colour by the value at that
point, read smoothly between cells. `core/region_tint.hpp` holds the values, fading them toward
their targets. Fog of war is the one use today, and it hides unseen land by blending to the clear
colour, so a revealed chunk's walls and the map's edge just fade out.

The grid lives in a mapped storage buffer per frame in flight rather than a texture. An upload
blocks for about 4ms, while this can change every frame for the cost of a `memcpy`. Set
`WorldRendererConfig::regionTintCells` before `initialize`, since the buffers have a fixed size.

## Known problem

Scene geometry in the translucent list on every frame (`SceneRenderer::translucent()`) has made
the whole world draw in one flat colour, as if the chunk pass lost its texture (2026-11-13, sky
bodies tried there). The opaque list is fine. Not yet found; until it is, keep per-frame scene
geometry in `opaque()`, which cuts out clear texels.
