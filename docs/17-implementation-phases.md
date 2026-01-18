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
- [ ] `WorldRenderer` - Renders visible subchunks
- [ ] View-relative coordinates (subtract camera position)
- [ ] Frustum culling (use FineStructureVK's Camera)
- [ ] Texture atlas for block faces

### 4.3 Shaders
- [ ] Basic vertex shader (view-relative MVP)
- [ ] Basic fragment shader (texture + simple lighting)

### Testing
- [x] Unit tests for ChunkVertex construction and equality
- [x] Unit tests for MeshData (reserve, clear, memory usage)
- [x] Unit tests for MeshBuilder (face culling, AO, vertex positions, normals, UVs)
- [x] Stress tests (full subchunk, checkerboard pattern)
- [ ] Render a static manually-placed world
- [ ] View-relative precision at large coordinates
- [ ] Frustum culling excludes off-screen chunks

---

## Phase 5: VK Integration - Mesh Optimization

*Performance improvements.*

### 5.1 Greedy Meshing
- [ ] Greedy mesh algorithm (merge coplanar faces)
- [ ] Per-face-direction processing
- [ ] Handle transparent blocks separately
- [ ] Handle off-grid displaced blocks (only elide faces with matching displacement)

### 5.2 Mesh Update Pipeline (4 Stages)

**Stage 1: Block Change Detection**
- [ ] `meshDirty_` flag per SubChunk (separate from persistence dirty)
- [ ] SubChunk.setBlock() automatically marks mesh dirty (O(1))
- [ ] Adjacent subchunk notifications (boundary face visibility changes)

**Stage 2: Priority Queue**
- [ ] `MeshRebuildQueue` - CoalescingQueue with priority
- [ ] Priority = f(distance to camera, in-frustum, time since dirty)
- [ ] Coalescing: multiple changes to same subchunk = one rebuild

**Stage 3: Mesh Worker Thread(s)**
- [ ] Thread pool for parallel mesh generation
- [ ] Generates CPU-side vertex/index arrays
- [ ] Priority ordering: near + visible first, stale eventually processed
- [ ] Stale mesh displayed while new mesh computes

**Stage 4: GPU Upload Queue (Main Thread)**
- [ ] FIFO queue (priority already handled in Stage 2)
- [ ] Coalescing by ChunkPos (latest wins)
- [ ] Throttled uploads per frame to prevent stalls
- [ ] Alternative: prepare staging buffers in worker, main thread only issues copy commands

### Testing
- [ ] Greedy meshing reduces vertex count significantly
- [ ] Nearby changes mesh faster than distant
- [ ] No visual pops on mesh updates

---

## Phase 6: VK Integration - LOD System

*Distance-based detail reduction.*

### 6.1 LOD Generation
- [ ] LOD levels (1x, 2x, 4x, 8x block grouping)
- [ ] Simplified mesh for each LOD
- [ ] Representative block selection for grouped blocks

### 6.2 LOD Selection
- [ ] Distance-based LOD switching
- [ ] Buffer zones (hysteresis) at LOD boundaries
- [ ] Smooth transitions

### 6.3 GPU Memory Management
- [ ] Lazy unloading of unused LOD meshes
- [ ] Only update visible LOD level on distant changes

### Testing
- [ ] Distant terrain renders at lower detail
- [ ] LOD transitions don't pop
- [ ] Memory usage stays bounded at large view distances

---

## Phase 7: Module System

*Game-agnostic plugin architecture.*

### 7.1 Module Loader
- [ ] `ModuleLoader` - Load .so/.dll shared objects
- [ ] `GameModule` interface (name, version, lifecycle)
- [ ] Entry point: `extern "C" GameModule* createModule()`

### 7.2 Registries
- [ ] `BlockRegistry` - Block type registration
- [ ] `EntityRegistry` - Entity type registration
- [ ] `ItemRegistry` - Item registration
- [ ] `CommandRegistry` - Custom command registration

### 7.3 Core Module
- [ ] Core game content as a module (not engine built-in)
- [ ] Demonstrates module API usage

### Testing
- [ ] Load test module, register content
- [ ] Module lifecycle (init, shutdown)
- [ ] Core module provides basic blocks

---

## Phase 8: Lighting System

*Block and sky lighting.*

### 8.1 Light Storage
- [ ] Separate lighting data from block data
- [ ] Block light (0-15) per block
- [ ] Sky light (0-15) per block

### 8.2 Light Propagation
- [ ] BFS propagation on block changes
- [ ] Sky light column propagation
- [ ] Light removal algorithm

### 8.3 Ambient Occlusion
- [ ] Per-vertex AO calculation
- [ ] Smooth AO across faces

### Testing
- [ ] Torches illuminate surroundings
- [ ] Shadows under overhangs
- [ ] Light updates on block changes

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
```

---

[Next: Open Questions](18-open-questions.md)
