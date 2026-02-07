# Source File ↔ Design Document Mapping

**Purpose:** Cross-reference between implementation files and design documentation to maintain consistency and traceability.

**Status:** Audited — all source files mapped, all design docs reviewed (last audit: 2026-02-07)

---

## Core Data Structures (Doc 04)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/position.hpp` | §4.1 BlockPos, ChunkPos, ColumnPos | Position types and conversions |
| `src/core/position.cpp` | §4.1 | Position method implementations |
| `include/finevox/core/subchunk.hpp` | §4.2 SubChunk | 16³ block storage, palette refs |
| `src/core/subchunk.cpp` | §4.2, §4.4 | SubChunk + palette operations |
| `include/finevox/core/palette.hpp` | §4.4 SubChunkPalette | Per-subchunk block type mapping |
| `src/core/palette.cpp` | §4.4 | Palette management |
| `include/finevox/core/string_interner.hpp` | §4.3 StringInterner | Global string→ID mapping |
| `src/core/string_interner.cpp` | §4.3 | Interner implementation |
| `include/finevox/core/block_type.hpp` | §4.5 BlockTypeId, BlockType | Block type definitions |
| `src/core/block_type.cpp` | §4.5 | BlockRegistry, BlockType |
| `include/finevox/core/rotation.hpp` | §4.6 Rotation | 24 cube rotations |
| `src/core/rotation.cpp` | §4.6 | Rotation lookup tables |

## World Management (Doc 05)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/world.hpp` | §5.1 World | Main world interface |
| `src/core/world.cpp` | §5.1, §5.5 Force-loading | World + force-loader |
| `include/finevox/core/chunk_column.hpp` | §5.2 ChunkColumn | Vertical column of subchunks |
| `src/core/chunk_column.cpp` | §5.2 | Column implementation |
| `include/finevox/core/column_manager.hpp` | §5.4 ColumnManager | Column lifecycle state machine |
| `src/core/column_manager.cpp` | §5.4.1-5.4.6 | State transitions, LRU cache |
| `include/finevox/core/lru_cache.hpp` | §5.4.2 LRU Cache | Generic LRU cache |

## Rendering (Docs 06, 07, 22)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/mesh.hpp` | [06] §6.2 Mesh Generation | SubChunkMesh, vertex data |
| `src/core/mesh.cpp` | [06] §6.2 | Greedy meshing |
| `include/finevox/core/mesh_worker_pool.hpp` | [06] §6.4 Async Workers | Parallel mesh generation |
| `src/core/mesh_worker_pool.cpp` | [06] §6.4 | Worker thread pool |
| `include/finevox/core/mesh_rebuild_queue.hpp` | [06] §6.3 Priority Queue | Mesh rebuild scheduling |
| `include/finevox/render/world_renderer.hpp` | [06] §6.1 WorldRenderer | Render coordination |
| `src/render/world_renderer.cpp` | [06] §6.1 | View-relative rendering |
| `include/finevox/core/batch_builder.hpp` | [13] §13.1 BatchBuilder | Block operation batching |
| `src/core/batch_builder.cpp` | [13] §13.1 | Coalescing pattern |
| `include/finevox/core/lod.hpp` | [07], [22] LOD System | Level of detail |
| `src/core/lod.cpp` | [07], [22] | LOD generation |
| `include/finevox/render/subchunk_view.hpp` | [06] §6.5 SubChunkView | Read-only neighbor access |
| `src/render/subchunk_view.cpp` | [06] §6.5 | Boundary queries |

## Lighting (Docs 09, 24)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/light_data.hpp` | [09] §9.1 Light Data | Per-block light storage |
| `src/core/light_data.cpp` | [09] §9.1 | Light accessors |
| `include/finevox/core/light_engine.hpp` | [24] §24.8-24.11 | Lighting propagation |
| `src/core/light_engine.cpp` | [24] §24.8-24.11 | BFS light spread |

## Physics (Doc 08)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/physics.hpp` | §8.1-8.7 | AABB, collision, raycasting |
| `src/core/physics.cpp` | §8.1-8.7 | Step-climbing, wall glitch |

## Persistence (Doc 11)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/cbor.hpp` | §11.2 CBOR Format | CBOR encoding/decoding |
| `include/finevox/core/serialization.hpp` | §11.3 Serialization | SubChunk/Column serialize |
| `src/core/serialization.cpp` | §11.3 | CBOR serialization |
| `include/finevox/core/region_file.hpp` | §11.4 Region Files | 32×32 chunk regions |
| `src/core/region_file.cpp` | §11.4 | Region file I/O |
| `include/finevox/core/io_manager.hpp` | §11.5 IOManager | Async persistence |
| `src/core/io_manager.cpp` | §11.5 | Save/load threading |

