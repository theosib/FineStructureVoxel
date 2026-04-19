# System: Particles

**Library:** `finevox` core (`finevox::`) + `finevox_render` (`finevox::render::`)
**Script bridge:** `finevox_script` (`finevox::script::ParticleBridge`)
**Core headers:** `include/finevox/core/particle.hpp`, `particle_type.hpp`, `particle_registry.hpp`, `particle_system.hpp`, `particle_subsystem.hpp`, `include/finevox/script/particle_bridge.hpp`
**Render headers:** `include/finevox/render/particle_atlas.hpp`, `particle_renderer.hpp`
**Shaders:** `shaders/particle.vert`, `shaders/particle.frag`
**Status:** 26-A + 26-B shipped. CPU simulation on game thread, Vulkan
billboard rendering integrated into `WorldRenderer::render` after the
fluid pass. Minor follow-ups listed at end.

---

## Overview

Particles are small, short-lived visual entities. The engine provides the
mechanism (simulation, lifetime, collision, emitter scheduling, event
plumbing) and data files define every type/preset. Engine-side additions
are therefore zero-code for later phases: weather adds `.particle` +
`.emitter` files, combat fires bursts from existing triggers, etc.

Key architectural decisions in 26-A:

- **CPU simulation, game-thread authoritative.** `ParticleSystem` lives
  in `GameSession::Impl` and ticks via `ParticleSubsystem` at 30 TPS
  (phase `LateTick`, priority 50 — runs after entity physics so attached
  emitters see post-physics positions). Profiling with 8192 particles
  sits at ~0.37 ms/tick (~1% of the 33 ms budget) so GPU compute is not
  warranted yet.
- **Data model is a discriminated pair.** `ParticleTypeDef` (physical
  behavior) and `EmitterPresetDef` (shape + rate + particle type) are
  loaded from `.particle` and `.emitter` files into two global singletons.
- **Events are flexible Values.** Spawning an emitter goes through the
  same command-queue path as `block_placed` / `attack_entity` — making
  multiplayer, scripting, and testing all follow one pattern.

---

## Data Model

### ParticleTypeDef (from `.particle` files)

Immutable per-type configuration. One registry row per entry.

```
name: finevox:block_fragment
lifetime: 0.6-1.2                # seconds
initial_speed: 1.5-4.0
velocity_bias: 0 2.5 0           # vec3; scalar broadcasts to xyz
gravity: 14.0                    # positive pulls down
drag: 0.6                        # per-second velocity decay
size: 0.08-0.14
size_end_scale: 0.6              # final_size = start * this
start_tint: 1 1 1 1              # RGBA
end_tint: 1 1 1 0.2
atlas_region: block_fragment     # key into ParticleAtlas (26-B)
billboard: camera                # camera | velocity | axis
collides: true                   # voxel terrain collision
kill_on_collision: false
restitution: 0.15                # reflection damping
```

### EmitterPresetDef (from `.emitter` files)

```
name: finevox:block_break
shape: point                     # point | sphere | box | cone | line
shape_size: 0.3 0.1 0.3          # half-extents / radius / cone(r,h)
burst: 14                        # instantaneous spawn at start
spawn_rate: 0                    # particles/sec (0 = burst-only)
duration: 0.0                    # seconds; -1 = continuous
particle: finevox:block_fragment
tint: 1 1 1 1                    # optional per-emitter multiplier
```

### Registries

```cpp
ParticleTypeRegistry::global().get(ParticleTypeId::fromName("finevox:spark"));
EmitterPresetRegistry::global().get("finevox:block_break");
```

Same pattern as `LootRegistry` / `FluidRegistry`: `shared_mutex`, singleton,
loaded once during startup via `ParticleLoader::loadTypesFromDirectory()` /
`loadPresetsFromDirectory()`. Registration is late-bound: `BlockType`
stores an `EmitterPresetId` whose registry lookup happens at break time.

---

## Simulation

`ParticleSystem` owns authoritative state:

