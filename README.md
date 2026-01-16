# FineStructureVoxel

A voxel game engine built on FineStructureVK (Vulkan wrapper).

## Overview

FineStructure Voxel (finevox) is a game-agnostic voxel engine. Games are loaded as modules (shared objects), making the engine itself independent of specific game content.

## Current Status

**Phase 0-1 Complete:**
- Position types (BlockPos, ChunkPos, ColumnPos)
- String interning for block type IDs
- Per-subchunk palette with variable bit-width storage
- SubChunk (16x16x16) and ChunkColumn structures
- World management with lifecycle states
- LRU cache for unloaded columns
- Batch operations with coalescing
- DataContainer with CBOR serialization

**261 tests passing**

## Building

```bash
mkdir build && cd build
cmake ..
make -j8
./finevox_tests
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Game Modules (loaded .so/.dll)                         │
├─────────────────────────────────────────────────────────┤
│  finevox Engine                                         │
│  ├── World (columns, subchunks, blocks)                 │
│  ├── Rendering (mesh gen, LOD, view-relative)           │
│  ├── Physics (collision, raycasting)                    │
│  ├── Persistence (CBOR, region files)                   │
│  └── Module Loader                                      │
├─────────────────────────────────────────────────────────┤
│  FineStructureVK                                        │
├─────────────────────────────────────────────────────────┤
│  Vulkan / GLFW / GLM                                    │
└─────────────────────────────────────────────────────────┘
```

## Documentation

See the `docs/` directory for detailed design documentation.

## License

TBD
