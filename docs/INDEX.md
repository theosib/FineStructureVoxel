# FineStructureVoxel — Documentation Index

> **Start here.** This is the navigation map for all project documentation.

---

## Quick Start for LLMs (New Session)

1. Read **[STATUS.md](STATUS.md)** — what's built, test count, deferred items, next steps
2. Read **[ARCHITECTURE.md](ARCHITECTURE.md)** — layer diagram, 5 libraries, directory layout
3. Read the relevant **[systems/](systems/)** file for the subsystem you're working on
4. Check **[PATTERNS.md](PATTERNS.md)** for conventions and gotchas

---

## Top-Level Docs

| File | What It Covers | Read When |
|------|---------------|-----------|
| [STATUS.md](STATUS.md) | Phase completion checklist, test counts, deferred items, next planned work | Starting any session |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Layer diagram, 5 libraries, namespaces, external deps, tech decisions | Understanding overall structure |
| [PATTERNS.md](PATTERNS.md) | Code conventions, common patterns, recurring gotchas | Writing or reviewing code |
| [ROADMAP.md](ROADMAP.md) | Future phases not yet started, deferred design decisions | Planning next work |

---

## System Reference Docs

| File | System | Library | Read When Working On |
|------|--------|---------|---------------------|
| [systems/world.md](systems/world.md) | Block data, chunks, world | `finevox` | Blocks, chunk lifecycle, World API |
| [systems/persistence.md](systems/persistence.md) | CBOR, region files, IO | `finevox` | Save/load, DataContainer, ResourceLocator |
| [systems/rendering.md](systems/rendering.md) | WorldRenderer, mesh gen, LOD | `finevox_render` | Rendering, mesh pipeline, fluids rendering |
| [systems/lighting.md](systems/lighting.md) | LightEngine, sky/block light, AO | `finevox` | Light propagation, vertex lighting |
| [systems/physics.md](systems/physics.md) | AABB, collision, raycasting | `finevox` | Entity movement, block raycasting |
| [systems/events.md](systems/events.md) | UpdateScheduler, BlockHandler, event flow | `finevox` | Block behavior, game tick logic |
| [systems/worldgen.md](systems/worldgen.md) | Noise, biomes, generation pipeline | `finevox_worldgen` | World generation, biome/feature system |
| [systems/entities.md](systems/entities.md) | Entity, MobEntity, AI, animation | `finevox` | Entity behavior, spawning, AI goals |
| [systems/fluids.md](systems/fluids.md) | FluidType, simulation, mesh, light | `finevox` | Fluid behavior, rendering, physics |
| [systems/audio.md](systems/audio.md) | AudioEngine, SoundRegistry | `finevox_audio` | Sound events, 3D audio, footsteps |
| [systems/scripting.md](systems/scripting.md) | GameScriptEngine, proxies, native fns | `finevox_script` | Script integration, .fsc blocks/entities |
| [systems/ui.md](systems/ui.md) | finegui, MapRenderer, console | apps | UI layout, in-game console, settings |
| [systems/game-session.md](systems/game-session.md) | GameSession, GameActions, game thread | `finevox` | Session lifecycle, command routing |

---

## Archive

All original documentation (pre-reorganization) is preserved in [old_docs/](../old_docs/).

Notable old docs for deep reference:
- [old_docs/25-entity-system.md](../old_docs/25-entity-system.md) — Detailed entity design (1685 lines)
- [old_docs/24-event-system.md](../old_docs/24-event-system.md) — Event system design (892 lines)
- [old_docs/26-network-protocol.md](../old_docs/26-network-protocol.md) — Network protocol design (1280 lines)
- [old_docs/27-world-generation.md](../old_docs/27-world-generation.md) — World generation design (516 lines)
- [old_docs/finegui-design.md](../old_docs/finegui-design.md) — finegui toolkit design (1047 lines)
- [old_docs/PLAN-network-layer.md](../old_docs/PLAN-network-layer.md) — Network transport spec (1540 lines)
- [old_docs/17-implementation-phases.md](../old_docs/17-implementation-phases.md) — Original phase breakdown
- [old_docs/AI-NOTES.md](../old_docs/AI-NOTES.md) — Previous AI session notes
