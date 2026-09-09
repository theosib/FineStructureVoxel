# System: World Generation

**Library:** `finevox_worldgen` (`finevox::worldgen::`)
**Headers:** `include/finevox/worldgen/`
**Resource files:** `resources/biomes/*.biome`, `resources/features/*.feature`, `resources/features/*.ore`
**Old docs:** [old_docs/27-world-generation.md](../../old_docs/27-world-generation.md)

---

## Overview

Pipeline-based procedural generation. `GenerationPipeline` runs ordered `GenerationPass` objects per column. Built-in passes: TerrainPass → SurfacePass → CavePass → OrePass → StructurePass → DecorationPass. FluidPass (priority 7000, in core) fills water below sea level. Biomes use Voronoi + climate noise. Cross-column features recompute deterministically (no shared state).

**Namespace note:** Forward declarations of core types (BlockTypeId, etc.) in worldgen headers must be in `namespace finevox {}`, NOT `namespace finevox::worldgen {}` (wrong type would be created).

---

## Noise Library

```cpp
#include <finevox/worldgen/noise.hpp>

// All noise types implement Noise2D or Noise3D interface:
// float evaluate(x, z) or float evaluate(x, y, z)  → result in [-1, 1]

// 2D noise types
PerlinNoise2D perlin(seed);
OpenSimplex2D simplex(seed);
VoronoiNoise2D voronoi(seed, cellSize=256.0f);

// 3D noise types
PerlinNoise3D perlin3(seed);
OpenSimplex3D simplex3(seed);

// Composite noise
FBMNoise2D fbm(std::make_shared<PerlinNoise2D>(seed), octaves=6, lacunarity=2.0f, gain=0.5f);
RidgedNoise2D ridged(base, sharpness=1.0f);
BillowNoise2D billow(base);
DomainWarp2D warped(base, warpNoise, strength=64.0f);

// Modifiers
ScaledNoise2D scaled(base, scaleX, scaleZ);
ClampedNoise2D clamped(base, min, max);
CombinedNoise2D combined(a, b, blend_t);  // lerp between a and b

// Voronoi — returns full result struct
VoronoiResult r = voronoi.evaluateFull(x, z);
// r.distance1     — distance to nearest cell center
// r.distance2     — distance to second-nearest
// r.cellId        — unique ID for the cell
// r.cellCenter    — {x, z} of nearest center
float f1 = voronoi.evaluate(x, z);           // distance1 only
float edge = voronoi.evaluateF2MinusF1(x, z); // edge detection
```

---

## Seed Derivation

```cpp
#include <finevox/worldgen/noise_hash.hpp>

// Derive seeds to prevent noise correlation between different uses
uint64_t terrainSeed = NoiseHash::deriveSeed(worldSeed, "terrain");
uint64_t caveSeed = NoiseHash::deriveSeed(worldSeed, "caves");
uint64_t biomeSeed = NoiseHash::deriveSeed(worldSeed, "biomes");

// Point hashing (for deterministic per-position values)
uint32_t h = NoiseHash::hash2D(x, z, seed);
uint32_t h3 = NoiseHash::hash3D(x, y, z, seed);
```

---

## Biome System

```cpp
#include <finevox/worldgen/biome_registry.hpp>
#include <finevox/worldgen/biome.hpp>

// BiomeId
BiomeId plains = BiomeId(StringInterner::global().intern("finevox:plains"));

// BiomeProperties
BiomeProperties props;
props.temperature = {0.3f, 0.7f};   // temperature range for this biome
props.humidity = {0.2f, 0.6f};      // humidity range
props.baseHeight = 64.0f;           // sea level block height
props.heightVariation = 8.0f;
props.surfaceBlock = stoneId;       // what's on top
props.subsurfaceBlock = dirtId;
props.subDepth = 3;                 // depth of subsurface layer
props.featureDensity["finevox:oak_tree"] = 0.01f;

// Registration
BiomeRegistry::global().registerBiome("finevox:plains", props);

// BiomeMap — query by world position
BiomeMap biomeMap(biomeSeed);
BiomeId biome = biomeMap.selectBiome(x, z, climate);
float height = biomeMap.getBlendedHeight(x, z, heightNoise);  // interpolated
```

