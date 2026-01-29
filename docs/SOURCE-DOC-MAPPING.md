# Source File ↔ Design Document Mapping

**Purpose:** Cross-reference between implementation files and design documentation to maintain consistency and traceability.

**Status:** Audited — all source files mapped, all design docs reviewed

---

## Core Data Structures (Doc 04)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/position.hpp` | §4.1 BlockPos, ChunkPos, ColumnPos | Position types and conversions |
| `src/position.cpp` | §4.1 | Position method implementations |
| `include/finevox/subchunk.hpp` | §4.2 SubChunk | 16³ block storage, palette refs |
| `src/subchunk.cpp` | §4.2, §4.4 | SubChunk + palette operations |
| `include/finevox/palette.hpp` | §4.4 SubChunkPalette | Per-subchunk block type mapping |
| `src/palette.cpp` | §4.4 | Palette management |
| `include/finevox/string_interner.hpp` | §4.3 StringInterner | Global string→ID mapping |
| `src/string_interner.cpp` | §4.3 | Interner implementation |
| `include/finevox/block_type.hpp` | §4.5 BlockTypeId, BlockType | Block type definitions |
| `src/block_type.cpp` | §4.5 | BlockRegistry, BlockType |
| `include/finevox/rotation.hpp` | §4.6 Rotation | 24 cube rotations |
| `src/rotation.cpp` | §4.6 | Rotation lookup tables |

## World Management (Doc 05)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/world.hpp` | §5.1 World | Main world interface |
| `src/world.cpp` | §5.1, §5.5 Force-loading | World + force-loader |
| `include/finevox/chunk_column.hpp` | §5.2 ChunkColumn | Vertical column of subchunks |
| `src/chunk_column.cpp` | §5.2 | Column implementation |
| `include/finevox/column_manager.hpp` | §5.4 ColumnManager | Column lifecycle state machine |
| `src/column_manager.cpp` | §5.4.1-5.4.6 | State transitions, LRU cache |
| `include/finevox/lru_cache.hpp` | §5.4.2 LRU Cache | Generic LRU cache |

## Rendering (Docs 06, 07, 22)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/mesh.hpp` | [06] §6.2 Mesh Generation | SubChunkMesh, vertex data |
| `src/mesh.cpp` | [06] §6.2 | Greedy meshing |
| `include/finevox/mesh_worker_pool.hpp` | [06] §6.4 Async Workers | Parallel mesh generation |
| `src/mesh_worker_pool.cpp` | [06] §6.4 | Worker thread pool |
| `include/finevox/mesh_rebuild_queue.hpp` | [06] §6.3 Priority Queue | Mesh rebuild scheduling |
| `include/finevox/world_renderer.hpp` | [06] §6.1 WorldRenderer | Render coordination |
| `src/world_renderer.cpp` | [06] §6.1 | View-relative rendering |
| `include/finevox/batch_builder.hpp` | [13] §13.1 BatchBuilder | Block operation batching |
| `src/batch_builder.cpp` | [13] §13.1 | Coalescing pattern |
| `include/finevox/lod.hpp` | [07], [22] LOD System | Level of detail |
| `src/lod.cpp` | [07], [22] | LOD generation |
| `include/finevox/subchunk_view.hpp` | [06] §6.5 SubChunkView | Read-only neighbor access |
| `src/subchunk_view.cpp` | [06] §6.5 | Boundary queries |

## Lighting (Docs 09, 24)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/light_data.hpp` | [09] §9.1 Light Data | Per-block light storage |
| `src/light_data.cpp` | [09] §9.1 | Light accessors |
| `include/finevox/light_engine.hpp` | [24] §24.8-24.11 | Lighting propagation |
| `src/light_engine.cpp` | [24] §24.8-24.11 | BFS light spread |

## Physics (Doc 08)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/physics.hpp` | §8.1-8.7 | AABB, collision, raycasting |
| `src/physics.cpp` | §8.1-8.7 | Step-climbing, wall glitch |

## Persistence (Doc 11)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/cbor.hpp` | §11.2 CBOR Format | CBOR encoding/decoding |
| `include/finevox/serialization.hpp` | §11.3 Serialization | SubChunk/Column serialize |
| `src/serialization.cpp` | §11.3 | CBOR serialization |
| `include/finevox/region_file.hpp` | §11.4 Region Files | 32×32 chunk regions |
| `src/region_file.cpp` | §11.4 | Region file I/O |
| `include/finevox/io_manager.hpp` | §11.5 IOManager | Async persistence |
| `src/io_manager.cpp` | §11.5 | Save/load threading |

