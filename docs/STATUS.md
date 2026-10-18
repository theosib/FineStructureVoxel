# FineStructureVoxel — Project Status

> **Single source of truth for what's built, what's in progress, and what's next.**
> Updated after each major phase completion.

---

## Current State

| Item | Value |
|------|-------|
| Last completed phase | **Phase R4** — `ContentPackLoader` Extraction, `sky.conf` & `GameScriptEngine` Wiring, `useBlock` Interaction Routing, and `render_demo.cpp` Simplification (completing Reclamation Phases R0–R4) |
| Test count | **2233** (headless+audio build) / **2274** (with Vulkan render tests), 100% green |
| In-progress | — |
| Libraries | 5 shared libraries (see Architecture) |

---

## Phase Completion Checklist

### Foundation (VK-Independent)

- [x] **Phase 0** — Core data structures: BlockCoord, ChunkPos, StringInterner, SubChunk, ChunkColumn, DataContainer, Queue/KeyedQueue/WakeSignal
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
- [x] **Phase 23** — Inventory UI & Crafting Integration (6 sub-phases):
  - [x] 23-A: Recipe data model — `Recipe`, `RecipeRegistry`, `RecipeLoader` (shaped/shapeless/smelting), `.recipe` file format, `CraftingHelper` with pattern matching
  - [x] 23-B: Inventory native functions — `InventoryBridge` with `inv_get/set/move/swap/size/count/type`, `build_inv_grid` grid builder, crafting natives (`craft_find/execute/recipes`)
  - [x] 23-C: Block/item icon widgets — `IconAtlas` (UV regions from block atlas), `IconRegion`, finegui `uv0/uv1` support for `ui.image`/`ui.image_button`, `item_icon` native, `IconLookup` callback
  - [x] 23-D: Inventory UI scripts — `inventory.fs` (4x9 bag + 2x2 crafting grid + output), `container.fs` (generic container template), `slot_widget.fsc` (reusable slot builder), open/close via `open_inventory` action
  - [x] 23-E: Recipe browser — `recipe_browser.fs` (scrollable recipe list), `build_recipe_list` native, ingredient display in `craft_recipes`, craft output preview in per-frame update
  - [x] 23-F: Workbench crafting station — `workbench.model` block definition, `workbench.fs` (3x3 crafting grid UI), right-click interaction opens workbench UI, `open_workbench` action
- [ ] **Phase 24** — Player Survival + Combat:
  - [x] 24-A: Entity system fixes & script wiring — MobEventHooks interface, AI goal parameterization (all magic numbers → param structs), EntityManager auto-configures AI presets on spawn, ScriptMobEventHooks adapter, hooks provider wiring, mob native functions (add\_goal, clear\_goals, apply\_impulse, get/set\_data, remove)
  - [x] 24-B: Player entity unification — AIDriver adapter pattern (BrainAIDriver, PlayerInputDriver), player is MobEntity with PlayerInputDriver, virtual isPlayerEntity(), landing detection (preLandingVelocityY), player.entity type definition, HUD bridge natives (mob\_is\_player, mob\_fall\_velocity, mob\_yaw/pitch, mob\_last\_attacker, mob\_time\_since\_damage)
  - [x] 24-C: AI acceleration primitives — `EntitySpatialIndex` (grid-based, cell=16), wired into EntityManager (insert/update/remove), spatial query natives (`entities_in_radius`, `entities_in_box`, `nearest_entity`), LOS natives (`mob_can_see`, `raycast_blocks`)
  - [x] 24-D: Combat bridge — `GameActions::attackEntity`, `attack_entity` native (damage + auto-knockback), `entity_in_cone` native (melee sweep), `entity_position`/`entity_health`/`entity_is_alive` cross-entity query natives, `EntityManager::getMob()`
  - [x] 24-E: Survival stats & HUD bridge — `PlayerStatsBridge` (`player_health`, `player_max_health`, `player_get_stat`, `player_set_stat`, `player_is_alive`, `player_position`), HUD overlay script (`hud.fs`), death screen script (`death_screen.fs`)
  - [x] 24-F: Entity animation audit — named animation states in EntityTypeDef (`anim:name: slot` in `.entity` files), `MobEntity::resolveAnimation(name)`/`playAnimation(name)`, `mob_play_animation`/`mob_resolve_animation` script natives