---

## Generation Pipeline

```cpp
#include <finevox/worldgen/generation_pipeline.hpp>

GenerationPipeline pipeline;
pipeline.addPass(std::make_unique<TerrainPass>(terrainSeed));
pipeline.addPass(std::make_unique<SurfacePass>());
pipeline.addPass(std::make_unique<CavePass>(caveSeed));
pipeline.addPass(std::make_unique<OrePass>(oreSeed));
pipeline.addPass(std::make_unique<StructurePass>());
pipeline.addPass(std::make_unique<DecorationPass>());
// FluidPass is added separately at priority 7000 (in core, not worldgen)

// Generate a column
pipeline.generateColumn(column, world, biomeMap);

// Custom pass insertion
pipeline.replacePass(std::make_unique<MyCustomTerrainPass>());  // replaces by name
pipeline.removePass("TerrainPass");  // remove by name

// GenerationPass interface
class MyPass : public GenerationPass {
public:
    std::string name() const override { return "MyPass"; }
    int priority() const override { return 1500; }  // between Terrain(1000) and Surface(2000)
    bool needsNeighbors() const override { return false; }

    void generate(GenerationContext& ctx) override {
        for (int x = 0; x < 16; x++) {
            for (int z = 0; z < 16; z++) {
                int height = ctx.heightmap[x * 16 + z];
                // Set blocks at ctx.column
            }
        }
    }
};
```

---

## GenerationContext

```cpp
struct GenerationContext {
    ChunkColumn& column;
    ColumnPos pos;
    World& world;
    BiomeMap& biomeMap;
    uint64_t worldSeed;
    int heightmap[256];    // populated by TerrainPass; [x*16+z]
    BiomeId biomes[256];   // populated by BiomeMap; [x*16+z]

    uint64_t columnSeed() const;  // deterministic per-column seed
};
```

---

## Pass Priorities

| Priority | Pass | Purpose |
|----------|------|---------|
| 1000 | `TerrainPass` | Noise heightmap + 3D density; fills heightmap[] |
| 2000 | `SurfacePass` | Biome-specific surface layers (grass, sand, etc.) |
| 3000 | `CavePass` | 3D noise carving |
| 4000 | `OrePass` | Configurable ore blob placement |
| 5000 | `StructurePass` | Trees, buildings via feature system |
| 6000 | `DecorationPass` | Flowers, grass, small features |
| 7000 | `FluidPass` | Fill air below sea level with water (in core) |
| 9000 | `FinalizationPass` | Post-processing cleanup |

---

## Feature System

```cpp
#include <finevox/worldgen/feature_registry.hpp>
#include <finevox/worldgen/feature.hpp>

// Built-in features
class TreeFeature : public Feature {
    // Configurable: trunkBlock, leafBlock, trunkHeight, crownRadius
};
class OreFeature : public Feature {
    // Configurable: oreBlock, veinSize, density, heightRange
};
class SchematicFeature : public Feature {
    // Stamps a loaded Schematic template
};

// FeaturePlacement rule
FeaturePlacement rule;
rule.featureName = "finevox:oak_tree";
rule.density = 0.01f;             // chance per surface block
rule.heightRange = {60, 128};    // only place at Y 60-128
rule.requiredBiomes = {plainsId, forestId};
rule.requiresSurface = true;      // only on top of solid block

FeatureRegistry::global().registerFeature("finevox:oak_tree", std::make_unique<TreeFeature>(config));
FeatureRegistry::global().addPlacement(rule);
```

### Schematic Rotation Semantics

`rotateSchematic(s, R)` (in `finevox/worldgen/schematic.hpp`) is the canonical way to
orient a structure template. It transforms three things, not just positions:

- **Cells** — the result extent is the bounding box of `R` applied to the eight corners of
  the source cell-index box, shifted so the minimum corner is `(0,0,0)`. A non-air cell at
  `p` lands at `R.apply(p) - minCorner`.
