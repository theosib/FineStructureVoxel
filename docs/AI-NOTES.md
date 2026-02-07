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
| Block model spec format | [19-block-models.md](19-block-models.md) |
| Event system / block handlers | [24-event-system.md](24-event-system.md) |
| Entity system design | [25-entity-system.md](25-entity-system.md) |
| Network protocol | [26-network-protocol.md](26-network-protocol.md) |
| Implementation phases | [17-implementation-phases.md](17-implementation-phases.md) |
| GUI toolkit design | [finegui-design.md](finegui-design.md) |
| Source ↔ Doc mapping | [SOURCE-DOC-MAPPING.md](SOURCE-DOC-MAPPING.md) |

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
│  ├── Block Models (.model/.geom/.collision spec files)  │
│  └── Module Loader                                      │
├─────────────────────────────────────────────────────────┤
│  finegui                                                │  <- GUI toolkit
│  └── Dear ImGui + finevk Vulkan backend                 │
├─────────────────────────────────────────────────────────┤
│  FineStructureVK                                        │  <- Vulkan wrapper
│  ├── Device, Swapchain, Pipelines                       │
│  ├── Buffer/Image management                            │
│  ├── GameLoop, InputManager, InputEvent                 │
│  └── Camera (double-precision), Overlay2D               │
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
- Camera with double-precision support (`positionD()`, `viewRelative`)
- InputManager and InputEvent abstraction
- Texture atlas support
- Frustum culling basics
- Overlay2D for simple HUD elements
- `frame.beginRenderPass()` / `frame.endRenderPass()` / `frame.extent` / `frame.frameIndex()`

**finevox provides (not in FineStructureVK):**
- World/chunk/block data model
- Mesh generation algorithms (greedy meshing, LOD, custom geometry)
- Block model system (.model/.geom/.collision spec files)
- Physics system
- Persistence/serialization
- Game module loading
- Event system (UpdateScheduler, block handlers)

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

### Phase 5: VK Integration - Mesh Optimization ✓
- Greedy meshing (subchunk-local)
- Mesh worker thread pool (MeshWorkerPool)
- Push-based mesh rebuild pipeline
- Custom mesh exclusion for non-cube blocks

### Phase 6: VK Integration - LOD System ✓
- LOD levels 0-4 (1x to 16x block grouping)
- Distance-based LOD selection with hysteresis
- GPU memory budget enforcement
- Fog system integration

### Phase 7: Module System ✓
- ModuleLoader with dependency resolution
- GameModule interface, BlockHandler system
- BlockRegistry, EntityRegistry, ItemRegistry

### Phase 8: Lighting System ✓
- LightEngine with BFS propagation
- Sky light and block light (0-15)
- Smooth per-vertex lighting in mesh builder

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
- Managed by ColumnManager lifecycle state machine
- Periodic saves (default 60s) prevent data loss for active columns

**Mesh Dirty** (Phase 5, implemented):
- Push-based: block/light changes push rebuild requests to mesh queue
- MeshWorkerPool processes rebuilds in parallel
- See [PLAN-mesh-architecture-improvements.md](PLAN-mesh-architecture-improvements.md) for data flow

---

## Current Work State

*Update this section when resuming work*

**All core phases complete (0-8).** Phase 9 (block updates) mostly complete.

**Recent work (since Phase 9):**
- Non-cube block model system (slabs, stairs, wedges) - complete
  - BlockModel, BlockGeometry, BlockModelLoader using ConfigParser format
  - .model/.geom/.collision spec files in `resources/`
  - Custom mesh rendering via `addCustomFace()` in MeshBuilder
  - `hasCustomMesh` flag for greedy mesh exclusion
- Entity system foundation (entity.hpp, entity_manager.hpp, graphics_event_queue.hpp)
- Queue primitives refactor (queue.hpp, simple_queue, coalescing_queue, keyed_queue)
- finevk API migration (frame.beginRenderPass, frame.extent, frame.frameIndex)
- finegui integration in render_demo (coordinates overlay + mock hotbar)

**Remaining Phase 9 work:**
- Scheduled tick persistence across save/load
- `UpdatePropagationPolicy` for cross-chunk updates
- Network quiescence protocol

**Next task:** TBD
**Blockers:** None

---

## Non-Cube Block System (Implemented)

**Complete implementation for slabs, stairs, wedges, etc.**

### Core Files
- `include/finevox/block_model.hpp` - FaceGeometry, BlockGeometry, BlockModel, RotationSet
- `include/finevox/block_model_loader.hpp` - Parser for .model/.geom/.collision files
- `src/mesh.cpp` - addCustomFace(), geometry provider integration

### Key Concepts
- **RotationSet**: Constrains which of 24 rotations are valid (None/Vertical/Horizontal/HorizontalFlip/All/Custom)
- **Fallback chain**: hit → collision → geometry faces → full block
- **Face naming**: Supports aliases (west/w/-x → 0, top/up/posy → 3) and numbers
- **Greedy mesh exclusion**: Custom geometry blocks skip greedy merging

### Spec Files (resources/)
- `shapes/slab_faces.geom`, `shapes/slab_collision.collision`
- `shapes/stairs_faces.geom`, `shapes/stairs_collision.collision`
- `shapes/wedge_faces.geom`, `shapes/wedge_collision.collision`
- `blocks/base/slab.model`, `blocks/base/stairs.model`, `blocks/base/wedge.model`

### Future: Smart Block Placement (Design Notes)

Context-aware placement like Hytale/Minecraft:
1. **Player facing** + **target surface** → suggested rotation
2. Constrain to block's `allowedRotations()`
3. **R-key cycling** through valid rotations with ghost preview
4. **Side placement** for vertical slabs (Hytale-style)
5. **Fine rotation** (16+ directions) - lower priority

See plan file: `.claude/plans/abundant-pondering-hollerith.md`

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

*Last updated: 2026-02-06 — Documentation audit and update*
