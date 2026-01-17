# AI Session Notes - FineStructure Voxel Engine

**Purpose:** This file preserves context across conversation compactions. Read this first when resuming work.

---

## Project Overview

**FineStructure Voxel (finevox)** - A voxel game engine built on FineStructureVK (Vulkan wrapper).

**Key Principle:** The engine is game-agnostic. Games are loaded modules (shared objects). Even "core" game content is a module.

---

## Critical Design Decisions (Settled)

| Decision | Choice |
|----------|--------|
| Namespace | `finevox` |
| Block IDs | Per-subchunk palette + global string interning (unlimited types, compressible) |
| Loading unit | Full-height columns (16x16xHeight), but subchunk granularity for lifecycle |
| Serialization | CBOR (RFC 8949) |
| Chunk lifecycle | Active → SaveQueue → Saving → UnloadQueue → Evicted (with LRU caching) |
| Rendering | View-relative (solves float precision), greedy meshing, multi-LOD |
| Mesh updates | Priority by distance/visibility; lazy for distant changes |
| GPU memory | Buffer zones at LOD boundaries, lazy unloading |
| Collision vs Hit | Separate boxes (physics vs raycasting) |
| Mod system | Shared object (.so/.dll) loading; all game content is modules |
| Block displacement | Optional offset [-1,1] per axis; face elision only with identical displacement |
| Bit-packing | Word-aligned only (no straddling); better compression, simpler code |

---

## Key Files to Read

| Topic | File |
|-------|------|
| Full index | [INDEX.md](INDEX.md) |
| Core types (BlockPos, Block, SubChunk) | [04-core-data-structures.md](04-core-data-structures.md) |
| Subchunk lifecycle & caching | [05-world-management.md](05-world-management.md) Section 5.4 |
| Collision/Hit box distinction | [08-physics.md](08-physics.md) Section 8.2 |
| Batch operations pattern | [13-batch-operations.md](13-batch-operations.md) |
| Command language syntax | [12-scripting.md](12-scripting.md) Section 12.3 |
| Multiplayer architecture | [18-open-questions.md](18-open-questions.md) |
| Implementation phases | [17-implementation-phases.md](17-implementation-phases.md) |
| FineStructureVK critique | [16-finestructurevk-critique.md](16-finestructurevk-critique.md) |

---

## Layer Architecture (Bottom-Up)

```
┌─────────────────────────────────────────────────────────┐
│  Game Modules (loaded .so/.dll)                         │  <- Games built here
├─────────────────────────────────────────────────────────┤
│  finevox Engine                                         │  <- This project
│  ├── World (columns, subchunks, blocks)                 │
│  ├── Rendering (mesh gen, LOD, view-relative)           │
│  ├── Physics (collision, raycasting)                    │
│  ├── Persistence (CBOR, region files)                   │
│  └── Module Loader                                      │
├─────────────────────────────────────────────────────────┤
│  FineStructureVK                                        │  <- Vulkan wrapper
│  ├── Device, Swapchain, Pipelines                       │
│  ├── Buffer/Image management                            │
│  ├── GameLoop, InputManager                             │
│  └── (needs: InputEvent abstraction)                    │
├─────────────────────────────────────────────────────────┤
│  Vulkan / GLFW / GLM                                    │  <- System
└─────────────────────────────────────────────────────────┘
```

---

## What's in FineStructureVK (Don't Duplicate)

Based on analysis (see [16-finestructurevk-critique.md](16-finestructurevk-critique.md)):

**Already in FineStructureVK:**
- Vulkan device/instance/swapchain
- Pipeline creation
- Buffer/image management
- Command buffer recording
- GameLoop with timing
- Basic camera
- Texture atlas support
- Frustum culling basics

**Needs to be added to FineStructureVK (not finevox):**
- InputEvent abstraction (generic input events)
- Possibly: indirect draw support

**finevox provides (not in FineStructureVK):**
- World/chunk/block data model
- Mesh generation algorithms
- Physics system
- Persistence/serialization
- Game module loading