- **Block orientation** — the block's own `rotation` applies *first*, then the schematic
  rotation: `dst.rotation.apply(v) == R.apply(src.rotation.apply(v))` for every `v`.
  Since `Rotation::compose` is the matrix product `this * other`, that means
  `R.compose(src.rotation)` — getting this order backwards makes rotated stairs/doors face
  the wrong way while positions still look correct.
- **Sub-block displacement** — `displacement` is in block-local axes, so it is rotated as a
  vector by `R`'s matrix. `Rotation::matrix()` entries are only -1/0/+1, so the transform is
  exact in float; no epsilon comparisons are needed or wanted.

`typeName` is copied verbatim, `extraData` is deep-copied via `DataContainer::clone()`, and
`name()`/`author()` carry over. Rotation is invertible cell-for-cell
(`rotate(rotate(s, R), R.inverse())` reproduces `s`) and composes
(`rotate(rotate(s, R), R) == rotate(s, R.compose(R))`). See
`tests/test_schematic_transform.cpp` for the pinned contract.

### Schematic Mirror Semantics

`mirrorSchematic(s, axis)` follows the same rule as `rotateSchematic`, with the
transform being the reflection matrix `M` — identity with `-1` in the `axis` slot:

- **Cells** — the extent is unchanged; a non-air cell at `p` moves to `p` with its
  `axis` component replaced by `size[axis] - 1 - p[axis]`.
- **Block orientation** — the block's own `rotation` applies *first*, then the
  reflection: `dst.rotation == M.compose(src.rotation)`, so
  `dst.rotation.apply(v) == M.apply(src.rotation.apply(v))` for every `v`. Leaving
  `rotation` untouched (the pre-S2 behaviour) mirrors the layout of a structure while
  every stair, door and wedge inside it keeps facing its original way.
- **Sub-block displacement** — transformed by `M`, i.e. the `axis` component is
  negated. For a pure axis reflection this coincides with the old hand-rolled
  negation, so the displacement half was already correct; the defect was the
  untransformed `rotation`.

Mirroring is an involution: `mirror(mirror(s, axis), axis) == s`.

> **Reflections are not yet persistable.** `M` has determinant −1, so
> `M.compose(src.rotation)` is generally *not* one of the 24 proper rotations.
> `Rotation` stores a general −1/0/+1 matrix and `compose`/`apply`/`inverse` all behave
> correctly on it (`inverse()` is the transpose, which is a true inverse for any
> orthogonal matrix). But `Rotation::index()` has no table entry for a reflection and
> silently returns `0` (identity), and **both** the world storage
> (`SubChunk::rotations_`, a `uint8_t` index array written via `setRotation()`) and the
> schematic file format (`schematic_io.cpp`, which encodes `rotation.index()`) store
> orientation as that index. A mirrored schematic is therefore correct in memory but
> loses per-block orientation the moment it is stamped into the world or saved.
> Widening those two encodings to carry a full orientation (or adding a mirrored-model
> concept per block type) is a prerequisite for using `mirrorSchematic` in generation,
> and is deliberately out of scope for S2 — it is an on-disk format change.

---

## .biome File Format

```
# resources/biomes/plains.biome
name: finevox:plains
temperature: 0.3 0.7
humidity: 0.2 0.6
base_height: 64
height_variation: 8
surface_block: finevox:grass
subsurface_block: finevox:dirt
sub_depth: 3
feature finevox:oak_tree 0.01
feature finevox:tall_grass 0.2
```

---

## Gotchas

- Forward declarations of core types in worldgen: use `namespace finevox { }` (NOT `finevox::worldgen`)
- Cross-column features (e.g., trees partially in adjacent column): `needsNeighbors()` = true loads neighbors temporarily
- `heightmap[]` is only valid after `TerrainPass` runs — don't read in earlier passes
- Seed derivation: each noise use case needs its own salt → `NoiseHash::deriveSeed(base, "my_use")`
- `std::hash<finevox::worldgen::BiomeId>` specialization must use fully-qualified type name
- Biome blending: weighted sum of neighbor biomes' heights for smooth transitions
- `GenerationContext::columnSeed()` is deterministic per-column — use for per-column RNG