- [x] **Phase 26-B** — Particle Rendering: Vulkan billboard renderer
  closes the loop on 26-A. `ParticleAtlas`
  (`src/render/particle_atlas.cpp`) loads PNGs from
  `resources/textures/particles/` via stb_image, packs them into a single
  atlas texture at startup (grid-based, 32×32 cells with 2-px filter
  border — mirrors `BlockAtlas`), then calls `applyToRegistry(...)` to
  patch `ParticleTypeDef::atlasUV` so the simulator stamps correct UVs
  into `Particle::atlasUV` at spawn without any per-particle string
  lookup in the render path.  `ParticleRenderer`
  (`src/render/particle_renderer.cpp`) drains
  `ParticleSystem::snapshot()` each frame, sorts back-to-front by
  squared distance, builds 6 vertices per particle on the CPU (three
  billboard modes: camera-facing, velocity-stretched, axis-locked),
  uploads to a single `RawMesh` (in-place update with 2× capacity
  reserve, one draw call), and draws into `WorldRenderer::render`
  after the fluid pass — correctly blending particles over water
  surfaces.  Pipeline is alpha-blended with depth test on / depth
  write off, cull none; a 0.02 alpha cutoff in the fragment shader
  avoids edge artefacts on cut-out sprites.  Fog parameters (including
  underwater variants) are forwarded from `WorldRenderer` so particles
  fade into the distance consistently with terrain.  Shaders:
  `shaders/particle.vert` (pass-through transform; CPU already did the
  billboard math) + `shaders/particle.frag` (atlas sample × tint +
  distance fog).  Resources: 6 placeholder 28×28 PNGs in
  `resources/textures/particles/` (one per atlas region referenced by
  the shipped `.particle` files — generated with a small Pillow
  script; obvious temp art).  Tests (+11 total): `test_particle_uv_patch`
  (3 tests) verifies `setAtlasUV` → registry → simulator path end-to-end;
  `test_particle_renderer` (8 tests) validates `buildParticleQuad`
  vertex generation for all three billboard modes, view-relative
  camera math, UV propagation, tint interpolation, and CPU-side
  perf (500 particles × 60 iters = 0.21 ms/iter, ~1% of a 16.67 ms
  frame budget at 60 FPS).  See
  [systems/particles.md](systems/particles.md).
- [x] **Phase 26-A** — Particle System (engine side): CPU-simulated
  particles on the game thread. Data-driven policy via `.particle` /
  `.emitter` config files parsed into `ParticleTypeRegistry` /
  `EmitterPresetRegistry` (`src/core/particle_registry.cpp`). Runtime
  state in `Particle` / `ParticleEmitter`, simulated by `ParticleSystem`
  (`src/core/particle_system.cpp`) behind a `shared_mutex` so the
  renderer can copy snapshots without contention. `ParticleSubsystem`
  ticks at `TickPhase::LateTick` so attached emitters see post-physics
  positions. Event plumbing: `EVT_PARTICLE_SPAWN` / `_DESPAWN` / `_BURST`
  in `event_value.hpp`, handled in `game_session.cpp::executeCommand`;
  `GameActions::spawnParticleEmitter/particleBurst/despawnParticleEmitter`
  convenience methods; `ParticleBridge` registers finescript natives
  (`spawn_particle_emitter`, `particle_burst`, `active_particle_count`,
  etc.). Wired fire points: block-break reads `BlockType::particleEmitter()`
  (set from `.model particle:` field) or falls back to `finevox:block_break`;
  splash path in `EntityManager::physicsPass` tries
  `finevox:splash_<fluid>` then `finevox:fluid_splash`. Voxel collision
  sub-steps (0.25-block max per step) with kill-or-reflect policy.
  Global cap (default 8192) evicts oldest when exceeded. Profile: 8192
  particles × 30 ticks costs 0.37 ms/tick (~1% of 33 ms budget) so GPU
  compute is not warranted yet. Resources: 6 `.particle` + 8 `.emitter`
  starter files in `resources/{particles,emitters}/`; luminite block
  overrides with `particle: finevox:ember_burst`. Tests: +17 in
  `test_particle_system.cpp` (parsing, registry lookup, burst counts,
  velocity distribution, lifetime expiry, cap eviction, continuous-
  emitter spawn rate, despawn-stops-spawn, collide-kills, reflect-
  preserves, air passes through, and event-value round-trips for spawn/
  despawn/burst); plus a profile smoke in `test_particle_perf.cpp`.
  **Deferred to 26-B**: Vulkan billboard renderer (ParticleAtlas +
  particle.vert/frag + integration into renderTranslucent) — kept out of
  this phase because finevk currently exposes only `GraphicsPipeline`
  and building out compute / new pipeline plumbing is its own project.
  Footstep-dust firing deferred (requires cross-library coupling or an
  event-queue refactor). See [systems/particles.md](systems/particles.md).
