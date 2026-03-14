# FineStructureVoxel — Project Status

> **Single source of truth for what's built, what's in progress, and what's next.**
> Updated after each major phase completion.

---

## Current State

| Item | Value |
|------|-------|
| Last completed phase | Phase 22 — Flexibility Initiative (7 sub-phases) |
| Test count | **1971** |
| In-progress | None — see Roadmap for next steps |
| Libraries | 5 shared libraries (see Architecture) |

---

## Phase Completion Checklist

### Foundation (VK-Independent)

- [x] **Phase 0** — Core data structures: BlockCoord, ChunkPos, StringInterner, SubChunk, ChunkColumn, DataContainer, AlarmQueue
- [x] **Phase 1** — World management: World class, ColumnManager lifecycle, LRU cache, BatchBuilder
- [x] **Phase 2** — Persistence: CBOR encoder/decoder, region files, IOManager save/load threads, ConfigManager, ResourceLocator
- [x] **Phase 3** — Physics: AABB, CollisionShape, PhysicsSystem, raycasting (DDA), step-climbing

### Rendering (VK-Dependent)

- [x] **Phase 4** — Basic rendering: ChunkVertex, MeshBuilder (face culling + AO), SubChunkView, WorldRenderer, view-relative coords
- [x] **Phase 5** — Mesh optimization: greedy meshing, MeshWorkerPool, push-based rebuild pipeline, transparent mesh separation
- [x] **Phase 6** — LOD system: LOD0-4 levels, distance-based selection with hysteresis, GPU memory budget, fog system

### Engine Systems

- [x] **Phase 7** — Module system: ModuleLoader (dlopen/LoadLibrary), GameModule interface, BlockHandler system, BlockRegistry/EntityRegistry/ItemRegistry
- [x] **Phase 8** — Lighting: LightEngine (BFS), sky + block light (0-15), heightmap, per-vertex smooth lighting in mesh
- [x] **Phase 9** — Block updates: UpdateScheduler, three-queue architecture (outbox/tick/alarm), scheduled ticks, random ticks, force-loaders
- [x] **Phase 10** — World generation: noise library (Perlin/Simplex/Voronoi/FBM/Ridged/DomainWarp), BiomeRegistry, FeatureRegistry, GenerationPipeline, schematic/clipboard
- [x] **Phase 11/12** — Player controller + input: PlayerController (fly + physics mode), mouse look, WASD, KeyBindings persistence
- [x] **Phase 13** — Inventory + items: ItemTypeId, ItemStack, InventoryView, NameRegistry (stable persistence IDs), ItemRegistry, ItemDropEntity
- [x] **Phase 14** — Tags + unification: TagId/TagRegistry with transitive closure, UnificationRegistry (cross-mod item equivalence), ItemMatch predicate, .tag file format

### Game Features

- [x] **Phase 15** — Sky + day/night: WorldTime (36000 ticks/day, 30 TPS), SkyParameters, sky/block light split in ChunkVertex, sun direction + sky brightness push constants
- [x] **Phase 16** — Audio: SoundRegistry/SoundEvent/SoundEventQueue in core, AudioEngine (miniaudio), SoundLoader, FootstepTracker, 3D spatialization
- [x] **Phase 17** — Script integration: finescript via shared StringInterner, GameScriptEngine, ScriptBlockHandler, BlockContextProxy, DataContainerProxy, ScriptCache, .model `script:` field
- [x] **Phase 18** — Game session + game thread: GameSession, GameActions/LocalGameActions, EntityState, game thread at 30 TPS, WorldTime atomic ticks
- [x] **Phase 19** — UI: finegui MapRenderer + finescript-driven UI, debug overlay, settings screen, in-game console (backtick), command history, SetWorldTime command
- [x] **Phase 20** — Entity system: MobEntity, AIBrain (priority goals: Idle/Wander/Chase/Attack/Flee), A\* Pathfinder, skeletal animation with crossfade, EntityRenderer, SpawnManager, ScriptEntityHandler, CBOR persistence
- [x] **Phase 21** — Fluid system (6 sub-phases):
  - [x] 21-1: Core types + storage (FluidTypeId, FluidType, FluidRegistry, FluidLayer, FluidPalette, FluidCell)
  - [x] 21-2: Flow simulation (FluidSimulator: gravity→horizontal→slope→equalize→source→drain, FluidTickManager)
  - [x] 21-3: Fluid mesh + GPU pipeline (FluidMeshBuilder, fluid.frag, translucent render pass, underwater fog)
  - [x] 21-4: Light integration (fluid attenuation, logarithmic model, fluid emission, LightingUpdate extended)
  - [x] 21-5: Entity physics (FluidContactInfo, buoyancy, drag, flow force, EntityManager::physicsPass)
  - [x] 21-6: Polish + interactions (serialization, FluidInteractionRegistry, GameActions::placeFluid/removeFluid, FluidPass worldgen, script API, splash sound, isStaticSource optimization, EventJournal)
