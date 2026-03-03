# FineStructureVoxel

A voxel game engine built on FineStructureVK (Vulkan wrapper). Game content is loaded as modules (shared objects), keeping the engine game-agnostic.

## Current Status

**Phase 21 complete — 1684 tests passing** (1633 main + 51 script)

All engine systems implemented:
- World management (chunk columns, lifecycle, persistence)
- Rendering (greedy meshing, LOD 0-4, fluid rendering)
- Physics (AABB collision, raycasting, step-climbing)
- Lighting (sky + block light, smooth AO, day/night cycle)
- Block update system (UpdateScheduler, BlockHandler, tick types)
- World generation (noise, biomes, feature pipeline)
- Entity system (AI, pathfinding, skeletal animation, spawning)
- Fluid system (storage, flow simulation, physics, rendering, light)
- Audio (miniaudio, 3D spatialization, footsteps)
- Script integration (finescript with BlockContextProxy, native functions)
- UI (finegui MapRenderer + finescript-driven UI, in-game console)
- Game session & game thread (30 TPS, GameActions interface)

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Game Modules  (.so/.dll)                               │  ← Games built here
├─────────────────────────────────────────────────────────┤
│  finevox Engine  (5 shared libraries)                   │
│  ├── libfinevox (core) ── finevox::                     │
│  ├── libfinevox_worldgen  ── finevox::worldgen::        │
│  ├── libfinevox_render  ── finevox::render::            │
│  ├── libfinevox_audio  ── finevox::audio::              │
│  └── libfinevox_script  ── finevox::script::            │
├─────────────────────────────────────────────────────────┤
│  finegui / finescript / FineStructureVK                 │
├─────────────────────────────────────────────────────────┤
│  Vulkan / GLFW / GLM                                    │
└─────────────────────────────────────────────────────────┘
```

## Building

```bash
mkdir build && cd build
cmake ..
make -j8
./finevox_tests
./render_demo          # interactive demo (requires Vulkan)
./render_demo --worldgen  # with procedural world generation
```

## Documentation

See **[docs/INDEX.md](docs/INDEX.md)** for the full documentation index.

Quick links:
- [docs/STATUS.md](docs/STATUS.md) — current phase status and test counts
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — layer diagram and library structure
- [docs/PATTERNS.md](docs/PATTERNS.md) — code conventions and gotchas
- [docs/ROADMAP.md](docs/ROADMAP.md) — future planned work

Original design documents are archived in [old_docs/](old_docs/).

## License

TBD
