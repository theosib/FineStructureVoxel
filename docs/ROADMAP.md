# FineStructureVoxel — Roadmap

> Future phases and deferred design work. All phases 0–21 are complete.
> See [STATUS.md](STATUS.md) for current state and deferred items from completed phases.
>
> Priority is driven by **Shattered Lands** (`test_voxel_game/GAME_CONCEPT.md`),
> a kitchen-sink stress-test game designed to validate engine generality.

---

## Deferred From Completed Phases

These items were designed and partially scoped but deferred during implementation:

### From Phase 9 (Block Updates)
- ~~**Scheduled tick persistence**~~ ✓ — TickJournal persists pending ticks per-column alongside region files. IOManager and ColumnManager wired for save/load/eviction round-trips.
- ~~**UpdatePropagationPolicy interface**~~ ✓ — Per-block-type `PropagationPolicy` enum (Drop/Defer). `notifyNeighbors()` emits `BlockUpdate` events for unloaded neighbors when policy is Defer, leveraging existing event deferral/journaling infrastructure.
- **Network quiescence protocol** — For connected blocks (wires, pipes), a way to detect when an update wave has settled before snapshotting for network sync.

### From Phase 2 (Persistence)
- **LZ4 compression** — Infrastructure in place (ChunkFlags compression bit in region file header). Just needs actual LZ4 encode/decode wired in.
- **Region file corruption recovery** — If a region file's TOC is corrupted (truncated write), no recovery path. Needs checksum validation and fallback to previous TOC backup.

### From Phase 10 (World Generation)
- **Schematic file I/O** — CBOR format designed (see old_docs/21-clipboard-schematic.md). Clipboard/copy-paste in-memory works; disk save/load not wired.

### Design Issue (Phase 19 / Script)
- **Cross-context native functions** — Functions like `show_main_menu()` crossing finescript execution contexts is architecturally awkward. The right fix is a message-passing or state-machine approach where UI state transitions are events rather than direct calls.

---

## Planned Future Work

### Tier 1 — Core Game Loop (Phases 22–24)

These features together produce a playable survival loop: craft, eat, fight, die, respawn.

#### Phase 22: Crafting + Recipe System
**Priority: Immediate** | Data model complete (ItemStack, InventoryView, ItemMatch predicate)

- **RecipeRegistry** — shaped (3x3 grid), shapeless (unordered set), smelting/cooking (input + fuel + time). Each recipe specifies required station type (or none for hand-crafting).
- **Recipe matching** — uses existing ItemMatch/TagId for flexible ingredient specification.
- **Crafting UI** (finegui) — recipe book with category tabs and search, shows required station and materials, craft button with quantity selector.
- **Inventory UI** — drag-and-drop grid, shift-click transfer, sort button, equipment slots.
- **Crafting stations as block entities** — workbench, furnace, etc. have inventories and UI. Adjacent-container material pull (stations draw from neighboring chests).
- **Smelting/cooking progress** — fuel consumption, progress bar, output slot.

#### Phase 23: Player Survival + Combat
**Priority: Immediate**

- **Survival stats** — hunger, stamina (thirst/temperature deferred to weather phase). Hunger drains over time; food restores it with saturation. Stamina spent on sprint/combat/mining, regens based on food level.
- **Damage type system** — physical (slash/crush/pierce), fire, drowning, fall, void. Armor reduces by type. EntityState already has health.
- **Fall damage** — velocity-based threshold on landing.
- **Death/respawn** — drop inventory on death, respawn at spawn point (bed or world spawn). Death marker entity.
- **Melee combat** — attack sweep with hitbox (not just raycasting). Weapon reach, speed, damage vary by weapon type. Knockback.
- **Ranged combat** — projectile entities with arc/gravity. Bow charge mechanic.
- **Shield blocking** — damage reduction when blocking, timed parry for stagger.
- **HUD** — health/hunger/stamina bars, hotbar, crosshair, damage flash.