## Event System (Doc 24)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/block_event.hpp` | §24.2 BlockEvent | Event types and data |
| `src/core/block_event.cpp` | §24.2 | Event factory methods |
| `include/finevox/core/event_queue.hpp` | §24.6 Three-Queue, §24.13 | Outbox, UpdateScheduler |
| `src/core/event_queue.cpp` | §24.6, §24.13 | Event processing loop |
| `include/finevox/core/block_handler.hpp` | §24.7 Handlers | BlockContext, BlockHandler |
| `src/core/block_handler.cpp` | §24.7 | Handler callbacks |
| `include/finevox/core/data_container.hpp` | [17] §9.1 Extra Data | Key-value storage |
| `src/core/data_container.cpp` | [17] §9.1 | DataContainer methods |

## Distance & Loading (Doc 23)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/distances.hpp` | §23.1 Distance Zones | Zone calculations |
| `include/finevox/core/config.hpp` | §23.2 Configuration | Loading policies |
| `src/core/config.cpp` | §23.2 | Config implementation |

## Configuration & Resources

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/config_file.hpp` | [Appendix A] | Config file parsing |
| `src/core/config_file.cpp` | [Appendix A] | Config loading |
| `include/finevox/core/config_parser.hpp` | [Appendix A] | Parser utilities |
| `src/core/config_parser.cpp` | [Appendix A] | Parsing implementation |
| `include/finevox/core/resource_locator.hpp` | [Appendix A] | Asset path resolution |
| `src/core/resource_locator.cpp` | [Appendix A] | Resource lookup |
| `include/finevox/render/texture_manager.hpp` | [06] §6.6 Textures | Texture atlas |
| `src/render/texture_manager.cpp` | [06] §6.6 | Texture loading |
| `include/finevox/render/block_atlas.hpp` | [06] §6.6 Block Atlas | UV coordinate mapping |
| `src/render/block_atlas.cpp` | [06] §6.6 | Atlas generation |

## Module System (Doc 18)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/module.hpp` | §18.4 ModuleLoader | Module loading API |
| `src/core/module.cpp` | §18.4 | Module implementation |
| `include/finevox/core/entity_registry.hpp` | §18.5 Registries | Entity registration |
| `src/core/entity_registry.cpp` | §18.5 | Entity management |
| `include/finevox/core/item_registry.hpp` | §18.5 Registries | Item registration |
| `src/core/item_registry.cpp` | §18.5 | Item management |

## Block Model System (Doc 19)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/block_model.hpp` | [19] §19.1-19.4 | FaceGeometry, BlockGeometry, BlockModel, RotationSet |
| `src/core/block_model.cpp` | [19] §19.1-19.4 | Model data structures |
| `include/finevox/core/block_model_loader.hpp` | [19] §19.3, §19.8 | Parser for .model/.geom/.collision files |
| `src/core/block_model_loader.cpp` | [19] §19.3, §19.8 | Uses ConfigParser (not YAML as doc originally described) |

## Entity System (Doc 25)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/entity.hpp` | [25] §25.1 | Entity base class |
| `src/core/entity.cpp` | [25] §25.1 | Entity implementation |
| `include/finevox/core/entity_manager.hpp` | [25] §25.2 | Entity lifecycle management |
| `src/core/entity_manager.cpp` | [25] §25.2 | Entity manager implementation |
| `include/finevox/core/graphics_event_queue.hpp` | [25] §25.2-25.3 | Game↔graphics thread messaging |
| `src/core/graphics_event_queue.cpp` | [25] §25.2-25.3 | Graphics event queue implementation |

