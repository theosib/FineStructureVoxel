# FineStructure Voxel Engine - Documentation Index

**Version:** 0.3 (Draft)
**Target Platform:** Vulkan via FineStructureVK
**Language:** C++17/20
**Namespace:** `finevox`

---

## For AI Assistants

**Start here:** [AI-NOTES.md](AI-NOTES.md) - Session memory with settled decisions, layer architecture, and current work state.

---

## Quick Start

| Goal | Document |
|------|----------|
| High-level overview | [01 - Executive Summary](01-executive-summary.md) |
| Architecture diagram | [03 - Architecture Overview](03-architecture.md) |
| Implementation plan | [17 - Implementation Phases](17-implementation-phases.md) |
| Settled vs open decisions | [18 - Open Questions](18-open-questions.md) |

---

## Documentation by Topic

### Core Data Model

| Document | Key Contents |
|----------|--------------|
| [04 - Core Data Structures](04-core-data-structures.md) | BlockPos, ChunkPos, Block, SubChunk, StringInterner, SubChunkPalette, pointer strategy |
| [05 - World Management](05-world-management.md) | ChunkColumn, World, SubChunkManager, lifecycle states, LRU caching |
| [13 - Batch Operations](13-batch-operations.md) | BatchBuilder, CoalescingQueue, collect-coalesce-execute pattern |

### Rendering

| Document | Key Contents |
|----------|--------------|
| [06 - Rendering System](06-rendering.md) | Mesh generation, view-relative rendering, shaders |
| [07 - Level of Detail](07-lod.md) | LOD levels, lazy updates, buffer zones |
| [22 - Phase 6 LOD Design](22-phase6-lod-design.md) | POP buffers, octree downsampling, boundary stitching |
| [09 - Lighting System](09-lighting.md) | Block light, sky light, ambient occlusion |

### Physics & Interaction

| Document | Key Contents |
|----------|--------------|
| [08 - Physics and Collision](08-physics.md) | AABB, CollisionShape, collision vs hit box, step-climbing, RaycastMode, entity persistence |
| [10 - Input and Player Control](10-input.md) | InputManager, PlayerController |
| [19 - Block Models](19-block-models.md) | Hierarchical model format, render/collision/hit shapes, model inheritance |
| [20 - Large World Coordinates](20-large-world-coordinates.md) | View-relative rendering, double-precision camera, precision at large distances |
| [21 - Clipboard and Schematic](21-clipboard-schematic.md) | BlockSnapshot, Schematic, copy/paste, CBOR serialization, ClipboardManager |

### Persistence & Scripting

| Document | Key Contents |
|----------|--------------|
| [11 - Persistence](11-persistence.md) | CBOR format, region files, chunk serialization |
| [12 - Scripting](12-scripting.md) | Command language syntax (`{}` for calls, `()` for math, `[]` for arrays) |

### Infrastructure

| Document | Key Contents |
|----------|--------------|
| [14 - Threading Model](14-threading.md) | Thread responsibilities, work queues |
| [15 - FineStructureVK Integration](15-finestructurevk-integration.md) | How we use FineStructureVK features |
| [16 - FineStructureVK Critique](16-finestructurevk-critique.md) | Missing features, what to add vs not duplicate |

### Systems & Configuration

| Document | Key Contents |
|----------|--------------|
| [23 - Distance and Loading](23-distance-and-loading.md) | Distance zones, chunk loading policies, block updates, fog, force-loading |
| [24 - Event System](24-event-system.md) | Inbox/outbox event processing, lighting thread, version tracking |

### Planning & Analysis

| Document | Key Contents |
|----------|--------------|
| [01 - Executive Summary](01-executive-summary.md) | Goals, non-goals, technology stack |
| [02 - Prior Art Analysis](02-prior-art.md) | Lessons from VoxelGame2 and EigenVoxel |
| [17 - Implementation Phases](17-implementation-phases.md) | Bottom-up phases, VK-independent vs VK-dependent |
| [18 - Open Questions](18-open-questions.md) | Resolved decisions and remaining questions |

### Appendices

| Document | Key Contents |
|----------|--------------|
| [Appendix A - File Structure](appendix-a-file-structure.md) | Project directory layout |
| [Appendix B - Key Differences](appendix-b-differences.md) | Comparison with prior implementations |

---

## Key Design Decisions (Settled)

| Decision | Choice | Document |
|----------|--------|----------|
| Namespace | `finevox` | All docs |
| Block Registry | Per-subchunk palette + global string interning | [04](04-core-data-structures.md) §4.3-4.4 |
| Loading Unit | Full-height columns, subchunk granularity for lifecycle | [05](05-world-management.md) |
| Serialization | CBOR (RFC 8949) | [11](11-persistence.md) |
| Chunk Lifecycle | Active → SaveQueue → Saving → UnloadQueue → Evicted, LRU cache | [05](05-world-management.md) §5.4 |
| Rendering | View-relative coordinates | [06](06-rendering.md), [20](20-large-world-coordinates.md) |
| Mesh Generation | Greedy meshing, priority by distance/visibility | [06](06-rendering.md), [18](18-open-questions.md) |
| Collision vs Hit Box | Separate shapes for physics vs interaction | [08](08-physics.md) §8.3 |
| Block Models | Hierarchical model files, data-driven collision/hit shapes | [19](19-block-models.md) |
| Mod System | Shared object loading; all game content is modules | [18](18-open-questions.md) |
| Command Syntax | `{}` for function calls, `()` for math, `[]` for arrays | [12](12-scripting.md) §12.3 |

