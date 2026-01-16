# 1. Executive Summary

[Back to Index](INDEX.md)

---

## Goals

FineStructure Voxel (finevox) is a voxel game engine designed to support games similar to Minecraft, Roblox, and 7 Days to Die. The engine will provide:

- **Infinite worlds** with chunk-based streaming (full-height column loading)
- **Flexible block system** with rotation, custom meshes, per-block data, and text rendering
- **High-performance rendering** via Vulkan, greedy meshing, and multi-level LOD
- **Robust physics** with AABB collision and step-climbing
- **Batch operations API** for efficient bulk block changes (avoiding Minecraft's per-block overhead)
- **Scripting integration points** for game logic and command language
- **Multiplayer-ready design** (single-player first, but architected for networking)

## Non-Goals (Engine Level)

- Multiplayer networking implementation (design for it, don't implement yet)
- **Procedural terrain generation** (belongs in games built on finevox, not the engine)
- Mobile/console support
- VR support

## Technology Stack

| Component | Technology |
|-----------|------------|
| Graphics API | Vulkan via FineStructureVK |
| Windowing | GLFW (via FineStructureVK) |
| Math | GLM |
| Build System | CMake |
| Serialization | CBOR (RFC 8949) - see [Persistence](11-persistence.md) |
| Scripting | External project, integrated via interpreter hooks |

---

[Next: Prior Art Analysis](02-prior-art.md)