## World Generation (Doc 27)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/worldgen/noise.hpp` | §27.2 Noise Library | Noise2D/3D base, Perlin, OpenSimplex, FBM, Ridged, Billow |
| `src/worldgen/noise_perlin.cpp` | §27.2.3 | Perlin gradient noise |
| `src/worldgen/noise_simplex.cpp` | §27.2.3 | OpenSimplex2 noise |
| `src/worldgen/noise_voronoi.cpp` | §27.2.6 | Voronoi cell noise |
| `include/finevox/worldgen/noise_ops.hpp` | §27.2.4 Noise Ops | ScaledNoise, DomainWarp, Combined, Clamped |
| `src/worldgen/noise_ops.cpp` | §27.2.4 | Noise operation implementations |
| `include/finevox/worldgen/noise_voronoi.hpp` | §27.2.6 | VoronoiNoise2D, VoronoiResult |
| `include/finevox/worldgen/biome.hpp` | §27.3 Biome System | BiomeId, BiomeProperties, BiomeRegistry |
| `src/worldgen/biome.cpp` | §27.3 | Biome registration |
| `include/finevox/worldgen/biome_map.hpp` | §27.3.4 BiomeMap | Voronoi + climate biome selection |
| `src/worldgen/biome_map.cpp` | §27.3.4 | BiomeMap implementation |
| `include/finevox/worldgen/biome_loader.hpp` | §27.3.5 .biome files | Data-driven biome loading |
| `src/worldgen/biome_loader.cpp` | §27.3.5 | ConfigParser-based biome file parsing |
| `include/finevox/worldgen/feature.hpp` | §27.5 Feature System | Feature base, TreeFeature, OreFeature, SchematicFeature |
| `src/worldgen/feature_tree.cpp` | §27.5.2 TreeFeature | Tree generation |
| `src/worldgen/feature_ore.cpp` | §27.5.2 OreFeature | Ore vein placement |
| `src/worldgen/feature_schematic.cpp` | §27.5.2 SchematicFeature | Schematic stamping |
| `include/finevox/worldgen/feature_registry.hpp` | §27.5.4 FeatureRegistry | Feature + placement rule registry |
| `src/worldgen/feature_registry.cpp` | §27.5.4 | Feature registration |
| `include/finevox/worldgen/feature_loader.hpp` | §27.5.5 Data files | .feature/.ore file loading |
| `src/worldgen/feature_loader.cpp` | §27.5.5 | ConfigParser-based feature file parsing |
| `include/finevox/worldgen/world_generator.hpp` | §27.4 Generation Pipeline | WorldGenerator, GenerationPipeline |
| `src/worldgen/world_generator.cpp` | §27.4 | Pipeline execution |
| `include/finevox/worldgen/generation_passes.hpp` | §27.4.4 Standard Passes | TerrainPass, SurfacePass, CavePass, OrePass, etc. |
| `src/worldgen/generation_passes.cpp` | §27.4.4 | Pass implementations |
| `include/finevox/worldgen/schematic.hpp` | §27.9 Schematic Integration | BlockSnapshot, Schematic |
| `src/worldgen/schematic.cpp` | §27.9 | Schematic storage/manipulation |
| `include/finevox/worldgen/schematic_io.hpp` | §27.9 | Schematic file I/O |
| `src/worldgen/schematic_io.cpp` | §27.9 | Schematic serialization |
| `include/finevox/worldgen/clipboard_manager.hpp` | [21] ClipboardManager | In-game copy/paste |
| `src/worldgen/clipboard_manager.cpp` | [21] | ClipboardManager implementation |

