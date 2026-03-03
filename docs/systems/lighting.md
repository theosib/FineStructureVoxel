# System: Lighting

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/light_engine.hpp` (and related)
**Old docs:** [old_docs/09-lighting.md](../../old_docs/09-lighting.md)

---

## Overview

Dual light system: **sky light** (0-15) and **block light** (0-15). Both stored packed in 4+4 bits per block. `LightEngine` uses BFS propagation on block changes. Heightmap per column for sky light optimization. AO calculated per-vertex in mesh builder. In Phase 21, fluid attenuation was added with a logarithmic model.

---

## Key Types

| Type | Description |
|------|-------------|
| `LightEngine` | BFS propagation engine; runs on dedicated thread |
| `LightData` | Per-subchunk light storage; packed byte (sky<<4 \| block) |
| `BlockLightProvider` | Callback used by mesh builder to query light at position |
| `LightingUpdate` | Event struct sent to lighting thread (includes FluidTypeId since Phase 21) |

---

## Light Properties on BlockType

```cpp
blockType.setLightEmission(15);      // how much light it emits (0-15)
blockType.setLightAttenuation(1);    // how much it reduces light passing through (0-15)
blockType.setBlocksSkyLight(true);   // whether it blocks sky light from above
```

---

## Light Storage (Per Block)

```
Packed byte: (sky_light << 4) | block_light
sky_light:   0-15 (4 bits)
block_light: 0-15 (4 bits)
```

---

## LightEngine API

```cpp
#include <finevox/core/light_engine.hpp>

LightEngine engine;
engine.startLightThread();
engine.stopLightThread();

// Enqueue a lighting update (from game thread after block change)
LightingUpdate update;
update.pos = blockPos;
update.oldType = previousBlockType;
update.newType = newBlockType;
update.oldFluid = previousFluidType;   // Phase 21 addition
update.newFluid = newFluidType;        // Phase 21 addition
engine.enqueueUpdate(update);

// Callback for mesh builders to query light (registered on engine init)
auto provider = engine.createLightProvider();
// BlockLightProvider is a std::function<uint8_t(BlockCoord)>
// returns packed byte: (sky << 4) | block
```

---

## Smooth Lighting

Per-vertex light values calculated in `MeshBuilder`. For each face corner (4 vertices per face), samples the 4 blocks adjacent to that corner and averages:

```cpp
// Called from MeshBuilder for each face vertex
float getFaceLight(BlockCoord blockPos, Face face, int cornerU, int cornerV);
// Returns 0.0-1.0 normalized light value for that corner
```

**Toggle:** `MeshBuilder::setSmoothLighting(true/false)` — defaults to true.

---

## Ambient Occlusion (AO)

AO is calculated per-vertex in `MeshBuilder` (not in the light engine). Uses 3 neighbor blocks at each face corner:
- `side1`, `side2` (the two adjacent blocks in the face plane)
- `corner` (the diagonal block)

```
if (side1 && side2) → AO = 0.2 (fully occluded)
else: AO = 0.3 + (3 - count_of_solid) × 0.25
```

LOD meshes do NOT calculate AO (use default 1.0f — acceptable at LOD transition distances).

**Face diagonal flipping**: When AO values differ across a quad, the diagonal is flipped to minimize AO artifacts. This is automatic in `MeshBuilder`.

---

## Sky Light

Sky light column initialization:
1. `ChunkColumn::lightInitialized_` starts false
2. On first mesh request for the column, `LightEngine::ensureColumnInitialized()` is called
3. Column scanned top-to-bottom: air blocks above heightmap get sky=15, blocks below get 0
4. Propagates sky light sideways into caves etc.

**Sky light shortcut**: If block is air with no solid/fluid block directly above, it's assigned max sky brightness without BFS (common case optimization).

---

## Fluid Light Integration (Phase 21)

```cpp
// Combined attenuation function
float getAttenuationWithFluid(BlockCoord pos, FluidQueryProvider provider, float& outFluidMult);
// outFluidMult: multiplicative factor for logarithmic model

// Logarithmic model for fluid:
// brightness *= pow(attenuationBase, distance) for each block of fluid
// Standard model: additive attenuation (same as blocks)
```

Fluid light integration points:
- `onFluidPlaced`: triggers emission propagation (if fluid emits), removes old light, re-propagates from neighbors
- `onFluidRemoved`: removes fluid's attenuation from BFS, re-propagates
- Only enqueues when fluid **type** changes (not just level)

---

## Shader Integration

Push constants used for sky/block light rendering:

```glsl
layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec3 sunDirection;
    float skyBrightness;    // 0.0-1.0 (based on time of day)
    float ambientLevel;     // minimum light floor
};
```

In `chunk.frag`: sky light multiplied by `skyBrightness`, block light unaffected by time of day.

`ChunkVertex` has two separate light channels (split in Phase 15):
```cpp
struct ChunkVertex {
    // ...
    float skyLight;    // was single 'float light' before Phase 15
    float blockLight;
};
```

---

## Gotchas

- `LightData` is stored SEPARATELY from block data — separate from SubChunk, own structure
- Sky light is lazy-initialized per column — first mesh request triggers it
- AO calculation happens in MeshBuilder, not LightEngine
- Phase 21: `LightingUpdate` has two extra fields (`oldFluid`, `newFluid`) — must be set when fluid changes
- BFS budget: `maxPropagationDistance_ = 256` — seal test enclosures to prevent budget exhaustion (see PATTERNS.md)
- `glm::pi<float>()` for sun angle calculations requires `#include <glm/gtc/constants.hpp>`
