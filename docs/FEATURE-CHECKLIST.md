# Feature Checklist

Tracks what's built, what's partially done, and what's still needed for a playable game.

---

## 1. World Generation (Priority: CRITICAL) - COMPLETE

- [x] Noise library (Perlin, Simplex, Voronoi, FBM, Ridged, DomainWarp)
- [x] ChunkGenerator interface (pluggable via module system)
- [x] Terrain shaping (heightmap from noise octaves, cave carving)
- [x] Biome system (temperature/moisture maps driving block selection)
- [x] Structure generation (trees, ores, schematics)
- [x] Integration with ColumnManager for async generation on load
- [x] Data-driven config (.biome, .feature, .ore files)
- [x] 6-pass pipeline: Terrain → Surface → Cave → Ore → Structure → Decoration

## 2. Inventory & Item System (Priority: CRITICAL) - MOSTLY COMPLETE

- [x] Item data model (ItemStack: type + count + durability + metadata)
- [x] Inventory container (InventoryView: ephemeral lock-free adapter over DataContainer)
- [x] Item drops (ItemDropEntity with pickup delay, despawn timer)
- [x] ItemTypeId (interned wrapper, same pattern as BlockTypeId)
- [x] ItemRegistry with ItemType properties
- [x] NameRegistry (per-world stable name↔PersistentId for persistence)
- [ ] Tool properties (mining speed, durability, damage)
- [ ] Crafting system (recipe registry, shaped/shapeless/furnace recipes)
- [ ] Hotbar mechanics (tie into finegui hotbar)

## 3. Input System (Priority: CRITICAL) - FULLY COMPLETE

- [x] InputManager action mapping layer
- [x] Configurable key bindings (persisted via ConfigManager)
- [x] Action events (movement, jump, attack, use, etc.)
- [x] Context switching (gameplay vs menu vs chat input modes)

## 4. Player Controller (Priority: HIGH) - PARTIALLY COMPLETE

- [x] Basic physics mode (gravity, collision, step-climbing)
- [x] Fly mode with toggle
- [x] Mouse look, WASD movement, jump
- [x] Mode switching with keybindings
- [ ] Sprint/crouch/sneak (speed modifiers, hitbox changes)
- [ ] Swimming (buoyancy in fluid blocks)
- [ ] Fall damage (velocity-based health reduction)
- [ ] Health/hunger system (basic survival stats)
- [ ] Death/respawn (spawn point, inventory drop)

## 5. Entity AI & Behavior (Priority: HIGH) - NOT STARTED

- [x] Entity infrastructure (EntityManager, EntityRegistry, GraphicsEventQueue)
- [x] Entity base class with DataContainer for per-entity data
- [ ] Mob AI (pathfinding, target selection, attack patterns)
- [ ] Spawn system (light-level and biome-based spawning rules)
- [ ] Mob types (passive, hostile, ambient)
- [ ] Entity rendering (WorldRenderer has no entity draw path)
- [ ] Skeletal animation system (bone hierarchies, keyframe animation, blending)
- [ ] Entity models and model loading
- [ ] Loot tables (what mobs drop on death)
- [ ] Entity scripting (finescript handlers for mob behavior)

## 6. Fluid System (Priority: HIGH) - NOT STARTED

- [ ] Fluid blocks (water/lava with level 0-7 and flow direction)
- [ ] Flow simulation (BFS spread with tick-based updates via UpdateScheduler)
- [ ] Fluid rendering (animated translucent meshes with flow direction)
- [ ] Physics interaction (buoyancy, current pushing entities, fall damage cancellation)
- [ ] Light interaction (water absorbs light, lava emits it)

## 7. Sky & Day/Night Cycle (Priority: MEDIUM) - COMPLETE

- [x] WorldTime (tick-based, 24000 ticks/day, 20 tps)
- [x] SkyParameters (dynamic sky color gradients, fog color, sun arc)
- [x] Separate sky light and block light in ChunkVertex
- [x] Shader integration (sun direction, sky brightness multiplier)
- [x] render_demo controls (T key cycles time speed)
- [ ] Sky renderer (gradient dome or skybox geometry)
- [ ] Sun/moon visual objects
- [ ] Cloud layer (scrolling texture or volumetric)
- [ ] Fog integration with sky color blending (FogConfig exists, needs connection)

## 8. Audio System (Priority: MEDIUM) - COMPLETE