- [x] **Phase 25** — Block-Drop & Loot Pipeline:
  - Default break path rolls `BlockType::lootTable()` in `executeCommand` (`src/core/game_session.cpp`) with `LootContext{brokenBlock, position, toolUsed, preciseBreak}` sourced from the `block_broken` event Value; each resulting `ItemStack` spawns an `ItemDropEntity` with small randomized outward velocity
  - `ItemDropEntity::tick` now queries `EntitySpatialIndex` for nearby players (local player fast path + spatial scan fallback), performs pickup inside `pickupRadius_` (1.25m) via `EntityManager::tryDeliverItemToPlayer`, and applies magnetism inside `attractRadius_` (3m) — clamped to `maxAttractSpeed_`
  - `EntityManager::setItemPickupCallback` lets outer layers (render_demo, tests) plug in inventory policy without the engine owning storage; on spawn, `EntityManager` wires itself onto any `ItemDropEntity` (same pattern as `MobEntity::setEntityManager`)
  - Event-value additions: `makeBlockBrokenValue(..., tool, preciseBreak)` with `tool_type` + `precise_break` symbol fields and `readItemTypeId` helper
  - Data: `.loot` files for dirt, grass (precise→grass, otherwise dirt), cobble, luminite (precise-only; non-precise path TODO when a "glow dust" item exists), workbench, slab, stairs, wedge, stone_slab; stone.loot updated to finevox:-prefixed items; every block `.model` gained `loot: <name>`; `resources/tags/tools.tag` defines `finevox:tool.{pickaxe,axe,shovel,precise}`
  - Render_demo boots `LootTableLoader::loadDirectory(resources/loot)`, walks `resources/tags/*.tag` into `TagRegistry::global()` + `rebuild()`, applies `model.loot()` during block registration, and installs a pickup callback that deposits into the player's bag `DataContainer`
  - Tests (+13 in `test_block_drop_pipeline.cpp`): loot-table rolling under precise/default, break→drop integration, air/unlooted-block paths, precise event routing, pickup with room/delay/full-inventory, magnetism distance + speed clamp, no-callback lingering
- [x] **Phase R0** — Critical Bug, Correctness, and Scaffolding Wiring Sweep:
  - S3a invariants (`Rotation::index()` loud failure on non-member matrices; `RegionFile::assertNoFreeSpanOverlapsLiveExtent()` debug invariant + seeded random op-sequence test)
  - `ScriptBlockHandler` `ud->blockCtx` wiring and lazy `DataContainer` allocation on write; `DataContainerProxy` bool/array/nested-map support
  - `BlockContext::requestMeshRebuild()` / `markDirty()` via `SubChunk::bumpVersion()`; `notifyNeighbors()` routed through `UpdateScheduler::outbox()`
  - Lock-free `SubChunk` fluid layer retention and `SubChunk::isEmpty()` fluid check; `ColumnSerializer::fromCBOR` direct move without duplicate parser
  - `ConfigParser::parseLine` colon-splitting fix for namespaced values; `StringInterner` ID 0 (`""`) semantics + transparent `std::string_view` lookup
  - `Queue::setAlarm` / `KeyedQueue::setAlarm` earliest-deadline fix + `KeyedQueue` priority ordering
  - `LightProvider` wired into `LightEngine` BFS propagation; `LightingQueue::enqueue` coalescing fix
  - `AudioEngine::cleanupFinished()` UAF fix + macOS build fix; entity physics grounded `velocity.y` reset, radian angle standardization, `AIDriver` tick/event wiring, `ChaseGoal` pathfinding, `ItemDropEntity` serialization, and `GameSessionConfig` wiring
