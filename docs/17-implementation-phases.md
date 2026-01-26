# 17. Implementation Phases

[Back to Index](INDEX.md) | [Previous: FineStructureVK Critique](16-finestructurevk-critique.md)

---

## Build Philosophy

**Bottom-up, layered approach:**
- Build lower layers first so they rarely need revisiting
- VK-independent phases (0-3) can be built and tested without any graphics
- VK-integration phases (4-6) add rendering on top of solid foundations
- Higher phases (7-8) add polish and extensibility

**What FineStructureVK already provides** (don't duplicate):
- Vulkan device/instance/swapchain management
- GraphicsPipeline and Material system
- Buffer/Image resource management
- GameLoop with timing and frame synchronization
- Camera with frustum culling basics
- Mesh loading and texture atlas support
- Basic input via Window callbacks

**What finevox must provide:**
- World/chunk/block data model
- Mesh generation algorithms (greedy meshing)
- Physics and collision system
- Persistence/serialization (CBOR)
- Game module loading

---

## Phase 0: VK-Independent Foundation ✓

*No graphics, pure data structures. All unit-testable.*

### 0.1 Position Types
- [x] `BlockPos` - 32-bit x/y/z with packing/unpacking
- [x] `ChunkPos` - Chunk coordinates (x, y, z for subchunks)
- [x] `ColumnPos` - Column coordinates (x, z only)
- [x] `Face` enum and neighbor lookup

### 0.2 Block Registry System
- [x] `StringInterner` - String to uint32_t interning
- [x] `BlockTypeId` - Global block type identifier
- [x] `SubChunkPalette` - Per-subchunk local palette
- [x] Variable bit-width block storage (4/8/12/16 bits based on variety)

### 0.3 Core Structures
- [x] `SubChunk` - 16x16x16 block storage with palette
- [x] `ChunkColumn` - Full-height column of subchunks
- [x] `DataContainer` - Interned-key arbitrary data storage with CBOR serialization

### 0.4 Utilities
- [x] `CoalescingQueue<T>` - Deduplicating work queue
- [x] `CoalescingQueueTS<T>` - Thread-safe version
- [x] `CoalescingQueueWithData<K,V>` - Version with associated data
- [x] Rotation utilities (24 rotations, composition, inverse)

### Testing
- [x] Unit tests for all position types (packing, neighbors, conversions)
- [x] Unit tests for string interning (round-trip, thread safety)
- [x] Unit tests for palette (add types, lookup, bit-width calculations)
- [x] Unit tests for SubChunk (get/set blocks, palette expansion)
- [x] Unit tests for ChunkColumn
- [x] Unit tests for CoalescingQueue variants
- [x] Unit tests for Rotation

---

## Phase 1: VK-Independent World Management

*World class with chunk lifecycle, still no graphics.*

### 1.1 World Class
- [x] `World` - In-memory world container
- [x] Column map (ColumnPos -> ChunkColumn)
- [x] Block get/set operations through World interface
- [x] Subchunk iteration utilities

### 1.2 Lifecycle Management
- [x] `SubChunkManager` - Lifecycle state machine
- [x] States: Active, SaveQueued, Saving, UnloadQueued, Evicted
- [x] LRU cache for recently-unloaded columns
- [x] Reference counting for active columns
- [x] `currentlySaving_` set to prevent stale loads
- [x] Periodic save interval for active dirty columns (data loss prevention)

### 1.3 Batch Operations
- [x] `BatchBuilder` - Collect block changes
- [x] Coalescing: multiple changes to same block keep only latest
- [x] Atomic commit of batch to world
- [ ] **Future optimization:** Hierarchical batch commit
  - World splits batch by column, columns split by subchunk
  - SubChunk applies sub-batch in single pass (avoid repeated lookups)
  - Currently: O(n) individual setBlock calls - adequate for moderate batches

### 1.4 Dirty Tracking (Two Kinds)
**Persistence dirty** (implemented in Phase 1):
- Tracks columns that need saving to disk
- Managed by SubChunkManager lifecycle state machine
- Periodic saves prevent data loss for long-lived active columns

**Mesh dirty** (Phase 4-5):
- Tracks subchunks that need mesh regeneration
- Coalescing queue for mesh rebuild requests
- Priority by distance/visibility
- Triggers GPU buffer updates
- *Note: This is separate from persistence dirty and will be added in rendering phases*

### Testing
- [x] Create world, place/get blocks
- [x] Lifecycle transitions (active -> save queue -> saving -> unload)
- [x] LRU eviction behavior
- [x] Batch operations with coalescing

---

## Phase 2: VK-Independent Persistence

*Save/load world data, still no graphics.*

### 2.1 Serialization
- [x] CBOR encoder/decoder (custom implementation, no external library)
- [x] DataContainer serialization (entity data, tile entities)
- [x] SubChunk serialization (palette + 8/16-bit block indices)
- [x] ChunkColumn serialization (sparse subchunk storage)

### 2.2 Region Files
- [x] Region file format with journal-style ToC
- [x] Free space management (best-fit allocation)
- [x] ToC compaction (removes obsolete entries)
- [ ] LZ4 compression for chunk data

### 2.3 I/O Threads
- [x] IOManager class with async save/load
- [x] Save thread (processes save queue with callbacks)
- [x] Load thread (async load with callbacks)
- [x] Region file caching with LRU eviction
- [x] Integration with SubChunkManager (coordinate lifecycle)

### 2.4 Configuration & Resource Location
- [x] ConfigManager - Global engine settings (singleton, CBOR persistence)
- [x] WorldConfig - Per-world settings with global fallback
- [x] ResourceLocator - Unified path resolution for all resources
  - Scopes: engine/, game/, user/, world/<name>/, world/<name>/dim/<dim>/
  - World/dimension registration
  - Platform-aware user directory handling
- [x] ChunkFlags in region file header (compression flag infrastructure)

### Testing
- [x] RegionPos coordinate conversion (world to region, local indices)
- [x] TocEntry serialization round-trip
- [x] Region file create, save, load single column
- [x] Multiple columns in same region
- [x] Column overwrite (journal ToC correctly picks latest)
- [x] Negative coordinates
- [x] ToC compaction
- [x] Large columns (192 Y levels, all blocks filled)
- [x] Full region (1024 columns)
- [x] IOManager save and load round-trip
- [x] Multiple region file management
- [x] Concurrent save/load operations
- [x] Region file eviction
- [x] ConfigManager (init, save, reload, typed accessors, generic get/set)
- [x] WorldConfig (metadata, compression override, persistence)
- [x] ResourceLocator (scope resolution, world/dimension management)
- [x] Round-trip: create world -> save -> load -> verify identical
- [ ] Region file corruption recovery

---

## Phase 3: VK-Independent Physics

*Collision and raycasting, still no graphics.*

### 3.1 Collision Primitives
- [x] `AABB` - Axis-aligned bounding box
- [x] Intersection, containment, swept collision
- [x] `CollisionShape` - Collection of AABBs
- [x] `Vec3` - Floating-point 3D vector for physics

### 3.2 Block Collision
- [x] Collision box vs hit box distinction (RaycastMode enum)
- [x] BlockType provides both shapes
- [x] 24-rotation precomputation for shapes
- [x] Standard shapes: FULL_BLOCK, HALF_SLAB_BOTTOM/TOP, FENCE_POST, THIN_FLOOR

### 3.3 Raycasting
- [x] `RaycastMode` - Collision, Interaction, Both
- [x] Block raycasting through world (DDA algorithm)
- [x] Face hit detection
- [x] Ray-AABB intersection

### 3.4 Entity Physics
- [x] `PhysicsSystem` - Entity movement and collision
- [x] `PhysicsBody` interface for entities
- [x] Gravity application
- [x] Step-climbing algorithm (from EigenVoxel)
- [x] Ground detection
- [x] `SimplePhysicsBody` for testing

### Testing
- [x] AABB intersection tests
- [x] AABB swept collision tests
- [x] CollisionShape rotation tests
- [x] Ray-AABB intersection tests
- [x] Raycast hits expected blocks
- [x] Entity walks on ground, bumps into walls
- [x] Step-climbing works up to MAX_STEP_HEIGHT
- [x] Gravity integration tests

---

## Phase 4: VK Integration - Basic Rendering

*First graphics code. Depends on FineStructureVK.*

### 4.1 Mesh Generation
- [x] `ChunkVertex` - Vertex format: position, normal, texcoord, AO
- [x] `MeshData` - CPU-side vertex/index arrays
- [x] `MeshBuilder` - Simple face culling (no greedy meshing yet)
- [x] Ambient occlusion calculation (per-vertex, "0fps" algorithm)
- [x] Face diagonal flipping for AO artifact prevention
- [x] `SubChunkView` - GPU mesh handle for a subchunk (VK-dependent)

### 4.2 World Rendering
- [x] `WorldRenderer` - Renders visible subchunks
- [x] View-relative coordinates (subtract camera position)
- [x] Frustum culling (use FineStructureVK's Camera)
- [x] Texture atlas for block faces (`BlockAtlas` class)

### 4.3 Shaders
- [x] Basic vertex shader (view-relative MVP)
- [x] Basic fragment shader (texture + simple lighting)

### 4.4 Debug Features
- [x] Debug camera offset mode (renders from offset position to visualize culling)
- [x] Render statistics (loadedChunkCount, renderedChunkCount, culledChunkCount, etc.)

### 4.5 Large World Support
- [x] Double-precision camera position (`glm::dvec3`) for jitter-free rendering at large coordinates
- [x] View-relative frustum culling using double-precision AABB calculations
- [x] `WorldRenderer::updateCamera()` overload accepting high-precision position

### Testing
- [x] Unit tests for ChunkVertex construction and equality
- [x] Unit tests for MeshData (reserve, clear, memory usage)
- [x] Unit tests for MeshBuilder (face culling, AO, vertex positions, normals, UVs)
- [x] Stress tests (full subchunk, checkerboard pattern)
- [x] Render a static manually-placed world (`render_demo.cpp`)
- [x] View-relative precision at large coordinates (tested at 1,000,000 blocks)
- [x] Frustum culling excludes off-screen chunks (verified with debug camera offset)

---

## Phase 5: VK Integration - Mesh Optimization

*Performance improvements.*

### 5.1 Greedy Meshing

**Note:** Greedy meshing is confined to subchunk boundaries. This keeps frustum culling simple - each subchunk is an independent unit with its own mesh, no cross-boundary considerations needed.

- [x] Greedy mesh algorithm (merge coplanar faces within subchunk)
- [x] Per-face-direction processing
- [x] Handle transparent blocks separately (`SubChunkMeshData` with opaque/transparent split)
- [ ] Handle off-grid displaced blocks (only elide faces with matching displacement) - deferred until `BlockDisplacement` implemented
- [ ] Exclude blocks with custom/dynamic meshes from greedy merging - deferred until custom mesh system implemented

**Custom Mesh Exclusion (Future):** Blocks with dynamic appearance (stairs, sloped terrain, rotating machinery) cannot be greedy-merged because their visual representation varies per-instance. When the custom mesh system is implemented, add a `BlockType::hasCustomMesh()` flag. The greedy algorithm will skip these blocks, rendering them individually via `buildSimpleMesh()`. The `FaceMaskEntry` comparison already prevents merging blocks with different AO values, which partially handles orientation-dependent lighting.

### 5.2 Mesh Update Pipeline (Pull-Based Architecture)

**Implementation Note:** The original 4-stage push-based design was simplified to a pull-based model during implementation. This reduces complexity while maintaining good performance characteristics.

**Version-Based Change Detection**
- [x] `blockVersion_` atomic counter per SubChunk (incrementing version number)
- [x] SubChunk.setBlock() increments version (O(1), lock-free)
- [x] Render loop compares cached version vs current to detect staleness
- [x] No push notifications needed - workers pull work based on version mismatch

**FIFO Rebuild Queue with Deduplication**
- [x] `MeshRebuildQueue` (AlarmQueueWithData) - thread-safe FIFO with alarm support
- [x] Render loop iterates near-to-far, providing natural priority ordering
- [x] O(1) deduplication prevents unbounded queue growth
- [x] Non-popping `waitForWork()` with alarm-based wakeup for frame sync

**Mesh Worker Thread Pool**
- [x] `MeshWorkerPool` for parallel mesh generation
- [x] Generates CPU-side vertex/index arrays
- [x] Lock-free reads from SubChunk (palette indices stable)
- [x] Stale mesh displayed while new mesh computes

**Mesh Cache Architecture** (refined in Phase 6)
- [x] `MeshCacheEntry` per subchunk: pending mesh, version, LOD tracking
- [x] Graphics thread calls `getMesh()` - triggers rebuild if stale
- [x] Workers write directly to cache, no separate result queue
- [x] `markUploaded()` transfers pending state after GPU upload

### Testing
- [x] Greedy meshing reduces vertex count significantly (verified: 4x4x4 cube 384→24 vertices)
- [x] Nearby changes mesh faster than distant (FIFO + near-to-far render order)
- [x] No visual pops on mesh updates (stale mesh shown during rebuild)
- [x] Transparent blocks separated from opaque (7 tests in `TransparentMeshTest`)

---

## Phase 6: VK Integration - LOD System

*Distance-based detail reduction.*

### 6.1 LOD Generation
- [x] LOD levels (LOD0-LOD4: 1x, 2x, 4x, 8x, 16x block grouping)
- [x] `LODSubChunk` - Downsampled block storage for each LOD level
- [x] Mode-based representative block selection (most common solid block in group)
- [x] `MeshBuilder::buildLODMesh()` generates simplified mesh at each LOD
- [x] LOD meshes use scaled block geometry (2x, 4x, 8x, 16x block sizes)

### 6.2 LOD Selection
- [x] `LODConfig` - Configurable distance thresholds for each LOD level
- [x] `LODRequest` with 2x encoding for hysteresis (flexible zones at boundaries)
- [x] Distance-based LOD switching via `getRequestForDistance()`
- [x] Debug controls: `lodBias` (shift all distances), `forceLOD` (force specific level)
- [x] `LODDebugMode` enum for visualization (ColorByLOD, WireframeByLOD, ShowBoundaries)

### 6.3 Mesh Pipeline Integration
- [x] `WorldRenderer` LOD configuration and enable/disable
- [x] `MeshRebuildRequest` carries LOD request with hysteresis encoding
- [x] `SubChunkView` tracks `lastBuiltLOD` for staleness detection
- [x] `MeshWorkerPool` builds meshes at requested LOD level
- [x] `MeshCacheEntry` tracks pending/uploaded LOD per chunk

### 6.4 Worker Thread Architecture (Refined)
- [x] `AlarmQueue` - Thread-safe queue with alarm-based wakeup for frame sync
- [x] Pull-based mesh cache: workers write to cache, graphics thread queries
- [x] `getMesh()` API triggers rebuilds when version or LOD is stale
- [x] `markUploaded()` transfers pending mesh state to uploaded state
- [x] Background scanning finds stale chunks without explicit requests
- [x] `BlockingQueue` deprecated in favor of `AlarmQueue`

### 6.5 Chunk Lifecycle Integration
- [x] `SubChunkManager::setChunkLoadCallback()` for chunk load notifications
- [x] `WorldRenderer::markColumnDirty()` for marking newly loaded columns
- [x] `MeshCacheEntry` uses `weak_ptr<SubChunk>` for version checking without ownership
- [x] Stale meshes continue rendering after chunk unload (graceful degradation)
- [x] Decoupled chunk lifecycle from mesh lifecycle (semi-independent systems)

**Design Note:** The chunk lifecycle (SubChunkManager) and rendering lifecycle (WorldRenderer/MeshWorkerPool) are intentionally semi-independent:
- **Chunk unload**: Mesh views use weak pointers, so stale meshes can persist and render
- **Chunk load**: Callback notifies renderer to request meshes for new chunks
- **Version tracking**: Atomic `blockVersion_` counter enables lock-free staleness detection

### 6.6 GPU Memory Management
- [x] `SubChunkView::gpuMemoryBytes()` tracks allocated GPU memory per mesh
- [x] `WorldRendererConfig` GPU memory settings:
  - `gpuMemoryBudget` - Target GPU memory limit (default 512MB)
  - `unloadDistanceMultiplier` - Hysteresis for unload distance (default 1.2x)
  - `maxUnloadsPerFrame` - Limit unloads to avoid GPU stalls (default 16)
- [x] `WorldRenderer::gpuMemoryUsed()` - Sum of all loaded mesh memory
- [x] `WorldRenderer::unloadDistantChunks()` - Hysteresis-based unloading with per-frame limit
- [x] `WorldRenderer::enforceMemoryBudget()` - Unload furthest out-of-view chunks when over budget
- [x] `WorldRenderer::performCleanup()` - Combined cleanup (distance + budget enforcement)

### 6.7 Distance Configuration
- [x] `DistanceConfig` struct in `distances.hpp` for all distance thresholds:
  - `RenderDistanceConfig` - chunk and entity render distances
  - `FogConfig` - fog start/end distances, color, dynamic color flag
  - `LoadingDistanceConfig` - chunk loading distances with hysteresis
  - `ProcessingDistanceConfig` - block update, entity, simulation distances
- [x] `FogConfig` integrated into `WorldRendererConfig`
- [x] Fog configuration API in WorldRenderer:
  - `fogConfig()`, `setFogEnabled()`, `setFogDistances()`, `setFogColor()`
  - `setFogDynamicColor()`, `getFogFactor()`
- [x] Fog shader support:
  - Vertex shader passes `fragDistance` to fragment shader
  - Push constants include fog parameters (start, end, color)
  - Fragment shader applies linear fog blend based on distance
- [ ] Add entity render distance (when entity system exists)

See [23 - Distance and Loading](23-distance-and-loading.md) for full design.

### Testing
- [x] LOD level utilities (grouping, resolution, conversions)
- [x] LODRequest hysteresis encoding (exact vs flexible, accepts())
- [x] LODConfig distance thresholds, bias, force modes
- [x] LODSubChunk downsampling from SubChunk
- [x] LOD mesh generation (scaling, face culling at each level)
- [x] MeshWorkerPool cache API (getMesh, markUploaded, version/LOD tracking)
- [x] AlarmQueue alarm-based wakeup and FIFO ordering
- [x] SubChunkManager ChunkLoadCallback fires on add() and requestLoad()
- [ ] Runtime LOD transitions at boundaries (visual testing)
- [x] Memory usage bounded via hysteresis unloading and budget enforcement

---

## Phase 7: Module System ✓

*Game-agnostic plugin architecture.*

### 7.1 Module Loader
- [x] `ModuleLoader` - Load .so/.dll shared objects via dlopen/LoadLibrary
- [x] `GameModule` interface (name, version, dependencies, lifecycle)
- [x] Entry point: `extern "C" GameModule* finevox_create_module()`
- [x] `FINEVOX_MODULE(ModuleClass)` macro for entry point definition
- [x] Dependency resolution with topological sort (Kahn's algorithm)
- [x] Module initialization order based on dependencies

### 7.2 Registries
- [x] `BlockRegistry` - Block type registration with handler support
  - Block types (collision shapes, properties)
  - Block handlers (stateless behavior)
  - Handler factories for lazy loading
  - Namespace utilities (`namespace:localname` format validation)
- [x] `EntityRegistry` - Entity type registration (stub)
- [x] `ItemRegistry` - Item registration (stub)
- [ ] `CommandRegistry` - Custom command registration (future)

### 7.3 Block Handler System
- [x] `BlockHandler` - Stateless block behavior interface
  - Lifecycle: onPlace, onBreak
  - Ticks: onTick (scheduled, repeating, random)
  - Events: onNeighborChanged, onUse, onHit
  - Visual: onRepaint
- [x] `BlockContext` - Ephemeral context passed to handlers
  - Access to world, subchunk, block position
  - Rotation get/set (storage deferred to Phase 9)
  - Extra data access (storage deferred to Phase 9)
  - Tick scheduling (deferred to Phase 9)
  - Neighbor notification
  - [x] `previousType()`, `previousData()` for place/break undo support
  - [x] `setBlock()` for handlers to modify/undo placements
- [x] `BlockEvent` - Unified event container for event system
  - EventType enum (BlockPlaced, BlockBroken, NeighborChanged, Tick, PlayerUse/Hit, etc.)
  - Factory methods for creating events
  - Sentinel checks for event type classification

### 7.4 Module Context
- [x] `ModuleRegistry` - Context provided during registration
  - Namespace-aware content registration
  - `qualifiedName()` auto-prefixes with module namespace
  - Access to BlockRegistry, EntityRegistry, ItemRegistry
  - Logging utilities (log, warn, error)

### 7.5 Core Module
- [ ] Core game content as a module (not engine built-in) - deferred
- [ ] Demonstrates module API usage - see test_module.cpp for examples

### Testing
- [x] Module registration and lookup
- [x] Duplicate module rejection
- [x] Lifecycle methods called in order
- [x] Dependency resolution (A→B→C chain)
- [x] Missing dependency detection
- [x] Namespace validation (valid/invalid formats)
- [x] Block handler registration and retrieval
- [x] Handler factory lazy loading
- [x] Handler lookup by BlockTypeId
- [x] Entity/Item registry stubs

---

## Phase 8: Lighting System ✓

*Block and sky lighting.*

### 8.1 Light Storage
- [x] `LightData` class - Separate lighting data from block data
- [x] Block light (0-15) per block - 4 bits in packed byte
- [x] Sky light (0-15) per block - 4 bits in packed byte
- [x] Heightmap per ChunkColumn for sky light optimization
- [x] BlockType lighting properties: `lightEmission`, `lightAttenuation`, `blocksSkyLight`
- [x] `lightInitialized_` flag in ChunkColumn for lazy sky light init

### 8.2 Light Propagation
- [x] `LightEngine` - BFS propagation on block changes
- [x] Sky light column initialization and propagation
- [x] Light removal algorithm with re-propagation
- [x] Custom attenuation callbacks for special materials (water)

### 8.3 Smooth Lighting
- [x] Per-vertex AO calculation (already existed)
- [x] Per-vertex light values in ChunkVertex
- [x] `getFaceLight()` averages light from 4 adjacent blocks per corner
- [x] Smooth lighting integrated into MeshBuilder (configurable via `setSmoothLighting()`)
- [x] `BlockLightProvider` callback for mesh generation

### 8.4 Version Tracking (for mesh invalidation)
- [x] `blockVersion_` atomic in SubChunk (auto-incremented by setBlock)
- [x] `lightVersion_` atomic in SubChunk (incremented on light changes)
- [x] Version comparison for mesh staleness detection

### 8.5 Event System Integration (Partial - See doc 24)
- [x] `BlockEvent` struct with factory methods
- [x] `EventType` enum for event classification
- [x] `BlockContext` extended with previousType/previousData for undo
- [ ] `EventProcessor` with inbox/outbox pattern
- [ ] `LightingQueue` - consolidating queue for lighting thread
- [ ] Integration of LightEngine with World (optional, config-driven)

### Testing
- [x] LightData storage and access
- [x] BlockType lighting properties
- [x] Heightmap updates
- [x] LightEngine propagation
- [x] Light utility functions

---

## Phase 9: Block Update System

*Scheduled block updates for game logic (redstone, automation, etc.)*

**Note:** This phase spans engine and game layers. Engine provides scheduling mechanisms; game modules define update behavior.

### 9.1 Engine: Data Storage ✓
- [x] Add block extra data to SubChunk (tile entity data - chests, signs, etc.)
  - `blockData(index)` - returns nullptr if no data
  - `getOrCreateBlockData(index)` - creates if needed
  - Sparse map storage (most blocks have no extra data)
- [x] Add `data()` to SubChunk for per-subchunk game state
  - Lighting cache, local heightmaps, biome blends
  - Game-defined format via DataContainer
- [x] Add `data()` to ChunkColumn for per-column game state
  - Stores pending block events when unloading mid-update
  - Game-defined format via DataContainer
- [x] Serialize block/subchunk/column extra data with chunks
- [x] BlockContext `data()` and `getOrCreateData()` implemented

**Standard Data Hierarchy:**

Block, subchunk, and column extra data use DataContainer with standard top-level keys:

| Key | Description | Example usage |
|-----|-------------|---------------|
| `inventory` | Item storage for blocks with inventories | Chests, hoppers, furnaces |
| `geometry` | Dynamic geometry data based on neighbors | Stairs orientation, fence connections |
| `display` | Custom display/rendering data | Sign text, banner patterns |
| `state` | Block-specific state | Furnace progress, hopper cooldown |

Game modules may define additional keys. Use namespace prefixes for mod-specific keys (e.g., `mymod:custom_data`).

Example usage:
```cpp
// In a chest's onPlace handler:
DataContainer& data = ctx.getOrCreateData();
auto inventory = std::make_unique<DataContainer>();
inventory->set("slots", 27);  // 27 slots
data.set("inventory", std::move(inventory));

// In onUse handler:
DataContainer* data = ctx.data();
if (data && data->has("inventory")) {
    // Open inventory UI
}
```

### 9.2 Engine: Update Scheduler
- [ ] `BlockUpdateScheduler` - schedule/cancel timed updates
- [ ] Per-chunk update queues with distance filtering
- [ ] Persistence of pending updates across save/load
- [ ] Cross-chunk update boundary handling

### 9.3 Engine: Force Loading
- [ ] `ChunkForceLoader` - ticket-based force loading
- [ ] Activity timer per chunk
- [ ] Unload veto mechanism

### 9.4 Game: Update Propagation (Module-Defined)
- [ ] `UpdatePropagationPolicy` interface
- [ ] Cross-chunk update queueing vs loading decision
- [ ] Network quiescence protocol for connected blocks

See [23 - Distance and Loading](23-distance-and-loading.md) for detailed design.

### Testing
- [ ] Schedule and execute block updates
- [ ] Updates persist across save/load
- [ ] Force-load prevents chunk unloading
- [ ] Cross-chunk updates handled correctly

---

## Future Phases (Not Scheduled)

These are documented for completeness but not in initial implementation scope:

### Multiplayer
- Client/server architecture
- World state synchronization
- Client-side prediction

### Advanced Graphics
- Translucent block sorting
- Water rendering with refraction
- Sky rendering (sun, moon, clouds)

### World Generation
- Noise generators
- Biome system
- Structure generation

### World Editing / Clipboard System
- `BlockSnapshot` and `Schematic` structures
- Region extraction and placement
- CBOR serialization for schematic files
- `ClipboardManager` for runtime copy/paste
- Transformation utilities (rotate, mirror, crop)
- See [21 - Clipboard and Schematic System](21-clipboard-schematic.md) for full design

### Fluid System
Engine should provide helper infrastructure for fluid simulation:
- **Fluid properties**: Spread rate, slope/viscosity, transparency, light absorption
- **Source block mechanics**: Game-level mechanic where two adjacent source blocks flowing into a gap create a new source block (engine provides helper, game defines policy)
- **Visual aspects**: Dynamic meshes for fluid surfaces, translucency sorting
- **Light interaction**: Light levels through translucent fluids, caustics (optional)
- **Physics**: Buoyancy, flow direction affecting entities
- **Integration**: Game modules register fluid types with properties, engine handles propagation scheduling

---

## Phase Dependencies

```
Phase 0 (Data Structures)
    ↓
Phase 1 (World Management)
    ↓
Phase 2 (Persistence) ←───────────────────────┐
    ↓                                         │
Phase 3 (Physics)                             │
    ↓                                         │
────────────────────────────────────          │
VK-Independent above | VK-Dependent below     │
────────────────────────────────────          │
    ↓                                         │
Phase 4 (Basic Rendering)                     │
    ↓                                         │
Phase 5 (Mesh Optimization)                   │
    ↓                                         │
Phase 6 (LOD System)                          │
    ↓                                         │
Phase 7 (Module System) ──────────────────────┘
    ↓                     (modules can use persistence)
Phase 8 (Lighting)
    ↓
Phase 9 (Block Updates) ← Engine + Game layer
```

---

[Next: Open Questions](18-open-questions.md)
