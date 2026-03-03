# FineStructureVoxel — Architecture

---

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│  Game Modules  (loaded .so/.dll)                                │  ← Games built here
├─────────────────────────────────────────────────────────────────┤
│  finevox Engine  (five shared libraries)                        │  ← This project
│  ├── libfinevox  (core)  —  finevox::                          │
│  │   ├── World, SubChunk, ChunkColumn, BlockType               │
│  │   ├── Physics, Persistence (CBOR, region files)             │
│  │   ├── Events, UpdateScheduler, BlockHandler                 │
│  │   ├── Items, Tags, Inventory                                │
│  │   ├── GameSession, GameActions, game thread                 │
│  │   └── Fluids (FluidType, FluidSimulator, FluidTickManager)  │
│  ├── libfinevox_worldgen  —  finevox::worldgen::               │
│  │   ├── Noise (Perlin, Simplex, Voronoi, FBM, DomainWarp)    │
│  │   ├── BiomeRegistry, BiomeMap                               │
│  │   ├── FeatureRegistry, GenerationPipeline                   │
│  │   └── Schematics, ClipboardManager                          │
│  ├── libfinevox_render  —  finevox::render::                   │
│  │   ├── WorldRenderer, SubChunkView, FluidMeshBuilder         │
│  │   ├── MeshWorkerPool, MeshRebuildQueue                      │
│  │   ├── BlockAtlas, TextureManager                            │
│  │   └── LOD system, FrameFenceWaiter                          │
│  ├── libfinevox_audio  —  finevox::audio::                     │
│  │   ├── AudioEngine (miniaudio backend)                       │
│  │   ├── SoundLoader, FootstepTracker                          │
│  │   └── SoundRegistry, SoundEvent (types in core)             │
│  └── libfinevox_script  —  finevox::script::                   │
│      ├── GameScriptEngine (owns ScriptEngine + ScriptCache)    │
│      ├── ScriptBlockHandler, ScriptEntityHandler               │
│      └── BlockContextProxy, DataContainerProxy, EntityContextProxy│
├─────────────────────────────────────────────────────────────────┤
│  finegui                                                        │  ← GUI toolkit
│  └── Dear ImGui + finevk Vulkan backend                        │
├─────────────────────────────────────────────────────────────────┤
│  finescript  (game-language project)                            │  ← Scripting language
│  └── ScriptEngine, ProxyMap, ExecutionContext, Interner        │
├─────────────────────────────────────────────────────────────────┤
│  FineStructureVK  (finevk)                                      │  ← Vulkan wrapper
│  ├── Device, Swapchain, Pipelines, Buffers/Images              │
│  ├── GameLoop, InputManager, InputEvent                        │
│  ├── Camera (double-precision), Overlay2D                      │
│  └── RenderSurface, Frame, deferDelete                         │
├─────────────────────────────────────────────────────────────────┤
│  Vulkan / GLFW / GLM                                            │  ← System
└─────────────────────────────────────────────────────────────────┘
```

---

## Five Shared Libraries

| Library | CMake Target | Namespace | Links |
|---------|-------------|-----------|-------|
| `libfinevox` | `finevox` | `finevox::` | — |
| `libfinevox_worldgen` | `finevox_worldgen` | `finevox::worldgen::` | `finevox` PUBLIC |
| `libfinevox_render` | `finevox_render` | `finevox::render::` | `finevox` PUBLIC, finevk |
| `libfinevox_audio` | `finevox_audio` | `finevox::audio::` | `finevox`, miniaudio |
| `libfinevox_script` | `finevox_script` | `finevox::script::` | `finevox`, finescript |

Tests link `finevox_worldgen` (gets core transitively). The script library is always built (`FINEVOX_BUILD_AUDIO` is optional for audio).

---

## Directory Layout

```
FineStructureVoxel/
├── include/finevox/
│   ├── core/               ← libfinevox headers
│   ├── worldgen/           ← libfinevox_worldgen headers
│   ├── render/             ← libfinevox_render headers
│   ├── audio/              ← libfinevox_audio headers
│   └── script/             ← libfinevox_script headers
├── src/
│   ├── core/               ← libfinevox sources (mirrors headers)
│   ├── worldgen/
│   ├── render/
│   ├── audio/
│   └── script/
├── tests/                  ← all unit tests
├── resources/
│   ├── blocks/             ← .model, .geom, .collision files
│   ├── biomes/             ← .biome files
│   ├── features/           ← .feature, .ore files
│   ├── fluids/             ← .fluid files (water.fluid, lava.fluid)
│   ├── sounds/             ← .sound files
│   ├── shaders/            ← .vert, .frag GLSL sources
│   └── ui/                 ← .fs finescript UI definitions
├── apps/
│   └── render_demo/        ← main demo executable
├── docs/                   ← current docs (this directory)
└── old_docs/               ← archived original docs
```

---

## External Project Locations

| Project | Path | Purpose |
|---------|------|---------|
| FineStructureVK (finevk) | `/Users/theosib/projects/FineStructureVK/` | Vulkan wrapper |
| finegui | `/Users/theosib/projects/finegui/` | GUI toolkit |
| finescript (game-language) | `/Users/theosib/projects/game-language/` | Scripting language |

finescript headers use `.h` not `.hpp`. finegui links: `libfinegui.a` + `libfinegui-script.a` + `libfinegui-retained.a`.

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Block registry | Per-subchunk palette + global string interning | Unlimited types, compressible storage |
| Loading unit | Full-height columns (16x16xHeight) | Avoids Y-level loading issues |
| Serialization | CBOR (RFC 8949) | Self-describing, standardized, compact |
| Rendering | View-relative coordinates | Solves float precision at large world coords |
| Mesh updates | Push-based (block/light changes push rebuild) | Implicit ordering, no stale-detection polling |
| Mod system | Shared object (.so/.dll) loading | All game content is external modules |
| Coord types | Double precision world, single precision render | Jitter-free at >1M block coordinates |
| Collision vs Hit | Separate AABB shapes | Physics vs raycasting use different boxes |
| Fluid storage | Per-SubChunk lazy FluidLayer (packed uint8_t[4096]) | Zero overhead when no fluids present |

---

## finevk API — Key Surface

```cpp
// Frame rendering
frame.beginRenderPass();
frame.endRenderPass();
renderer->endFrame();
frame.extent                  // VkExtent2D (field, not function)
frame.frameIndex()            // uint32_t

// Window
window->contentScale()        // float (HiDPI scale)
window->windowSize()          // glm::ivec2 (screen coords, use for UI)
window->size()                // VkExtent2D (framebuffer pixels)
window->isHighDPI()           // bool

// GPU cleanup
RenderSurface::deferDelete()  // safe cleanup from render thread
```

---

## Threading Model

| Thread | Responsibilities |
|--------|-----------------|
| Main / Render | finevk game loop, input, camera, WorldRenderer draw calls |
| Game thread | 30 TPS tick loop: WorldTime, UpdateScheduler, EntityManager, FluidTickManager |
| Mesh workers | MeshWorkerPool: parallel CPU mesh generation, write to MeshCacheEntry |
| IO threads | IOManager: async save/load of region files |
| Light thread | LightEngine BFS propagation |
| Audio thread | miniaudio callback (never call ma_sound_uninit from here) |
| Fence-wait thread | FrameFenceWaiter: overlaps mesh processing with GPU fence wait |