- [x] **Phase 22** — Flexibility Initiative (replace rigid C++ structs with flexible data on non-hot paths):
  - [x] 22-1: Event foundation — `EventSymbols` cache, `event_value.hpp` builder functions for all event types, finescript config builtins (`config_parse`/`config_encode`)
  - [x] 22-2: GameCommandQueue migration — `Queue<BlockEvent>` → `Queue<finescript::Value>`, `GameActions::sendAction(Value)`, `LocalGameActions` builds Value maps
  - [x] 22-3: GraphicsEvent/SoundEvent migration — `GraphicsMessage` wrapper (POD snapshots + Value events), `SoundEventQueue` → `Queue<Value>`, AudioEngine reads Values
  - [x] 22-4: EntityState extension — `std::unique_ptr<DataContainer> extra` field, CBOR serialization, deep-copy semantics
  - [x] 22-5: Config flexibilization — `GameSessionConfig`/`TickConfig`/`DistanceConfig` DataContainer serialization, data-driven `SkyConfig` with keyframes (`resources/sky.conf`)
  - [x] 22-6: Entity system extension — `EntityTypeDef::properties` DataContainer for mod-extensible attributes, `SpawnPredicateRegistry` for custom spawn conditions
  - [x] 22-7: UI scripting — hotbar migrated from hardcoded ImGui to `resources/ui/hotbar.fs`, `ScriptGuiManager::loadUIFromValue()` for server-sendable UI

---

## Deferred Items (From Completed Phases)

From **Phase 9**:
- ~~Scheduled tick persistence~~ ✓ — TickJournal persists ticks per-column; IOManager/ColumnManager wired
- ~~`UpdatePropagationPolicy`~~ ✓ — Per-block-type PropagationPolicy enum (Drop/Defer) in notifyNeighbors()
- Network quiescence protocol for connected blocks

From **Phase 2**:
- LZ4 compression for chunk data (infrastructure ready, not implemented)
- Region file corruption recovery

From **Phase 10**:
- Schematic file I/O (CBOR format designed, not yet wired to disk)

Known design issue:
- Native finescript functions crossing contexts is awkward (show_main_menu etc.) — should revisit with message-passing or state-machine approach

---

## Next Planned Work

> *TBD — see [ROADMAP.md](ROADMAP.md) for candidate next phases*

---

## Library Contents Summary

| Library | Namespace | Major Contents |
|---------|-----------|----------------|
| `libfinevox` | `finevox::` | World, SubChunk, BlockType, Physics, Persistence, Events, Items, Tags, GameSession, Fluids |
| `libfinevox_worldgen` | `finevox::worldgen::` | Noise, BiomeRegistry, FeatureRegistry, GenerationPipeline, Schematics |
| `libfinevox_render` | `finevox::render::` | WorldRenderer, MeshWorkerPool, SubChunkView, BlockAtlas, LOD |
| `libfinevox_audio` | `finevox::audio::` | AudioEngine (miniaudio), SoundLoader, FootstepTracker |
| `libfinevox_script` | `finevox::script::` | GameScriptEngine, ScriptBlockHandler, BlockContextProxy, DataContainerProxy, EventSymbols, event\_value builders |
