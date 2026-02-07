# 27. World Generation

[Back to Index](INDEX.md) | [Previous: Network Protocol](26-network-protocol.md)

---

## 27.1 Overview

The world generation system provides engine-level infrastructure for procedural terrain creation. Following finevox's design philosophy, the engine provides tools and a pipeline framework while game modules define specific terrain, biomes, and features.

**Namespace:** `finevox::worldgen` (nested inside `finevox`, so core types like `BlockPos`, `World`, `BlockTypeId` are accessible without qualification).

**Headers:** `include/finevox/worldgen/`
**Sources:** `src/worldgen/`
**Library:** `libfinevox_worldgen` (shared, links `finevox` PUBLIC)

**Engine provides:**
- Noise library (Perlin, OpenSimplex2, Voronoi, fractal, domain warp)
- Biome framework (registration, selection, blending)
- Generation pipeline (ordered multi-pass column generation)
- Feature system (trees, ore veins, schematic-based structures)
- Data-driven configuration (`.biome`, `.feature`, `.ore` files)

**Game modules provide:**
- Specific biome definitions (climate parameters, block choices)
- Custom terrain shaping (noise configuration per biome)
- Custom features and structures
- Additional generation passes (rivers, dungeons, etc.)
- Custom placement rules

**Integration point:** `World::setColumnGenerator()` — the existing callback invoked when a new column is created.

---

## 27.2 Noise Library

All noise lives in `finevox::worldgen` (VK-independent). Noise functions are deterministic: same seed and coordinates always produce the same value.

### 27.2.1 Base Interfaces

```cpp
class Noise2D {
public:
    virtual ~Noise2D() = default;
    [[nodiscard]] virtual float evaluate(float x, float z) const = 0;
};

class Noise3D {
public:
    virtual ~Noise3D() = default;
    [[nodiscard]] virtual float evaluate(float x, float y, float z) const = 0;
};
```

All noise implementations return values in approximately [-1, 1] range.

### 27.2.2 Seed Derivation

```cpp
class NoiseHash {
public:
    static uint32_t hash2D(int32_t x, int32_t z, uint64_t seed);
    static uint32_t hash3D(int32_t x, int32_t y, int32_t z, uint64_t seed);
    static uint64_t deriveSeed(uint64_t baseSeed, uint64_t salt);
};
```

A world seed derives independent seeds for each noise generator via `deriveSeed(worldSeed, salt)`. Different salts ensure uncorrelated noise patterns for terrain, caves, biomes, etc.

### 27.2.3 Noise Types

| Class | Description | Use Case |
|-------|-------------|----------|
| `PerlinNoise2D/3D` | Classic Perlin gradient noise | General terrain, caves |
| `OpenSimplex2D/3D` | Patent-free simplex alternative | Smoother terrain |
| `VoronoiNoise2D` | Cell-based noise (returns distance + cell ID) | Biome regions |

### 27.2.4 Noise Operations (Composable)

| Class | Description |
|-------|-------------|
| `FBMNoise2D/3D` | Fractal Brownian Motion — stacks octaves for natural detail |
| `RidgedNoise2D/3D` | Ridged multi-fractal — sharp ridges for mountains |
| `BillowNoise2D/3D` | Absolute-value octaves — puffy/billowy appearance |
| `DomainWarp2D/3D` | Evaluates noise at warped coordinates — natural distortion |
| `ScaledNoise2D/3D` | Frequency/amplitude/offset adjustment |
| `ClampedNoise2D/3D` | Clamp output range |
| `CombinedNoise2D/3D` | Combine two sources (add, multiply, min, max, lerp) |

All operations wrap a `Noise2D`/`Noise3D` via `unique_ptr`, enabling arbitrary composition:

```cpp
// Example: warped ridged mountain noise
auto mountains = std::make_unique<DomainWarp2D>(
    std::make_unique<RidgedNoise2D>(
        std::make_unique<OpenSimplex2D>(seed1), 6, 2.0f, 0.5f),
    std::make_unique<OpenSimplex2D>(seed2),
    std::make_unique<OpenSimplex2D>(seed3),
    0.3f  // warp strength
);
float height = mountains->evaluate(worldX * 0.005f, worldZ * 0.005f);
```

### 27.2.5 Convenience Factory

```cpp
namespace NoiseFactory {
    std::unique_ptr<Noise2D> perlinFBM(uint64_t seed, int octaves = 6, float frequency = 0.01f);
    std::unique_ptr<Noise2D> simplexFBM(uint64_t seed, int octaves = 6, float frequency = 0.01f);
    std::unique_ptr<Noise2D> ridgedMountains(uint64_t seed, float frequency = 0.005f);
    std::unique_ptr<Noise2D> warpedTerrain(uint64_t seed, float frequency = 0.008f);
}
```