- [x] Audio backend (miniaudio)
- [x] Spatial audio (3D positioned sounds)
- [x] Sound events (triggered by block changes, footsteps)
- [x] Volume/mixing (master, effects, per-category channels)
- [x] SoundRegistry with data-driven .sound config files
- [x] FootstepTracker (distance-based, surface-aware)
- [x] Fire-and-forget sound cleanup (thread-safe)
- [ ] Music system (ambient tracks, biome-specific)
- [ ] Ambient sounds (weather, cave ambience, wildlife)

## 9. UI Framework Integration (Priority: MEDIUM) - PARTIALLY COMPLETE

- [x] finegui core integrated (GuiSystem, InputAdapter in render_demo)
- [x] Pause menu (resume, settings, quit)
- [ ] Inventory screen (drag-and-drop item management)
- [ ] Crafting UI (recipe grid with output preview)
- [ ] Chat/command bar (for scripting/commands)
- [ ] Settings screen (key bindings, render distance, audio)
- [ ] HUD (health, hunger, hotbar, crosshair)
- [ ] Script-driven UI via finegui-script (MapRenderer)

## 10. Scripting System (Priority: LOW-MEDIUM) - PARTIALLY COMPLETE

- [x] finescript integration (shared interner, ScriptEngine)
- [x] Block handler scripting (ScriptBlockHandler, on :event closures)
- [x] BlockContextProxy (read block state from scripts)
- [x] DataContainerProxy (per-block persistent data from scripts)
- [x] Native functions (ctx.*, world.*)
- [x] ScriptCache with hot-reload
- [x] .model file `script:` field
- [ ] In-game command console
- [ ] Entity/mob AI scripting
- [ ] User chat commands
- [ ] Script sandboxing / security restrictions
- [ ] Script error reporting UI

## 11. Multiplayer (Priority: LOW) - NOT STARTED

Design docs exist (25-entity-system.md, 26-network-protocol.md) but no implementation:

- [ ] Network protocol (thin client architecture)
- [ ] Player prediction (client-side movement with server correction)
- [ ] Entity interpolation (smooth remote entity movement)
- [ ] Chunk streaming (send world data to clients)
- [ ] Asset streaming (textures, sounds, scripts)
- [ ] Inventory networking (slot updates, prediction rollback)
- [ ] UI protocol (declarative UI definitions from server)

---

## Infrastructure & Polish

### Block Events (Phase 9) - Deferred Items

- [x] UpdateScheduler with three-queue architecture
- [x] Game tick, random tick, scheduled tick systems
- [x] Block handler dispatch
- [ ] Scheduled tick persistence across save/load
- [ ] UpdatePropagationPolicy for cross-chunk updates
- [ ] Network quiescence protocol

### Mesh Architecture Improvements

- [x] Push-based mesh rebuild pipeline
- [x] MeshWorkerPool for parallel mesh generation
- [ ] WakeSignal (multi-queue wait primitive with deadline support)
- [ ] Deadline-based graphics thread sleep (replace fixed polling)
- [ ] GUI thread separation

### Tags & Crafting

- [x] TagRegistry (composable tags with cycle detection)
- [x] UnificationRegistry (cross-mod item equivalence)
- [x] ItemMatch predicates (empty/exact/tagged)
- [ ] Recipe format (.recipe files)
- [ ] Recipe matching (shaped, shapeless, furnace, custom)
- [ ] RecipeRegistry and lookup
- [ ] Crafting grid logic (2x2, 3x3)

### Rendering Enhancements

- [x] Greedy meshing, LOD 0-4, view-relative coordinates
- [x] Non-cube block geometry (.model/.geom files)
- [x] Ambient occlusion
- [ ] Entity rendering pipeline
- [ ] Translucent block rendering (water, glass) with proper sorting
- [ ] Particle system
- [ ] Block break animation
- [ ] Item in-hand rendering

---

## Suggested Implementation Order

| Order | System | Rationale |
|-------|--------|-----------|
| 1 | Crafting & Recipes | Turns gathered resources into progression |
| 2 | Tool properties | Mining speed, durability — core gameplay loop |
| 3 | Player survival (health/hunger/sprint) | Core gameplay feel |
| 4 | Fluid system | Water/lava add terrain interest |
| 5 | Entity AI & spawning | Populates world with life/danger |
| 6 | Entity rendering | See the mobs |
| 7 | UI screens (inventory, crafting, HUD) | Interface for all the above |
| 8 | Sky visuals (skybox, sun/moon, clouds) | Visual atmosphere |
| 9 | Music & ambient audio | Game feel polish |
| 10 | In-game commands & console | Debug and modding |
| 11 | Multiplayer | Last — requires all other systems stable |

---

*Last updated: 2026-02-12*