---

## Cross-Reference: Where to Find...

| Topic | Location |
|-------|----------|
| Large world coordinates | [20](20-large-world-coordinates.md) |
| Block storage bit-width | [04](04-core-data-structures.md) §4.4 SubChunkPalette |
| Pointer usage strategy | [04](04-core-data-structures.md) §4.1 |
| Lifecycle state machine | [05](05-world-management.md) §5.4 |
| LRU cache behavior | [05](05-world-management.md) §5.4.2 |
| Save/unload queue separation | [05](05-world-management.md) §5.4.5-5.4.6 |
| CoalescingQueue utility | [13](13-batch-operations.md) §13.2 |
| Collision box vs hit box | [08](08-physics.md) §8.2 |
| RaycastMode enum | [08](08-physics.md) §8.2 |
| Step-climbing algorithm | [08](08-physics.md) §8.7 |
| Entity wall-glitch prevention | [08](08-physics.md) §8.4 |
| Soft vs hard collisions | [08](08-physics.md) §8.5 |
| Hierarchical model files | [19](19-block-models.md) §19.3 |
| Command language syntax | [12](12-scripting.md) §12.3 |
| ModuleLoader API | [18](18-open-questions.md) |
| Multiplayer architecture | [18](18-open-questions.md) |
| FineStructureVK overlap audit | [16](16-finestructurevk-critique.md), [AI-NOTES.md](AI-NOTES.md) |
| VK-independent phases | [17](17-implementation-phases.md) Phases 0-3 |
| VK-dependent phases | [17](17-implementation-phases.md) Phases 4-8 |
| Clipboard/schematic system | [21](21-clipboard-schematic.md) |
| LOD POP buffers | [22](22-phase6-lod-design.md) §2 |
| LOD octree downsampling | [22](22-phase6-lod-design.md) §3 |
| LOD boundary stitching | [22](22-phase6-lod-design.md) §4 |
| BlockSnapshot format | [21](21-clipboard-schematic.md) §21.3 |
| Schematic serialization | [21](21-clipboard-schematic.md) §21.6 |
| Distance zones | [23](23-distance-and-loading.md) §1 |
| Block update scheduling | [23](23-distance-and-loading.md) §3.1 |
| Cross-chunk updates | [23](23-distance-and-loading.md) §3.2 |
| Network quiescence | [23](23-distance-and-loading.md) §3.3 |
| Force-load mechanism | [23](23-distance-and-loading.md) §2.4 |
| Fog system | [23](23-distance-and-loading.md) §2.3 |
| Block extra data | [23](23-distance-and-loading.md) §2.5, [17](17-implementation-phases.md) §9.1 |
| SubChunk extra data | [23](23-distance-and-loading.md) §2.5, [17](17-implementation-phases.md) §9.1 |
| Column extra data | [23](23-distance-and-loading.md) §2.5, [17](17-implementation-phases.md) §9.1 |
| Event system inbox/outbox | [24](24-event-system.md) §24.2, §24.6 |
| Handler semantics (place/break) | [24](24-event-system.md) §24.7 |
| Lighting thread integration | [24](24-event-system.md) §24.8-24.11 |
| Version tracking (block/light) | [24](24-event-system.md) §24.9 |

---

## Implementation Phase Summary

| Phase | VK-Dependent? | Focus |
|-------|---------------|-------|
| 0 | No | Data structures (BlockPos, SubChunk, Palette, StringInterner) |
| 1 | No | World management (World, SubChunkManager, BatchBuilder) |
| 2 | No | Persistence (CBOR, region files, save/load threads) |
| 3 | No | Physics (AABB, collision, raycasting, step-climbing) |
| 4 | Yes | Basic rendering (mesh generation, view-relative, shaders) |
| 5 | Yes | Mesh optimization (greedy meshing, async workers, priority) |
| 6 | Yes | LOD system (generation, selection, GPU memory) |
| 7 | Mixed | Module system (loader, registries, core module) |
| 8 | Yes | Lighting (block light, sky light, AO) |

See [17 - Implementation Phases](17-implementation-phases.md) for full details.

---

## Related Projects

- **FineStructureVK** - Vulkan graphics library (provides device, pipelines, buffers, GameLoop, Camera)
- **VoxelGame2** - Prior C++/OpenGL implementation
- **EigenVoxel** - Prior Scala/Java implementation (step-climbing algorithm borrowed from here)

---

*Last Updated: 2026-01-24*
