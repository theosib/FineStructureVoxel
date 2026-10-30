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

## Shared Libraries

| Library | CMake Target | Namespace | Links |
|---------|-------------|-----------|-------|
| `libfinevox` | `finevox` | `finevox::` | `lz4_static`, `glm::glm`, `finescript` |
| `libfinevox_worldgen` | `finevox_worldgen` | `finevox::worldgen::` | `finevox` PUBLIC |
| `libfinevox_render` | `finevox_render` | `finevox::render::` | `finevox` PUBLIC, finevk |
| `libfinevox_audio` | `finevox_audio` | `finevox::audio::` | `finevox`, miniaudio |
| `libfinevox_script` | `finevox_script` | `finevox::script::` | `finevox`, finescript |
| `libfinevox_game` | `finevox_game` | `finevox::game::` | `finevox_worldgen`, `finevox_script` |

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
| FineStructureVK (finevk) | `/Users/theosib/projects/FineStructure/FineStructureVK/` | Vulkan wrapper |
| finegui | `/Users/theosib/projects/FineStructure/finegui/` | GUI toolkit |
| finescript (game-language) | `/Users/theosib/projects/FineStructure/game-language/` | Scripting language |
| finenet | `/Users/theosib/projects/FineStructure/finenet/` | Multiplexed channel & queue transport (local + network) |

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
| Game thread | 30 TPS tick loop: WorldTime, UpdateScheduler, EntityManager, FluidTickManager, navigation, vision, game script |
| Mesh workers | MeshWorkerPool: parallel CPU mesh generation, write to MeshCacheEntry |
| IO threads | IOManager: async save/load of region files |
| Light thread | LightEngine BFS propagation |
| Audio thread | miniaudio callback (never call ma_sound_uninit from here) |
| Fence-wait thread | FrameFenceWaiter: overlaps mesh processing with GPU fence wait |

### World data: writers lock, readers don't

Block, light, and fluid reads happen millions of times a second on every thread above, and on
a server they'd multiply again by the clients. A `shared_mutex` around them doesn't share:
readers still fight over its cache line, and eight threads got *less* done than one (970 ns a
read against 35 ns). So the storage has three rules, and adding to it means keeping them:

1. **Readers never block.** A read of a block, light level, or fluid is a few atomic loads.
   `World`'s `columnMutex_` and `ChunkColumn`'s `slotMutex_` are for writers, and only writers.
2. **Memory a reader can reach stays valid.** Nothing a reader might be inside is freed while
   the world runs. A subchunk "removed" with its last block leaves the directory and stays
   allocated for reuse; a `SubChunk*` is good for the life of its column. Palette buffers
   outgrown, fluid layers removed, and index tables replaced all stay until their owner goes.
3. **Stale is fine, torn is not.** A reader may see a value one update old. Whatever it fed
   gets corrected by the version bump and rebuild that follow the write. The one ordering that
   matters is that a palette entry is published before any block index that refers to it, so a
   reader never sees a block type that was never there.

`shared_cell.hpp` holds the access pattern for plain arrays. `ColumnIndex` is the lock-free
lookup of columns; `columns_` next to it is the writers' copy. To check any of this, build with
`-fsanitize=thread` and run the game: it should report nothing.

Not yet covered by these rules:

- With a `ColumnManager` attached (streaming worlds), a read that misses `columnIndex_` takes
  the manager's lock, and an unloading column is freed with no thought for readers. Needs a
  grace period before free: this is the next thing a multiplayer server would hit.
- `BlockRegistry::getType` takes a `shared_mutex` per call, and the mesher calls it per face.
  Types only change at startup, so it can freeze into a plain table.
- `compactPalette` rewrites indices and palette non-atomically, and a mesh built during it can
  stay wrong until the next change. Saving should compact a copy.