### 27.2.6 Voronoi Noise

Returns structured results for biome region generation:

```cpp
struct VoronoiResult {
    float distance1;        // Distance to nearest cell center
    float distance2;        // Distance to second-nearest
    glm::vec2 cellCenter;   // Position of nearest cell center
    uint32_t cellId;        // Deterministic ID for nearest cell
};

class VoronoiNoise2D {
public:
    explicit VoronoiNoise2D(uint64_t seed, float cellSize = 256.0f);
    [[nodiscard]] VoronoiResult evaluate(float x, float z) const;
    [[nodiscard]] float evaluateF1(float x, float z) const;
    [[nodiscard]] float evaluateF2MinusF1(float x, float z) const; // Edge detection
};
```

---

## 27.3 Biome System

### 27.3.1 BiomeId

Interned like `BlockTypeId`, using `StringInterner`:

```cpp
struct BiomeId {
    InternedId id = 0;
    [[nodiscard]] static BiomeId fromName(std::string_view name);
    [[nodiscard]] std::string_view name() const;
    constexpr bool operator==(const BiomeId&) const = default;
};
```

### 27.3.2 BiomeProperties

```cpp
struct BiomeProperties {
    BiomeId id;
    std::string displayName;

    // Climate (for selection)
    float temperatureMin = 0.0f, temperatureMax = 1.0f;
    float humidityMin = 0.0f, humidityMax = 1.0f;

    // Terrain shaping
    float baseHeight = 64.0f;
    float heightVariation = 16.0f;
    float heightScale = 1.0f;

    // Surface composition
    BlockTypeId surfaceBlock;       // grass, sand, snow, etc.
    BlockTypeId fillerBlock;        // dirt, sandstone, etc.
    int32_t fillerDepth = 3;
    BlockTypeId stoneBlock;         // stone, deepslate, etc.
    BlockTypeId underwaterBlock;    // sand, gravel, etc.

    // Feature density multipliers
    float treeDensity = 0.0f;
    float oreDensity = 1.0f;
    float decorationDensity = 1.0f;
};
```

### 27.3.3 BiomeRegistry

Thread-safe global singleton. Biomes registered during module `onRegister()` or loaded from `.biome` files.

### 27.3.4 BiomeMap (Selection Algorithm)

Biome assignment uses a two-layer approach:

1. **Voronoi tessellation** divides the world into irregular regions (cell size ~256 blocks)
2. **Climate noise** (low-frequency temperature + humidity) assigns values to each Voronoi cell center
3. **Biome matching**: Each cell's climate values select the biome whose temperature/humidity range best matches

This produces natural, irregular biome boundaries. At borders, blending weights are computed from relative distance to nearby cell centers, enabling smooth height transitions.

```
effectiveHeight(x, z) = sum(biome_i.baseHeight * weight_i)
                      + sum(biome_i.heightVariation * weight_i) * heightNoise(x, z)
```

### 27.3.5 Data File Format (.biome)

Uses ConfigParser format (consistent with `.model` files):

```
# plains.biome
name: Plains
temperature_min: 0.4
temperature_max: 0.7
humidity_min: 0.2
humidity_max: 0.6

base_height: 64
height_variation: 6
height_scale: 1.0

surface: blockgame:grass
filler: blockgame:dirt
filler_depth: 3
stone: blockgame:stone
underwater: blockgame:sand

tree_density: 0.02
ore_density: 1.0
decoration_density: 1.5
```

---

## 27.4 Generation Pipeline

### 27.4.1 GenerationPass Interface

```cpp
enum class GenerationPriority : int32_t {
    TerrainShape   = 1000,
    Surface        = 2000,
    Carving        = 3000,
    Ores           = 4000,
    Structures     = 5000,
    Decoration     = 6000,
    Finalization   = 9000,
};

class GenerationPass {
public:
    virtual ~GenerationPass() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual int32_t priority() const = 0;
    virtual void generate(GenerationContext& ctx) = 0;
    [[nodiscard]] virtual bool needsNeighbors() const { return false; }
};
```

### 27.4.2 GenerationContext

Shared mutable context passed through all passes for a column:

```cpp
struct GenerationContext {
    ChunkColumn& column;
    ColumnPos pos;
    World& world;
    const BiomeMap& biomeMap;
    uint64_t worldSeed;

    // Cached per-column data (populated by TerrainPass, consumed by later passes)
    std::array<int32_t, 256> heightmap{};    // Surface Y per (localX, localZ)
    std::array<BiomeId, 256> biomes{};       // Biome per (localX, localZ)

    // Per-column RNG (seeded from worldSeed + column position)
    [[nodiscard]] uint64_t columnSeed() const;
};
```