```cpp
ParticleSystem& sim = session->particles();

// One-shot burst
sim.burst("finevox:block_break", glm::dvec3(x, y, z));

// Retained emitter with lifetime
EmitterId id = sim.spawnEmitter("finevox:smoke_column", position, 5.0f);

// Emitter attached to an entity (position follows it each tick)
EmitterId id2 = sim.spawnEmitterAttached("finevox:torch_flame", entityId);

// Explicit shutdown
sim.despawnEmitter(id);

// Renderer interface
ParticleSystem::Snapshot snap = sim.snapshot();  // thread-safe copy-out
```

Per-tick loop:

1. Advance each emitter; fire burst once; accumulate continuous spawns;
   expire emitters past their duration.
2. Advance each particle:
   - age += dt
   - velocity.y -= gravity * dt
   - velocity *= (1 - drag * dt)
   - terrain collision (sub-stepped to 0.25-block max per sub-step when
     `collides` is set) — either kill or axial-reflect with restitution.
3. Remove dead particles (`age >= maxAge`).
4. Enforce the global cap by evicting oldest particles (default 8192,
   configurable via `setMaxParticles`).

---

## Event Plumbing

Emitter lifetime is also driven by flexible `finescript::Value` commands
through the normal `GameCommandQueue`, enabling scripting, multiplayer,
and test replay to all use the same path.

```cpp
// Event builders (include/finevox/script/event_value.hpp)
makeParticleSpawnValue(preset, pos, attachedEntity, durationOverride)
makeParticleDespawnValue(emitterId)
makeParticleBurstValue(preset, pos)

// GameActions convenience
session->actions().spawnParticleEmitter("finevox:smoke_column", pos);
session->actions().particleBurst("finevox:ember_burst", pos);
session->actions().despawnParticleEmitter(id);
```

Event types in `EVT_PARTICLE_SPAWN`, `EVT_PARTICLE_DESPAWN`,
`EVT_PARTICLE_BURST`. Handled in `game_session.cpp::executeCommand`.

---

## Wired-Up Fire Points

### Block break — `src/core/game_session.cpp`
Each `EVT_BLOCK_BROKEN` fires a burst. Preset lookup order:

1. `BlockType::particleEmitter()` — set from the block's `.model` file's
   `particle:` field (e.g. `luminite.model → particle: finevox:ember_burst`).
2. Engine default: `finevox:block_break`.
3. If neither is registered, no particles spawn (silent fallback).

### Fluid splash — `src/core/entity_manager.cpp::physicsPass`
On a dry→wet transition (mirrors the existing splash-sound trigger):

1. Tries fluid-specific preset: `finevox:splash_<bare_fluid_name>`
   (e.g. `finevox:splash_water`, `finevox:splash_lava`).
2. Falls back to `finevox:fluid_splash`.
3. Silent fallback if neither is loaded.

### Script natives — `include/finevox/script/particle_bridge.hpp`
Available once `ParticleBridge::registerNativeFunctions(engine)` is called:

| Native | Args | Returns |
|---|---|---|
| `spawn_particle_emitter` | preset, x, y, z, [duration] | emitter_id (0 on failure) |
| `spawn_particle_emitter_on_entity` | preset, entity_id, [duration] | emitter_id |
| `despawn_particle_emitter` | emitter_id | bool |
| `particle_emitter_active` | emitter_id | bool |
| `particle_burst` | preset, x, y, z | spawned_count |
| `active_particle_count` | — | int |
| `active_emitter_count` | — | int |

---

## `.model` Integration

Block models may override the default break emitter via a single line:

```
# in resources/blocks/my_block.model
particle: finevox:ember_burst
```

Loaded by `BlockModelLoader` into `BlockModel::particle()`, then wired
into `BlockType::setParticleEmitter()` at block-registration time in
`render_demo.cpp`. Blocks without the field use the default preset.

---

## Resource Files

Starter set (see `resources/particles/` and `resources/emitters/`):

| Type | Preset | Notes |
|------|--------|-------|
| `block_fragment` | `block_break` | Engine default for any broken block |
| `splash_drop`   | `fluid_splash`, `splash_water` | Water-tinted variant overrides generic |
| `spark`         | `ember_burst`, `splash_lava` | Velocity-stretched billboard |
| `torch_flame`   | `torch_flame` | Continuous (duration=-1), attach to block |
| `footstep_dust` | `footstep_dust` | Not yet fired — `FootstepTracker` hook deferred |
| `smoke_puff`    | `smoke_column` | Continuous; validates AxisLocked billboard |