#### Phase 24: Horde Defense + Mob Variety
**Priority: High**

- **Horde event system** — scheduled waves tied to WorldTime (every N days). Wave composition, intensity scaling with player progression, storm visual/audio effects.
- **Mob variety** — passive fauna (flee AI), neutral (territorial), hostile surface/cave/siege types. Uses existing AIBrain goal system with new goal types.
- **Siege AI** — block-targeting goal (attack doors, weak points, load-bearing blocks). Tunneler/climber/bomber variants.
- **Organic spawning** — nest/den-based: world-gen places spawn structures, creatures reproduce from them. Destroying nests clears area permanently.
- **Loot tables** — weighted random item drops per mob type, with condition predicates.

### Tier 2 — Living World (Phases 25–27)

These add environmental depth: the world changes around the player.

#### Phase 25: Particle System (GPU)
**Priority: High** — Pervasive dependency (weather, combat, fire, environment all need it)

- **GPU particle emitter/updater** — compute shader particle simulation, billboard rendering.
- **Emitter types** — point, sphere, box, cone, mesh-surface. Rate, burst, lifetime, velocity, gravity, drag.
- **Renderers** — billboard, stretched billboard (sparks), mesh (debris). Texture atlas animation.
- **Physics interaction** — particles collide with voxel terrain (rain hits ground, sparks bounce).
- **Standard emitters** — block-break fragments, torch flame, hit sparks, footstep dust, fluid splash.

#### Phase 26: Weather + Temperature + Seasons
**Priority: High**

- **Weather state machine** — clear, cloudy, rain, snow, thunderstorm, sandstorm (biome-specific). Transitions over time. Wind vector affects particles and projectiles.
- **Cloud layer** — scrolling cloud texture above world max height, density varies with weather.
- **Precipitation** — rain/snow particles (uses Phase 25). Snow accumulates on surfaces, melts near heat. Rain creates puddles (temporary fluid).
- **Temperature model** — base from biome + altitude + latitude. Modified by weather, time of day, shelter, proximity to fire/lava. Hypothermia/heatstroke penalties.
- **Seasonal cycle** — 4 seasons across configurable game-day count. Affects temperature, daylight hours, precipitation type, tree foliage color, crop growth.
- **Audio integration** — rain on surfaces (material-dependent), thunder, wind intensity.

#### Phase 27: Farming + Growth
**Priority: Medium**

- **Soil model** — soil blocks with nutrient levels (N/P/K simplified). Crops deplete nutrients, fertilizer/rotation restores them. Tilled/irrigated variants.
- **Crop growth** — multi-stage block transitions driven by scheduled ticks. Growth rate modified by season, temperature, water proximity, light, soil quality.
- **Irrigation** — water source blocks or fluid pipes within radius boost growth. Tests fluid system integration.
- **Animal husbandry** — penned passive mobs breed when fed. Offspring entities. Basic trait inheritance (future: selective breeding).
- **Food spoilage** — items decay over time (DataContainer timestamp). Preservation methods: salting, smoking, cold storage (temperature-dependent).

### Tier 3 — Engineering Depth (Phases 28–30)

Automation and logic — the "factory floor endgame."

#### Phase 28: Structural Integrity
**Priority: Medium**

- **Support value propagation** — each block has mass and support capacity. Load transfers to neighbors via block events. Vertical supports carry more than horizontal.
- **Material properties** — wood ~6 horizontal, stone ~10, reinforced ~15, steel ~25.
- **Collapse cascade** — when support fails, propagate through connected blocks. Collapsed blocks become debris item entities or rubble blocks.
- **Player-triggered only** — world-gen terrain is exempt; physics activates when player places/removes blocks near a structure.
- **Build mode overlay** — structural stress visualization (green/yellow/red) via UI.

#### Phase 29: Wiring + Logic (Oligosynthetic)
**Priority: Medium** | Requires UpdatePropagationPolicy (Phase 9 deferred)

