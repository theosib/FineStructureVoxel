# FineStructureVoxel

A voxel game engine built on FineStructureVK (Vulkan wrapper). Game content is loaded as modules (shared objects), keeping the engine game-agnostic.

## Current Status

See [docs/STATUS.md](docs/STATUS.md) for current state.

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

## Making a Game

A game is a content directory with a `game.conf`. It needs no C++:

```bash
finevox_run --game-root path/to/game       # play
finevox_headless --game-root path/to/game  # run with no window, for tests
```

See [docs/systems/game-runtime.md](docs/systems/game-runtime.md).

## Building

```bash
# Dependencies are siblings of this repo, each with a Release build in build/
cmake -S . -B ../fv-build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DFINEVOX_BUILD_RENDER=ON -DFINEVOX_BUILD_AUDIO=ON
cmake --build ../fv-build
../fv-build/finevox_tests         # run test binaries directly, not ctest -j
../fv-build/finevox_render_demo   # interactive demo (requires Vulkan)
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