### 27.4.3 GenerationPipeline

```cpp
class GenerationPipeline {
public:
    void addPass(std::unique_ptr<GenerationPass> pass);
    bool removePass(std::string_view name);
    bool replacePass(std::unique_ptr<GenerationPass> pass);
    void generateColumn(ChunkColumn& column, World& world, const BiomeMap& biomeMap);
    void setWorldSeed(uint64_t seed);
};
```

Passes sorted by priority. Games call `addPass()` to insert custom passes at any priority level.

### 27.4.4 Standard Passes

| Pass | Priority | What it Does |
|------|----------|-------------|
| **TerrainPass** | 1000 | Samples noise for heightmap. Fills stone below surface. Populates `ctx.heightmap[]` and `ctx.biomes[]`. Uses biome blending for smooth height transitions. |
| **SurfacePass** | 2000 | Reads `ctx.heightmap[]` and `ctx.biomes[]`. Replaces top N layers with biome's surface/filler/stone blocks. |
| **CavePass** | 3000 | 3D noise carving. Cheese caves (large), spaghetti caves (tunnels). Avoids carving within 2 blocks of surface. Updates heightmap if caves open to surface. |
| **OrePass** | 4000 | Places ore blobs at configured depths. Respects biome ore density multiplier. Each ore type has vein size, height range, frequency. |
| **StructurePass** | 5000 | Places multi-block features (trees, buildings) via FeatureSystem. `needsNeighbors() = true` for cross-column structures. |
| **DecorationPass** | 6000 | Places single-block decorations (flowers, tall grass, mushrooms) on surface blocks. |

### 27.4.5 Data Flow Between Passes

```
TerrainPass → writes ctx.heightmap[], ctx.biomes[], fills stone
     ↓
SurfacePass → reads ctx.heightmap[], ctx.biomes[] → replaces surface layers
     ↓
CavePass    → reads ctx.heightmap[] (avoids surface) → removes blocks, updates heightmap
     ↓
OrePass     → reads ctx.heightmap[], ctx.biomes[] → places ore in stone
     ↓
StructurePass → reads ctx.heightmap[] → places trees/structures
     ↓
DecorationPass → reads ctx.heightmap[] → places flowers/grass
```

### 27.4.6 Adding Custom Passes

Games insert passes at any priority. Example: a river pass between surface and caves:

```cpp
class RiverPass : public GenerationPass {
    std::string_view name() const override { return "mymod:rivers"; }
    int32_t priority() const override { return 2500; } // After surface, before caves
    void generate(GenerationContext& ctx) override { /* carve river channels */ }
};

pipeline->addPass(std::make_unique<RiverPass>());
```

---

## 27.5 Feature System

### 27.5.1 Feature Interface

```cpp
enum class FeatureResult { Placed, Skipped, Failed };

struct FeaturePlacementContext {
    World& world;
    BlockPos origin;
    BiomeId biome;
    uint64_t seed;              // Per-placement deterministic seed
    GenerationContext* genCtx;  // Null for runtime placement
};

class Feature {
public:
    virtual ~Feature() = default;
    [[nodiscard]] virtual std::string_view name() const = 0;
    [[nodiscard]] virtual FeatureResult place(FeaturePlacementContext& ctx) = 0;
    [[nodiscard]] virtual BlockPos maxExtent() const { return BlockPos(1, 1, 1); }
};
```

### 27.5.2 Built-In Features

**TreeFeature**: Configurable tree generator.
```cpp
struct TreeConfig {
    BlockTypeId trunkBlock, leavesBlock;
    int32_t minTrunkHeight = 4, maxTrunkHeight = 7;
    int32_t leafRadius = 2;
    bool requiresSoil = true;
};
```

**OreFeature**: Places ore veins using random walk from a center point.
```cpp
struct OreConfig {
    BlockTypeId oreBlock, replaceBlock;
    int32_t veinSize = 8;
    int32_t minHeight = 0, maxHeight = 64;
    int32_t veinsPerChunk = 8;
};
```

**SchematicFeature**: Stamps a loaded `Schematic` (from the schematic system) at a position.

### 27.5.3 Feature Placement Rules

```cpp
struct FeaturePlacement {
    std::string featureName;
    float density = 0.01f;
    int32_t minHeight = 0, maxHeight = 256;
    std::vector<BiomeId> biomes;        // Empty = all biomes
    bool requiresSurface = true;
    BlockTypeId requiredSurface;        // Empty = any solid
};
```