- [x] **Phase R2** — Queue Consolidation, Persistence & EventBus Wiring, and Documentation Reconciliation:
  - Created [ARCHIVED_CODE.md](ARCHIVED_CODE.md) recording git commit SHA `09cbcb9` and restoration commands for all retired headers in `include/finevox/core/deprecated/` (`alarm_queue.hpp`, `blocking_queue.hpp`, `coalescing_queue.hpp`, `simple_queue.hpp`) and `tests/test_blocking_queue.cpp`
  - Upgraded `Queue<T>` and `KeyedQueue<K, D>` to full parity with `finenet` (bounded capacity, `isFull()`, `pushBlocking()`, `PriorityFn`, `MergeFn`, `resetAlarm()`, `alarmTime()`)
  - Migrated `ColumnManager::saveQueue_`, `IOManager` load/save queues, and `LightEngine::LightingQueue` onto `Queue` / `KeyedQueue`
  - Fixed `IOManager::evictOldestRegion()` to use true LRU access tracking and `IOManager::flush()` to wait for in-flight saves and callbacks
  - Wired `ColumnManager` and `IOManager` into `World` and `GameSession` (including `EntityManager::saveColumnEntities` / `loadColumnEntities` callbacks) and added bulk `World::setBlocks` subchunk-grouped mutation
  - Wired `GameSession::executeCommand` to publish typed events (`BlockBrokenEvent`, `BlockPlacedEvent`, `FluidPlacedEvent`, `FluidRemovedEvent`, `PlayerUseEvent`, `PlayerHitEvent`, `PlayerPositionEvent`, `PlayerLookEvent`, `PlayerJumpEvent`, `PlayerSprintEvent`, `PlayerSneakEvent`, `SetWorldTimeEvent`, `CraftItemEvent`) onto `session.eventBus()` and implemented `EVT_CRAFT_ITEM` via `RecipeRegistry` + `CraftingHelper`
- [x] **Phase R3** — `finenet` In-Process Transport & Universal Smart-Client Boundary:
  - Implemented `finenet` core (`MessageBuilder`, `MessageReader`, `Channel`, `Connection::createLocalPair()` with optional wire-serialization debug mode) and wired a `finenet::Connection` local pair inside `GameSession::createLocal()` (`clientConnection()` / `serverConnection()`)
  - Added acting `EntityId actor` and `Rotation` propagation across `GameActions`, `World::placeBlock` / `breakBlock`, `UpdateScheduler`, `BlockContext::actor()`, `BlockContextProxy` (`:actor`), and `EventBus` events
  - Fixed dual-physics player prediction divergence in `EntityManager::tick()` (authoritative player entity is driven by client position updates rather than double-stepped by `physicsPass`)
  - Included `EntityTypeId` (`entity_type_id`) in `EVT_ENTITY_SPAWN` and wired `EntityRenderer` onto `session->graphicsEvents()` in `render_demo.cpp`
  - Wired `ctx.set_block` and `world.set_block` script natives to notify `LightEngine` and `FluidTickManager`
