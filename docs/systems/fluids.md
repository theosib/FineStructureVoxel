# System: Fluids

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/fluid*.hpp`
**Resource files:** `resources/fluids/*.fluid` (water.fluid, lava.fluid)
**Old docs:** [old_docs/AI-NOTES.md](../../old_docs/AI-NOTES.md) — Phase 21 section (extensive)

---

## Overview

Full fluid system: types, per-subchunk storage, BFS flow simulation, translucent mesh rendering, light integration, entity physics interaction, serialization, worldgen pass, and script API. All 6 sub-phases complete (1684 tests).

---

## Key Types

| Type | Description |
|------|-------------|
| `FluidTypeId` | Interned ID; `EMPTY_FLUID_TYPE` = id 0 |
| `FluidType` | Data struct: flow/physics/visual/light/sound/damage/fog/container properties |
| `FluidRegistry` | Singleton; thread-safe; `FluidRegistry::global()` |
| `FluidLayer` | Per-SubChunk storage; packed `uint8_t[4096]` (upper 4 = palette idx, lower 4 = level) |
| `FluidPalette` | Max 15 fluid types per subchunk; auto-cleanup on ref count drop to 0 |
| `FluidCell` | Pack/unpack helper; `isSource()` (level=15), `isFlowing()` (level 1-14) |
| `FluidInteractionRegistry` | Symmetric (A,B)→result lookup; canonical key ordering |
| `FluidLoader` | ConfigParser-based `.fluid` loader; `loadDirectory()` for bulk |
| `FluidSimulator` | Hybrid BFS flow algorithm (see flow logic below) |
| `FluidTickManager` | Game tick integration; tracks active subchunks |
| `FluidMeshBuilder` | CPU-side mesh generation; reuses `ChunkVertex` layout |
| `FluidContactInfo` | Entity-fluid interaction data: submersion, density, viscosity, buoyancy, flow |
| `FluidQueryProvider` | `std::function<pair<FluidTypeId,uint8_t>(BlockCoord)>` callback for physics decoupling |

---

## Storage

SubChunk has a lazy `unique_ptr<FluidLayer>` — zero overhead when no fluids present:

```cpp
// Fluid access through SubChunk
subchunk.setFluid(localIndex, fluidTypeId, level);
auto [type, level] = subchunk.getFluid(localIndex);
subchunk.removeFluid(localIndex);  // auto-deallocates FluidLayer if now empty

// World-level access
world.setFluid(pos, fluidTypeId, level);
world.getFluid(pos);           // returns FluidTypeId
world.getFluidLevel(pos);      // returns uint8_t 0-15
world.hasFluid(pos);           // bool
world.removeFluid(pos);
```

---

## Flow Simulation

`FluidSimulator` uses a hybrid algorithm per tick:
1. **Gravity flow** — fluid falls down if block below is empty/fluid (uses maxLevel, not FLUID_SOURCE_LEVEL)
2. **Horizontal spread** — source blocks spread horizontally at full level
3. **Slope detection** — prefers flowing toward lower terrain (steeper path gets more flow)
4. **Equalization** — connected fluid columns at same height equalize levels
5. **Source formation** — two flowing sources meeting create a new source block
6. **Drain cascade** — FIFO (deque) BFS for proper cascade ordering

**Key constant:** `FLUID_SOURCE_LEVEL = 15` (sources), levels 1-14 = flowing, 0 = empty.

### FluidTickManager

```cpp
// Track active subchunks (ones with flowing fluid)
tickManager.markActive(subchunkPos);
tickManager.tick(world, simulator);  // process up to maxUpdatesPerTick (4096)
```

---

## Rendering

Fluid meshes are translucent and rendered in a separate pass (back-to-front sorted, depth-write OFF):

```cpp
// FluidMeshBuilder
FluidMeshBuilder builder;
builder.build(subchunk, neighbors, fluidRegistry, lightProvider);
auto mesh = builder.takeMesh();  // RawMeshPtr

// SubChunkView holds both opaque and fluid meshes
view.setFluidMesh(fluidMesh);

// Underwater detection in updateCamera()
auto [type, level] = world_.getFluid(cameraBlockPos);
if (type != EMPTY_FLUID_TYPE) {
    worldRenderer.setFogColor(fluidType.fogColor);
    worldRenderer.setFogDensity(fluidType.fogDensity);
}
```

**Shader:** `fluid.frag` — tint-based (uses `tileBounds` field as tint color, no atlas sampling). Same vertex layout as `chunk.vert`.

**Pipeline settings:** `alphaBlending()` + `depthWrite(false)` + `cullNone()`. Shares `pipelineLayout_` with opaque pipeline.

---

## Light Integration

```cpp
// Combined block + fluid attenuation
float getAttenuationWithFluid(blockPos, fluidQueryProvider, outFluidMult);

// FluidSimulator notifies LightEngine when fluid TYPE changes (not just level)
simulator.setLightEngine(&lightEngine);
// onFluidPlaced/Removed: handles emission propagation, removal, re-propagation
```

Logarithmic light model: `attenuationBase` used for multiplicative attenuation through fluid.

---

## Entity Physics Interaction

```cpp
// FluidContactInfo — result of fluid contact query
struct FluidContactInfo {
    float submersion;          // 0.0-1.0 (fraction of entity height in fluid)
    float density;             // fluid density (water=1000)
    float viscosity;           // drag coefficient
    float buoyancyFactor;      // how much buoyancy applied
    glm::vec3 flowForce;       // directional push
    float damage;              // damage per second
    glm::vec3 flowDirection;   // normalized flow direction
};

// Usage in EntityManager::physicsPass()
FluidContactInfo contact = computeFluidContact(entityBounds, fluidQuery);
applyBuoyancy(entity, contact, gravity, dt);   // (density/1000)*g*submersion*buoyancy*dt
applyFluidDrag(entity, contact, dt);           // velocity *= (1 - viscosity*submersion*dt)
applyFlowForce(entity, contact, dt);           // velocity += flowDir*flowForce*submersion*dt
```

**Entity fluid state fields:** `inFluid_`, `isSubmerged_`, `fluidSubmersion_`, `inFluidType_`, `fluidDamageAccumulator_`

Fluid damage accumulates until >= 1.0 HP, then applied (avoids sub-tick damage spam).

---

## Serialization (Phase 21-6F)

Fluid data serialized in CBOR via `SerializedSubChunk`:
- Palette (array of fluid type names)
- Packed data (same `uint8_t[4096]` layout)
- Stored alongside block data in region files

---

## Worldgen Integration (FluidPass — Phase 21-6C)

`FluidPass` runs at priority 7000 (after all terrain/structure passes):
```
For each column, scan downward from sea level.
If block is air, fill with water to sea level.
```

---

## Script API (Phase 21-6D)

```
fluid_at x y z              -- FluidTypeId name or nil
fluid_level x y z           -- level 0-15
fluid_place x y z name lvl  -- place fluid
fluid_remove x y z          -- remove fluid
fluid_set_level x y z lvl   -- change level only
```

---

## Fluid File Format

```
# resources/fluids/water.fluid
name: finevox:water
density: 1000
viscosity: 0.8
flow_speed: 1.0
buoyancy_factor: 1.0
light_attenuation: 1
fog_color: 0.0 0.3 0.6
fog_density: 0.2
damage: 0.0
```

---

## Gotchas

- `FluidRegistry::registerType(name, FluidType)` — takes name string as first param
- `FluidTypeId.id` is a public `uint32_t` member, not a function
- `isStaticSource()` skips source blocks surrounded by solid/same-type sources (performance optimization)
- FIFO (deque) for drain cascade BFS — LIFO gives wrong order
- Gravity flow uses `maxLevel` (current max in neighborhood), NOT `FLUID_SOURCE_LEVEL`
- `BlockRegistry::getType()` returns `const BlockType&` (not pointer); no `tryGetType()`
- Fluid simulation only enqueues lighting updates when fluid TYPE changes (not level)
- `computeFluidContact()` uses `FluidMeshBuilder::surfaceHeight()` for precise submersion fraction