### 27.5.4 FeatureRegistry

Global singleton. Features registered during module `onRegister()`. Placement rules specify how features are distributed during generation.

### 27.5.5 Data File Formats

**`.feature` files** (tree, decoration):
```
# oak_tree.feature
type: tree
trunk: blockgame:oak_log
leaves: blockgame:oak_leaves
min_trunk_height: 4
max_trunk_height: 7
leaf_radius: 2
requires_soil: true
```

**`.ore` files** (ore veins):
```
# iron_ore.ore
block: blockgame:iron_ore
replace: blockgame:stone
vein_size: 8
min_height: 0
max_height: 64
veins_per_chunk: 8
```

---

## 27.6 Cross-Column Features

### 27.6.1 The Problem

A tree placed in column (0,0) at local position (14,70,8) has leaves extending into column (1,0). If columns generate independently, (1,0) would not have those leaves.

### 27.6.2 Solution: Deterministic Re-Computation

Each column deterministically computes what features from neighboring columns would overlap into it.

**Algorithm for column (cx, cz):**
1. For each neighbor column (nx, nz) within feature radius:
2. Compute `neighborSeed = hash(worldSeed, nx, nz, featureSalt)`
3. Using `neighborSeed`, determine which surface positions in (nx, nz) get features (same logic as StructurePass)
4. For each feature position in (nx, nz) whose `maxExtent()` overlaps into (cx, cz):
5. Run the feature placement, but only place blocks that fall within (cx, cz)

**Why this works:**
- Feature placement decisions are purely seed-based (no dependency on neighbor generation state)
- Same seed + position always produces same placement decision
- Fully parallelizable — no shared mutable state, no locks
- Column order doesn't matter

**Trade-off:** Each border column re-evaluates potential features from up to 8 neighbors. Tree position determination is cheap (hash + density check). Actual geometry generation only runs if the feature overlaps.

---

## 27.7 Thread Safety

Column generation runs on background threads (via ColumnManager). The generation system must be thread-safe:

- **Noise objects**: Read-only after construction. Safe for concurrent use.
- **BiomeRegistry**: Populated during module init (single-threaded), read during generation (concurrent). Uses shared_mutex.
- **FeatureRegistry**: Same pattern as BiomeRegistry.
- **GenerationContext**: Per-invocation, not shared. Each column gets its own context.
- **GenerationPipeline**: Passes are read-only during generation (their `generate()` methods write to GenerationContext, not to shared state).
- **Cross-column reads**: `World::getBlock()` for neighbor queries is thread-safe (read-only during generation).

---

## 27.8 Integration with Existing Systems

### 27.8.1 Lighting

After generation, the LightEngine initializes sky light for the column. The `ChunkColumn::recalculateHeightmap()` method updates the heightmap used for sky light propagation. Light-emitting blocks placed during generation (glowstone, lava) trigger block light propagation.

### 27.8.2 Persistence

Generated columns are saved by the existing persistence system. On reload, they are loaded from disk rather than re-generated. The `WorldConfig::seed()` stores the world seed for deterministic re-generation if needed.

### 27.8.3 Module System

Modules register biomes and features during `onRegister()`:

```cpp
void MyGameModule::onRegister(ModuleRegistry& registry) {
    using namespace finevox::worldgen;

    // Register block types (core namespace)
    registry.blocks().registerType("mymod:red_sand", BlockType().setOpaque(true));

    // Register biomes (worldgen namespace)
    BiomeRegistry::global().registerBiome("mymod:red_desert", BiomeProperties{...});

    // Register features (worldgen namespace)
    FeatureRegistry::global().registerFeature(
        std::make_unique<TreeFeature>("mymod:cactus", TreeConfig{...}));
}
```

### 27.8.4 Mesh Rebuilding

Column generation calls `column.setBlock()` which updates block versions. After generation completes, the renderer requests meshes for all subchunks in the new column via the existing `ChunkLoadCallback` → `MeshWorkerPool` pipeline.

---

## 27.9 Schematic System Integration

The schematic system (see [21 - Clipboard and Schematic](21-clipboard-schematic.md)) provides `BlockSnapshot` and `Schematic` for structure templates. During generation:

1. `.schematic` files loaded from `resources/structures/`
2. `SchematicFeature` wraps a `Schematic` with placement rules
3. `StructurePass` invokes features that may stamp schematics into the world
4. `placeSchematic()` handles block placement with rotation and filtering

This enables designers to create structures in-game (via clipboard), save as `.schematic` files, and reference them in `.feature` configs for world generation.

---

[Next: TBD]