8 configurable block types replace 30+ special-purpose blocks:
- **Wire** — carries signal (0–15), color-coded channels for crossing without interference.
- **Junction** — split/merge/redirect. Configurable mode.
- **Gate** — AND/OR/NOT/XOR/XNOR via interaction UI.
- **Latch** — SR/D-flip-flop/toggle. State storage.
- **Timer** — configurable pulse interval or one-shot delay.
- **Sensor** — detects: pressure, light, entity presence, container level, temperature, time of day.
- **Actuator** — push/pull/rotate/dispense/valve/switch.
- **Comparator** — greater/less/equal/threshold on two inputs.

Signal propagation via scheduled ticks and neighbor updates. Each block stores mode + state in DataContainer.

#### Phase 30: Item Transport + Power
**Priority: Medium**

- **Conveyor belts** — visual item transport (items rendered on belt surface). Speed tiers. Curves and junctions.
- **Item pipes** — enclosed bulk transport. Filters/sorters at junctions.
- **Hoppers** — pull from above, push below. Container interface.
- **Mechanical power** — waterwheels, windmills, hand cranks. Axle/gear transmission with torque and speed.
- **Steam power** — fuel + water → steam. Steam engines connect to mechanical network.
- **Electrical power** (late tier) — generators, wires, transformers. Supply/demand grid with brownout.
- **Auto-crafters** — configure recipe, supply materials via belt/pipe, output to belt/pipe. Requires power.

### Tier 4 — Scale + Polish

#### finenet Migration
**Priority: Low** | See [old_docs/PLAN-network-layer.md](../old_docs/PLAN-network-layer.md) and [old_docs/26-network-protocol.md](../old_docs/26-network-protocol.md)

Extract `Queue<T>`, `KeyedQueue<K,D>`, and `WakeSignal` into standalone `finenet` library. Local single-player uses same `Connection` API as multiplayer — identical code paths.

#### Multiplayer / Network Layer
**Priority: Low (requires finenet)** | See [old_docs/26-network-protocol.md](../old_docs/26-network-protocol.md)

- Thin client architecture — server authoritative, client predicts and corrects
- Semantic quantization — types transmitted by interned name, not numeric ID
- Asset streaming — textures/models from server if client lacks them
- UI protocol — server sends finescript UI definitions, client renders with finegui

#### LOD Boundary Stitching
**Priority: Low** | See [old_docs/22-phase6-lod-design.md](../old_docs/22-phase6-lod-design.md)

Visible seams at LOD boundaries. Full fix requires geometry shader or per-boundary vertex stitching. Not conspicuous at current transition distances.

---

## Architecture Decisions Pending

### Block Displacement
Partial design from Phase 0 (data model exists). Full impl requires:
- Face elision rules: only skip faces between two blocks with identical displacement AND matching type
- Mesh sorting for transparent displaced blocks
- See `old_docs/04-core-data-structures.md` section on BlockDisplacement

### Smart Block Placement (Rotation Preview)
Designed in old AI notes (see `old_docs/AI-NOTES.md` "Non-Cube Block System"):
- Player facing + target surface → suggested rotation
- R-key cycling through valid rotations with ghost block preview
- Side placement for vertical slabs
- Needs: ghost block renderer, rotation cycling input binding

### Terrain Visual Softening
From Shattered Lands concept (Section 1.5):
- Non-cubic surface geometry (slopes, rounded edges) for natural terrain
- Texture blending between adjacent block types (Hytale-style transition masks)
- Terraforming tools (flatten, raise, smooth, paint)

### Microblock / Sub-Voxel Detail
From Shattered Lands concept (Section 2.2):
- 16x16x16 micro-voxels within a single block space
- Custom mesh generation per microblock configuration
- Major rendering/storage challenge — design-only until needed

---

## Not Planned (Out of Scope for Engine)

- Water physics beyond current BFS flow (surface tension, erosion)
- Marching cubes / dual contouring (staying block-based)
- Full ecosystem simulation (predator/prey dynamics beyond basic AI goals)