---

## Implementation Phase Order (Bottom-Up)

### Phase 0: VK-Independent Foundation
*No graphics, pure data structures*
- BlockPos, ChunkPos, ColumnPos
- StringInterner, BlockTypeId
- SubChunkPalette
- Block, SubChunk, ChunkColumn structures
- DataContainer (CBOR serialization)
- CoalescingQueue utility
- **Test:** Unit tests for all data structures

### Phase 1: World Management (VK-Independent)
- World class (in-memory only)
- SubChunkManager (lifecycle, caching, queues)
- Column loading/unloading logic
- Batch operations (BatchBuilder)
- **Test:** Create world, place/get blocks, lifecycle transitions

### Phase 2: Persistence (VK-Independent)
- Region file format
- Column serialization/deserialization
- Save/load threads
- **Test:** Round-trip save/load of world data

### Phase 3: Physics (VK-Independent)
- AABB, CollisionShape
- Collision detection
- Raycasting (collision vs hit modes)
- Step-climbing algorithm
- **Test:** Entity movement, collision resolution

### Phase 4: VK Integration - Basic Rendering
*First graphics code*
- SubChunkView (GPU mesh handle)
- Basic mesh generation (face culling, no greedy)
- View-relative rendering
- Single draw per subchunk
- **Test:** Render a static world

### Phase 5: VK Integration - Mesh Optimization
- Greedy meshing
- Mesh worker thread pool
- Priority-based mesh updates
- **Test:** Performance benchmarks

### Phase 6: VK Integration - LOD System
- LOD level generation
- LOD selection by distance
- GPU memory management (buffer zones)
- **Test:** Distant rendering, LOD transitions

### Phase 7: Module System
- ModuleLoader
- GameModule interface
- Registry exposure (blocks, entities, items)
- **Test:** Load a simple test module

### Phase 8: Lighting (TBD architecture)
- Block light propagation
- Sky light
- Ambient occlusion
- **Test:** Light updates on block changes

---

## Command Language Syntax Quick Reference

```
verb arg1 arg2          # Top-level command
X, Y, Z                 # Context variables (bare words)
(Y + 5)                 # Math expression (parentheses)
{getHeight X Z}         # Function call (braces)
items[0]                # Array indexing (brackets - future)
```

---

## Dirty Tracking: Two Independent Systems

**Persistence Dirty** (Phase 1, implemented):
- Tracks columns needing save to disk
- Managed by SubChunkManager lifecycle state machine
- Periodic saves (default 60s) prevent data loss for active columns

**Mesh Dirty** (Phase 5):
- Tracks subchunks needing mesh regeneration
- 4-stage pipeline: Detection → Priority Queue → Worker Threads → GPU Upload
- See [06-rendering.md](06-rendering.md) Section 6.5 for full pipeline diagram

---

## Current Work State

*Update this section when resuming work*

**Last completed:**
- Phase 0: All data structures implemented and tested
- Phase 1: World, SubChunkManager, LRUCache, BatchBuilder implemented and tested
- Added BlockDisplacement for off-grid block placement
- Documented mesh update pipeline (4 stages)
- Phase 2 (nearly complete):
  - DataContainer with interned keys and CBOR serialization
  - SubChunk serialization (toCBOR/fromCBOR)
  - ChunkColumn serialization (toCBOR/fromCBOR)
  - RegionFile with journal-style ToC (crash-safe)
  - Free space management for region files
  - 294 tests passing

**Next task:** LZ4 compression for region files, I/O threads for async save/load, or Phase 3 (Physics)
**Blockers:** None

---

## API Summary (Built Components)

*Update this section as phases complete*

```cpp
// Phase 0 APIs will go here after implementation
// Example format:
//
// struct BlockPos {
//     int32_t x, y, z;
//     uint64_t pack() const;
//     static BlockPos unpack(uint64_t);
//     int toLocalIndex() const;
//     BlockPos neighbor(Face) const;
// };
```

---

*Last updated: Session creating comprehensive design review*
