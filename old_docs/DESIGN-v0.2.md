# FineStructure Voxel Engine - Design Document

**Version:** 0.2 (Draft)
**Target Platform:** Vulkan via FineStructureVK
**Language:** C++17/20
**Namespace:** `finevox`

---

## Overview

FineStructure Voxel (finevox) is a voxel game engine designed to support games similar to Minecraft, Roblox, and 7 Days to Die. The engine provides:

- **Infinite worlds** with chunk-based streaming (full-height column loading)
- **Flexible block system** with rotation, custom meshes, per-block data, and text rendering
- **High-performance rendering** via Vulkan, greedy meshing, and multi-level LOD
- **Robust physics** with AABB collision and step-climbing
- **Batch operations API** for efficient bulk block changes
- **Scripting integration points** for game logic and command language
- **Multiplayer-ready design** (single-player first, but architected for networking)

---

## Documentation

The full design documentation has been split into focused sections for easier navigation. See the [docs/INDEX.md](docs/INDEX.md) for the complete table of contents.

### Quick Links

| Topic | Document |
|-------|----------|
| Goals & Tech Stack | [docs/01-executive-summary.md](docs/01-executive-summary.md) |
| Prior Art Analysis | [docs/02-prior-art.md](docs/02-prior-art.md) |
| Architecture | [docs/03-architecture.md](docs/03-architecture.md) |
| Core Data Structures | [docs/04-core-data-structures.md](docs/04-core-data-structures.md) |
| World Management | [docs/05-world-management.md](docs/05-world-management.md) |
| Rendering System | [docs/06-rendering.md](docs/06-rendering.md) |
| Level of Detail (LOD) | [docs/07-lod.md](docs/07-lod.md) |
| Physics & Collision | [docs/08-physics.md](docs/08-physics.md) |
| Lighting System | [docs/09-lighting.md](docs/09-lighting.md) |
| Input & Player Control | [docs/10-input.md](docs/10-input.md) |
| Persistence (CBOR) | [docs/11-persistence.md](docs/11-persistence.md) |
| Scripting & Commands | [docs/12-scripting.md](docs/12-scripting.md) |
| Batch Operations API | [docs/13-batch-operations.md](docs/13-batch-operations.md) |
| Threading Model | [docs/14-threading.md](docs/14-threading.md) |
| FineStructureVK Integration | [docs/15-finestructurevk-integration.md](docs/15-finestructurevk-integration.md) |
| FineStructureVK Critique | [docs/16-finestructurevk-critique.md](docs/16-finestructurevk-critique.md) |
| Implementation Phases | [docs/17-implementation-phases.md](docs/17-implementation-phases.md) |
| Open Questions | [docs/18-open-questions.md](docs/18-open-questions.md) |

---

## Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Block Registry | Global uint16_t IDs | Simpler than per-chunk, 65K types sufficient |
| Loading Unit | Full-height columns | Avoids Y-level loading issues |
| Serialization | CBOR (RFC 8949) | Self-describing, standardized, compact |
| Chunk Unloading | shared_ptr | Safe concurrent access without timed queues |
| Rendering | View-relative | Solves float precision at large coordinates |
| Mesh Generation | Greedy meshing | Significant vertex count reduction |

---

## Technology Stack

| Component | Technology |
|-----------|------------|
| Graphics API | Vulkan via FineStructureVK |
| Windowing | GLFW (via FineStructureVK) |
| Math | GLM |
| Build System | CMake |
| Serialization | CBOR (RFC 8949) |
| Scripting | External project, integrated via interpreter hooks |

---

## Related Projects

- **FineStructureVK** - Vulkan graphics library
- **VoxelGame2** - Prior C++/OpenGL implementation (analyzed for lessons learned)
- **EigenVoxel** - Prior Scala/Java implementation (analyzed for lessons learned)

---

*Last Updated: 2025-01-11*
*Author: Generated from analysis of voxelgame2, EigenVoxel, and FineStructureVK*