## Event System (Doc 24)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/block_event.hpp` | §24.2 BlockEvent | Event types and data |
| `src/block_event.cpp` | §24.2 | Event factory methods |
| `include/finevox/event_queue.hpp` | §24.6 Three-Queue, §24.13 | Outbox, UpdateScheduler |
| `src/event_queue.cpp` | §24.6, §24.13 | Event processing loop |
| `include/finevox/block_handler.hpp` | §24.7 Handlers | BlockContext, BlockHandler |
| `src/block_handler.cpp` | §24.7 | Handler callbacks |
| `include/finevox/data_container.hpp` | [17] §9.1 Extra Data | Key-value storage |
| `src/data_container.cpp` | [17] §9.1 | DataContainer methods |

## Distance & Loading (Doc 23)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/distances.hpp` | §23.1 Distance Zones | Zone calculations |
| `include/finevox/config.hpp` | §23.2 Configuration | Loading policies |
| `src/config.cpp` | §23.2 | Config implementation |

## Configuration & Resources

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/config_file.hpp` | [Appendix A] | Config file parsing |
| `src/config_file.cpp` | [Appendix A] | Config loading |
| `include/finevox/config_parser.hpp` | [Appendix A] | Parser utilities |
| `src/config_parser.cpp` | [Appendix A] | Parsing implementation |
| `include/finevox/resource_locator.hpp` | [Appendix A] | Asset path resolution |
| `src/resource_locator.cpp` | [Appendix A] | Resource lookup |
| `include/finevox/texture_manager.hpp` | [06] §6.6 Textures | Texture atlas |
| `src/texture_manager.cpp` | [06] §6.6 | Texture loading |
| `include/finevox/block_atlas.hpp` | [06] §6.6 Block Atlas | UV coordinate mapping |
| `src/block_atlas.cpp` | [06] §6.6 | Atlas generation |

## Module System (Doc 18)

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/module.hpp` | §18.4 ModuleLoader | Module loading API |
| `src/module.cpp` | §18.4 | Module implementation |
| `include/finevox/entity_registry.hpp` | §18.5 Registries | Entity registration |
| `src/entity_registry.cpp` | §18.5 | Entity management |
| `include/finevox/item_registry.hpp` | §18.5 Registries | Item registration |
| `src/item_registry.cpp` | §18.5 | Item management |

## Utilities

| Source File | Design Section | Notes |
|-------------|----------------|-------|
| `include/finevox/blocking_queue.hpp` | [14] §14.2 | Thread-safe queue |
| `include/finevox/alarm_queue.hpp` | [24] §24.3 AlarmQueue | Timer-based events |
| `include/finevox/block_data_helpers.hpp` | [17] §9.1 | BlockTypeId storage helpers |

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
| Custom mesh exclusion from greedy meshing | Awaits custom mesh system |
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
| 19 | Block Models | Model loader, YAML parsing |
| 21 | Clipboard/Schematics | `BlockSnapshot`, `Schematic`, `ClipboardManager` |

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
| `include/finevox/blocking_queue.hpp` | Utility | Generic thread-safe queue (deprecated, use AlarmQueue) |
| `include/finevox/lru_cache.hpp` | Utility | Generic LRU cache |

---

## Design Document Audit

Full audit of all design docs against source code, completed Jan 2026.

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
| 24 | Event System | ✅ Updated to match UpdateScheduler, EventKey |
| Appendix A | File Structure | ✅ Source matches |

### Docs Without Implementation (Future Work)

| Doc | Topic | Status | Notes |
|-----|-------|--------|-------|
| 10 | Input System | ❌ Not started | `InputManager`, `PlayerController` not implemented |
| 12 | Scripting | ❌ Not started | Noted as external dependency; no integration yet |
| 19 | Block Models | ❌ Design only | Model loader, YAML parsing, `hasCustomMesh()` not implemented |
| 21 | Clipboard/Schematics | ❌ Not started | `BlockSnapshot`, `Schematic`, `ClipboardManager` not implemented |

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
| 03 | Architecture | Module overview; uses `finevox::` namespace (doc says `voxel::`) |
| 16 | FineVK Critique | Review document |
| 18 | Open Questions | Design decisions (modules section has source mapping above) |
| 20-rec | FineVK Recommendations | Suggestions for FineVK changes |
| Appendix B | Differences | Comparison with Minecraft |

### Known Doc-vs-Source Discrepancies (Cosmetic)

| Location | Doc Says | Code Does | Severity |
|----------|----------|-----------|----------|
| Doc 03 | `voxel::` namespace | `finevox::` | Low — doc 03 is early architecture sketch |
| §5.1 | `Chunk* getSubchunk()` | `SubChunk* getSubChunk()` | Low — naming |
| §24.14 | `std::chrono::milliseconds gameTickInterval` | `uint32_t gameTickIntervalMs` | Low — type choice |
| §24.14 | `std::optional<uint64_t> randomTickSeed` | `uint64_t randomSeed = 0` | Low — sentinel vs optional |
| Doc 14 | `WorkQueue<T>` | `BlockingQueue<T>` / `AlarmQueue<T>` | Low — evolved design |