---

## Rendering (Phase 26-B)

The renderer is a read-only consumer of the game-thread simulator.
Every frame:

1. `ParticleRenderer::render(cmd)` is called inside
   `WorldRenderer::render` **after the fluid pass** — particles draw
   over water surfaces so splashes, bubbles, and wet-entity drips look
   natural above the fluid mesh.
2. Pull `ParticleSystem::snapshot()` (shared_mutex copy-out).
3. Sort particles back-to-front by squared distance to camera for
   correct alpha-blend ordering.
4. For each particle, emit 6 view-relative vertices (two triangles
   forming a quad) oriented per its `BillboardMode`:
   - **CameraFacing** — quad perpendicular to view direction, corners at
     ±size/2 along the camera right / up vectors.
   - **VelocityStretched** — long axis along particle velocity projected
     onto the view plane; length grows with speed (capped at 4× size).
     Used for sparks and rain streaks.
   - **AxisLocked** — long axis fixed to world +Y, short axis
     perpendicular to it in the plane containing the camera-to-particle
     vector. Used for smoke columns and torch flames.
5. Upload the merged vertex buffer to a single `RawMesh` (in-place
   update with 2× capacity reserve) and issue **one draw call**.

### Pipeline

- Alpha-blended (`SRC_ALPHA`, `ONE_MINUS_SRC_ALPHA`).
- Depth test **on**, depth write **off** — particles don't occlude
  each other via Z, sort handles visibility.
- No backface culling — billboards viewed from behind still draw.
- Fragment shader alpha-cutoff at 0.02 kills fully-transparent pixels
  before blending, preventing halo artefacts on cut-out sprites.

### Atlas

`ParticleAtlas::loadFromDirectory(path)` loads every PNG in
`resources/textures/particles/` via stb_image, packs them into a single
RGBA texture in a square-ish grid (32×32 cells with a 2-pixel bleed
border — same convention as `BlockAtlas`), and records the UV rect per
region. Tiles that aren't exactly 28×28 get nearest-neighbor scaled.
`applyToRegistry(registry)` walks every registered `ParticleTypeDef`
and patches its `atlasUV` field based on `atlasRegion` — so by the time
the sim spawns a particle, the UV rect is already embedded in the type
def and flows into `Particle::atlasUV` for free (no per-frame string
lookup or registry access in the render hot path).

### Performance (CPU-only, measured on dev machine)

| Path | Cost |
|------|------|
| Simulation (8192 particles, 30 tick) | 0.37 ms/tick (~1 % of 33 ms budget) |
| Vertex generation (500 particles)    | 0.21 ms/iter (~1.3 % of 16.67 ms at 60 FPS) |

GPU compute particle updates stay out of scope — there's ample CPU
headroom for realistic workloads.

---

## Deferred / Known Debts

- **Footstep-dust auto-wiring.** `FootstepTracker` lives in
  `finevox_audio`; a direct `ParticleSystem*` from there would couple
  core↔audio. Scripts can call `particle_burst("finevox:footstep_dust",
  x, y, z)` today; a cleaner long-term fix routes footstep triggers
  through an existing event queue.
- **Torch/luminite continuous-flame block handler.** Spawning an
  attached emitter in `on_place` and despawning in `on_destroy` works
  as a `.fsc` handler today; a first-class C++ handler isn't landed
  yet.
- **Placeholder art.** The 6 PNGs currently in
  `resources/textures/particles/` are obviously-temporary (soft
  circles / streak / speckled square) generated by a small Pillow
  script. Replace with real tiles once Shattered Lands art is ready;
  no config changes needed as long as filename stems stay the same.
- **Per-particle lighting sampling** — not implemented (engine-level
  choice; skip unless needed for a specific effect).
- **Particle → entity collision / damage** — explicitly out of scope
  (visual only).
- **Animated atlas frames** — not implemented (static frames only).
- **GPU compute particle updates** — not needed at current loads; keep
  the CPU path unless profiling shows otherwise.