- [x] **Phase R4** — `ContentPackLoader` Extraction, `sky.conf` & `GameScriptEngine` Wiring, and `render_demo.cpp` Simplification:
  - Created `finevox::ContentPackLoader` (`include/finevox/core/content_pack_loader.hpp`, `src/core/content_pack_loader.cpp`) in `libfinevox` core to encapsulate loading `blocks/*.model` (with `shapes/*.geom`, `shapes/*.collision`, and `blocks/base/*.model` includes), `fluids/*.fluid`, `recipes/*.recipe`, `loot/*.loot`, `particles/*.particle`, `emitters/*.emitter`, `entities/*.entity`, `tags/*.tag` (with `UnificationRegistry` auto-resolution), `sounds/*.sound` (`SoundLoader` now compiled into `libfinevox` core), `lang/*.lang`, and `sky.conf` (`SkyConfig::fromFile`)
  - Registered both bare (`"stone"`) and `"finevox:"`-prefixed (`"finevox:stone"`) `BlockTypeId`s and custom `BlockGeometry` entries so worldgen, loot tables, and recipes resolve consistently, and exposed `geometryProvider()` and `faceOccludesProvider(world)` callbacks for `WorldRenderer`
  - Wired `GameScriptEngine::loadBlockScript` to register external `BlockHandler`s on `BlockRegistry::global()` (and unregister on `~GameScriptEngine()`), added `resources/scripts/workbench.fsc` (`script: scripts/workbench.fsc` in `workbench.model`), and instantiated `GameScriptEngine` in `render_demo.cpp`
  - Routed workbench and interactive block right-clicks in `render_demo.cpp` through `session->actions().useBlock(pos, face, playerId)` and `EventBus` `PlayerUseEvent` instead of render-thread interception, and wired `computeSkyParameters(timeOfDay, contentPack.skyConfig())` to use `resources/sky.conf`

---

## Deferred Items (From Completed Phases)

From **Phase 9**:
- ~~Scheduled tick persistence~~ ✓ — TickJournal persists ticks per-column; IOManager/ColumnManager wired
- ~~`UpdatePropagationPolicy`~~ ✓ — Per-block-type PropagationPolicy enum (Drop/Defer) in notifyNeighbors()
- Network quiescence protocol for connected blocks

From **Phase 2**:
- ~~LZ4 compression for chunk data~~ ✓ — implemented in `src/core/region_file.cpp` (`ChunkFlags::COMPRESSED_LZ4`, encode in `saveColumnRaw`, decode in `loadColumn`, honours `ConfigManager::compressionEnabled()`); 4 tests in `tests/test_region_file.cpp`
- Region file corruption recovery

From **Phase 10**:
- ~~Schematic file I/O~~ ✓ — implemented in `src/worldgen/schematic_io.cpp`: `serializeSchematic`/`deserializeSchematic`/`saveSchematic`/`loadSchematic`; format is 4-byte magic `"VXSC"` + 4-byte compressed size + LZ4-compressed CBOR; tests in `tests/test_schematic.cpp`

Known design issue:
- Native finescript functions crossing contexts is awkward (show_main_menu etc.) — should revisit with message-passing or state-machine approach

---

## Next Planned Work

See [ROADMAP.md](ROADMAP.md) for full roadmap. Near-term candidates:

- **Phase 27 — Structure template system + puzzle-block stitcher** (pending planner confirmation)
- **Phase 23 polish (remaining)** — config-driven input bindings, icon-based slots, container UI (see ROADMAP.md deferred items)
- **Particle polish (minor)** — footstep-dust cross-library wiring, luminite continuous-flame handler, real art to replace placeholder PNGs (see `docs/systems/particles.md` deferred list)

---

## Library Contents Summary

| Library | Namespace | Major Contents |
|---------|-----------|----------------|
| `libfinevox` | `finevox::` | World, SubChunk, BlockType, ContentPackLoader, Physics, Persistence, Events, Items, Tags, GameSession (finenet local transport), Fluids, Particles, SoundLoader |
| `libfinevox_worldgen` | `finevox::worldgen::` | Noise, BiomeRegistry, FeatureRegistry, GenerationPipeline, Schematics |
| `libfinevox_render` | `finevox::render::` | WorldRenderer, MeshWorkerPool, SubChunkView, BlockAtlas, LOD, ParticleAtlas, ParticleRenderer |
| `libfinevox_audio` | `finevox::audio::` | AudioEngine (miniaudio), SoundLoader, FootstepTracker |
| `libfinevox_script` | `finevox::script::` | GameScriptEngine, ScriptBlockHandler, BlockContextProxy, DataContainerProxy, EventSymbols, event\_value builders, InventoryBridge, PlayerStatsBridge, ParticleBridge |