## Utilities

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/core/blocking_queue.hpp` | [14] §14.2 | Thread-safe queue (legacy, superseded by queue primitives below) |
| `include/finevox/core/queue.hpp` | [14] | Base queue interface |
| `include/finevox/core/simple_queue.hpp` | [14] | Simple FIFO queue primitive |
| `include/finevox/core/simple_queue_impl.hpp` | [14] | Simple queue template implementation |
| `include/finevox/core/coalescing_queue.hpp` | [14], [13] | Queue with key-based deduplication |
| `include/finevox/core/coalescing_queue_impl.hpp` | [14], [13] | Coalescing queue template implementation |
| `include/finevox/core/keyed_queue.hpp` | [14] | Key-associated data queue |
| `include/finevox/core/alarm_queue.hpp` | [24] §24.3 AlarmQueue | Timer-based events |
| `include/finevox/core/wake_signal.hpp` | [14] | Thread wakeup signaling primitive |
| `include/finevox/core/block_data_helpers.hpp` | [17] §9.1 | BlockTypeId storage helpers |

---

## Resolved Inconsistencies

### 1. ✅ Lifecycle Management Level (RESOLVED)

**Resolution:** Renamed `SubChunkManager` → `ColumnManager` and updated docs §5.4 to reflect column-based lifecycle with `ManagedColumn` + `ColumnState`.

### 2. ✅ SubChunk Parent Reference (RESOLVED)

**Resolution:** Not needed. All event/update code paths already have `World&` access through `BlockContext`, `UpdateScheduler`, or `LightEngine`. No back-pointer from SubChunk to ChunkColumn is required.

---

### 3. ✅ Event Processor Naming (RESOLVED)

**Resolution:** Updated doc 24 to use `UpdateScheduler` instead of `EventProcessor`. The lighting thread/queue references remain as design documentation - actual implementation integrates lighting into the event loop.

### 4. ✅ Event Consolidation Key (RESOLVED)

**Resolution:** Updated doc §24.13 to show `EventKey{pos, type}` as the outbox consolidation key. Different event types at the same position are now correctly documented as being kept separate.

### 5. ✅ Block Wrapper Class (RESOLVED)

**Resolution:** The `Block` wrapper from doc 04 was merged into `BlockContext` rather than implemented as a separate type. Both served similar purposes (ephemeral block access with convenience methods). `BlockContext` already had `World&` access for neighbor queries and tick scheduling, so Block's unique features (`isAir()`, `isOpaque()`, `isTransparent()`, `skyLight()`, `blockLight()`, `combinedLight()`, `type()`, `chunkPos()`, `localIndex()`, `rotationIndex()`) were added to BlockContext.

---

## Documented but Not Implemented (Future Work)

See [17-implementation-phases.md](17-implementation-phases.md) for authoritative checklist with `[ ]` markers.

### Core Features (Doc 04)
| Feature | Notes |
|---------|-------|
| `BlockDisplacement` storage | Sub-block positioning for off-grid placement |
| `setCustomMesh()` on SubChunk | Custom geometry for blocks |

### Persistence (Doc 11/17)
| Feature | Notes |
|---------|-------|
| LZ4 compression | Chunk data compression (infrastructure exists) |
| Region file corruption recovery | Recovery from damaged region files |

### Rendering (Doc 06/17)
| Feature | Notes |
|---------|-------|
| Off-grid displaced block face culling | Awaits BlockDisplacement |
| Entity render distance | Awaits entity system |
| Runtime LOD transition visual testing | Manual testing item |

### Module System (Doc 17/18)
| Feature | Notes |
|---------|-------|
| `CommandRegistry` | Custom command registration |
| Core game content module | Engine demo as module (test_module.cpp has examples) |

### Event System (Doc 17/24)
| Feature | Notes |
|---------|-------|
| `LightingQueue` | Dedicated lighting thread queue |
| LightEngine ↔ World integration | Config-driven automatic lighting |
| Scheduled tick persistence | Save/restore pending ticks on chunk unload/load |
| `UpdatePropagationPolicy` | Cross-chunk update queueing policy |
| Network quiescence protocol | For connected block networks |

### Unimplemented Design Docs
| Doc | Feature | Notes |
|-----|---------|-------|
| 10 | Input System | `InputManager`, `PlayerController` |
| 12 | Scripting | External dependency, not integrated |
| ~~21~~ | ~~Clipboard/Schematics~~ | Implemented in Phase 10 (see World Generation section above) |

---

## Implemented but Not Documented

### SubChunk Features
| Feature | Location | Notes |
|---------|----------|-------|
| Light storage API | `subchunk.hpp:111-173` | Full sky/block light with version tracking |
| Rotation storage API | `subchunk.hpp:176-213` | Per-block rotation indices |
| Block extra data | `subchunk.hpp:214-250` | `std::unordered_map<int32_t, DataContainer>` |
| Game tick registry | `subchunk.hpp:273-296` | `registerForGameTicks()`, `unregisterFromGameTicks()` |
| Block change callback | `subchunk.hpp:319-324` | `setBlockChangeCallback()` |

### ChunkColumn Features
| Feature | Location | Notes |
|---------|----------|-------|
| Heightmap system | `chunk_column.hpp:108-139` | Sky light optimization |
| Activity timer | `chunk_column.hpp:185-206` | Cross-chunk update protection |

### ColumnManager Features
| Feature | Location | Notes |
|---------|----------|-------|
| ManagedColumn wrapper | `column_manager.hpp:29-54` | Holds state, dirty flag, timestamps, refCount |
| IOManager integration | `column_manager.hpp:124-145` | `bindIOManager()`, `processSaveQueue()` |
| CanUnloadCallback | `column_manager.hpp:121-122` | Force-loader integration |
| Activity timeout | `column_manager.hpp:114-116` | Cross-chunk update protection |

### Event System Features
| Feature | Location | Notes |
|---------|----------|-------|
| `BlockUpdate` event type | `block_event.hpp:36` | Redstone-like propagation |
| `TickGame` event type | `block_event.hpp:33` | For registered tick blocks |
| Deferred event queue | `event_queue.hpp:263-268` | For unloaded chunk updates |
| External input queue | `event_queue.hpp:274-276` | Thread-safe event injection |
| `BlockHandler::onUse()` returns bool | `block_handler.hpp:155` | Not mentioned in docs |
| `BlockHandler::onHit()` returns bool | `block_handler.hpp:171` | Not mentioned in docs |

---

## Minor Naming/Type Differences

| Location | Doc Says | Code Does |
|----------|----------|-----------|
| §5.1 | `Chunk* getSubchunk()` | `SubChunk* getSubChunk()` |
| §24.14 | `std::chrono::milliseconds gameTickInterval` | `uint32_t gameTickIntervalMs` |
| §24.14 | `std::optional<uint64_t> randomTickSeed` | `uint64_t randomSeed = 0` (0 = use system) |

---

## Files Without Clear Doc Mapping

| Source File | Status | Notes |
|-------------|--------|-------|
| `include/finevox/core/lru_cache.hpp` | Utility | Generic LRU cache |

---

## Design Document Audit

Full audit of all design docs against source code. Last updated 2026-02-07.

### Docs With Full Source Coverage (Checked)

| Doc | Topic | Status |
|-----|-------|--------|
| 04 | Core Data Structures | ✅ Source matches (`Block` wrapper merged into `BlockContext`) |
| 05 | World Management | ✅ Updated to match ColumnManager rename |
| 06 | Rendering | ✅ Source matches |
| 07 | LOD | ✅ Source matches |
| 08 | Physics | ✅ Source matches |
| 09 | Lighting | ✅ Source matches |
| 11 | Persistence | ✅ Source matches |
| 13 | Batch Operations | ✅ Source matches |
| 17 | Implementation Phases | ✅ Source matches (roadmap doc) |
| 22 | Phase 6 LOD Design | ✅ Source matches |
| 23 | Distance and Loading | ✅ Source matches |
| 19 | Block Models | ✅ Implemented (uses ConfigParser format, not YAML as doc originally described) |
| 24 | Event System | ✅ Updated to match UpdateScheduler, EventKey |
| 25 | Entity System | ⚠️ Partial - entity.hpp, entity_manager.hpp, graphics_event_queue.hpp exist |
| 27 | World Generation | ✅ Implemented — noise, biomes, features, generation pipeline in `finevox::worldgen` |
| Appendix A | File Structure | ✅ Updated to match `include/finevox/{core,worldgen,render}/` layout |

### Docs Without Implementation (Future Work)

| Doc | Topic | Status | Notes |
|-----|-------|--------|-------|
| 10 | Input System | ❌ Not started | `InputManager`, `PlayerController` not implemented |
| 12 | Scripting | ❌ Not started | Noted as external dependency; no integration yet |
| 21 | Clipboard/Schematics | ✅ Implemented | Phase 10 — `BlockSnapshot`, `Schematic`, `ClipboardManager` in `finevox::worldgen` |

### Docs With Partial Implementation

| Doc | Topic | Status | Notes |
|-----|-------|--------|-------|
| 14 | Threading | ⚠️ Partial | Thread patterns exist (IOManager, MeshWorkerPool) but `WorkQueue<T>` → `BlockingQueue`, `VoxelGame` game loop not implemented |
| 15 | FineVK Integration | ⚠️ Mostly done | View-relative rendering working; Material API differs from design |
| 20 | Large World Coords | ✅ Done | Double-precision camera + view-relative rendering verified |

### Reference-Only Docs (No Source Mapping Needed)

| Doc | Topic | Notes |
|-----|-------|-------|
| 01 | Executive Summary | High-level overview |
| 02 | Prior Art | Design references |
| 03 | Architecture | Module overview; uses `finevox::`/`worldgen::`/`render::` namespaces (doc says `voxel::`) |
| 16 | FineVK Critique | Review document |
| 18 | Open Questions | Design decisions (modules section has source mapping above) |
| 20-rec | FineVK Recommendations | Suggestions for FineVK changes |
| Appendix B | Differences | Comparison with Minecraft |

### Known Doc-vs-Source Discrepancies (Cosmetic)

| Location | Doc Says | Code Does | Severity |
|----------|----------|-----------|----------|
| Doc 03 | `voxel::` namespace | `finevox::`, `finevox::worldgen::`, `finevox::render::` | Low — doc 03 is early architecture sketch |
| §5.1 | `Chunk* getSubchunk()` | `SubChunk* getSubChunk()` | Low — naming |
| §24.14 | `std::chrono::milliseconds gameTickInterval` | `uint32_t gameTickIntervalMs` | Low — type choice |
| §24.14 | `std::optional<uint64_t> randomTickSeed` | `uint64_t randomSeed = 0` | Low — sentinel vs optional |
| Doc 14 | `WorkQueue<T>` | `BlockingQueue<T>` / `AlarmQueue<T>` | Low — evolved design |

