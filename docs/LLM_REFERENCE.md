# finevox LLM Reference

Dense API reference for AI assistants building on finevox. Maximum signal per token.

---

## Architecture

| Library | Namespace | Headers | Sources | Links |
|---------|-----------|---------|---------|-------|
| `finevox` | `finevox::` | `include/finevox/core/` | `src/core/` | GLM, LZ4 |
| `finevox_worldgen` | `finevox::worldgen::` | `include/finevox/worldgen/` | `src/worldgen/` | finevox (PUBLIC) |
| `finevox_render` | `finevox::render::` | `include/finevox/render/` | `src/render/` | finevox, Vulkan, GLFW |

Dependency: `finevox_worldgen` and `finevox_render` both link `finevox` PUBLIC. They do NOT depend on each other.

Nested namespace lookup: code in `finevox::worldgen` can use core types (`BlockPos`, `World`, etc.) unqualified.

---

## Coordinates (`core/position.hpp`)

| Type | Fields | Meaning |
|------|--------|---------|
| `BlockPos` | `int32_t x, y, z` | World-space block position |
| `LocalBlockPos` | `uint8_t x, y, z` | Position within subchunk (0-15) |
| `ChunkPos` | `int32_t x, y, z` | 16x16x16 subchunk position |
| `ColumnPos` | `int32_t x, z` | Column position (no Y) |
| `RegionPos` | `int32_t x, z` | 32x32-column region for persistence |

**Conversions:**
- `ChunkPos::fromBlock(blockPos)` -- block to subchunk
- `ColumnPos::fromBlock(blockPos)` -- block to column
- `ColumnPos::fromChunk(chunkPos)` -- subchunk to column
- `chunkPos.cornerBlockPos()` -- subchunk origin in world coords
- `chunkPos.toWorld(localPos)` -- local to world
- `blockPos.local()` -- world to local within subchunk
- `blockPos.localIndex()` -- Y-major index: `y*256 + z*16 + x`

**Face enum:** `NegX(0)=West, PosX(1)=East, NegY(2)=Down, PosY(3)=Up, NegZ(4)=North, PosZ(5)=South`
- `oppositeFace(f)` -- flip direction
- `faceNormal(f)` -- `array<int32_t, 3>` offset
- `blockPos.neighbor(face)` -- adjacent block

All position types are hashable (`std::hash<>` specialized).

---

## Block System

### BlockTypeId (`core/string_interner.hpp`)

`BlockTypeId` = `InternedId` = `uint32_t`. String-interned via global `StringInterner`.

```cpp
// Intern a name (returns existing ID if already interned)
BlockTypeId id = StringInterner::global().internString("mymod:stone");

// Lookup existing
std::optional<InternedId> id = StringInterner::global().find("mymod:stone");

// Reverse lookup
std::string_view name = StringInterner::global().getString(id);
```

### BlockType (`core/block_type.hpp`)

Static properties. Builder pattern (all setters return `BlockType&`):

| Setter | Default | Effect |
|--------|---------|--------|
| `setCollisionShape(shape)` | `FULL_BLOCK` | Physics collision (precomputes 24 rotations) |
| `setHitShape(shape)` | falls back to collision | Raycasting/selection |
| `setShape(shape)` | -- | Sets both collision and hit |
| `setNoCollision()` | -- | Pass-through (air, water) |
| `setNoHit()` | -- | Can't be selected/mined |
| `setOpaque(bool)` | `true` | Blocks light, enables face culling |
| `setTransparent(bool)` | `false` | Render sorting |
| `setLightEmission(0-15)` | `0` | Light source level |
| `setLightAttenuation(1-15)` | `15` (opaque) / `1` (transparent) | Light reduction when passing through |
| `setBlocksSkyLight(bool)` | `true` (opaque) | Affects heightmap |
| `setHardness(float)` | `1.0` | Mining time factor |
| `setWantsGameTicks(bool)` | `false` | Auto-register for game ticks |
| `setHasCustomMesh(bool)` | `false` | Exclude from greedy meshing |

### BlockRegistry (`core/block_type.hpp`)

Singleton: `BlockRegistry::global()`

| Method | Signature | Notes |
|--------|-----------|-------|
| `registerType` | `bool (BlockTypeId id, BlockType)` | Returns false if already registered |
| `registerType` | `bool (string_view name, BlockType)` | Auto-interns name |
| `getType` | `const BlockType& (BlockTypeId)` | Returns `defaultType()` if missing |
| `getType` | `const BlockType& (string_view)` | Returns `defaultType()` if missing |
| `hasType` | `bool (BlockTypeId)` | |
| `registerHandler` | `bool (string_view, unique_ptr<BlockHandler>)` | |
| `registerHandlerFactory` | `bool (string_view, function<unique_ptr<BlockHandler>()>)` | Lazy creation |
| `getHandler` | `BlockHandler* (BlockTypeId)` | nullptr if none |
| `getHandler` | `BlockHandler* (string_view)` | nullptr if none |
| `defaultType()` | `static const BlockType&` | Full solid block |
| `airType()` | `static const BlockType&` | No collision, no hit |

**Name format:** `"namespace:localname"` (e.g., `"blockgame:stone"`)

Static helpers: `isValidNamespacedName()`, `getNamespace()`, `getLocalName()`, `makeQualifiedName(ns, local)`

---

## World (`core/world.hpp`)

### Block Access

| Method | When to Use |
|--------|-------------|
| `getBlock(BlockPos) -> BlockTypeId` | Read block (returns AIR if unloaded) |
| `setBlock(pos, type)` | Direct set, NO events/lighting/handler callbacks. For bulk init, terrain gen, chunk loading. |
| `placeBlock(pos, type)` | Event-driven placement. Triggers handlers, neighbor updates, lighting. Requires UpdateScheduler. |
| `breakBlock(pos)` | Event-driven breaking. Same as above. |
| `placeBlocks(vector<pair<BlockPos,BlockTypeId>>)` | Bulk event-driven placement |
| `breakBlocks(vector<BlockPos>)` | Bulk event-driven breaking |

### Column Management

| Method | Returns | Notes |
|--------|---------|-------|
| `getColumn(ColumnPos)` | `ChunkColumn*` | nullptr if not loaded |
| `hasColumn(ColumnPos)` | `bool` | |
| `getOrCreateColumn(ColumnPos)` | `ChunkColumn&` | Creates if missing, triggers column generator |
| `removeColumn(ColumnPos)` | `bool` | |
| `forEachColumn(callback)` | -- | `function<void(ColumnPos, ChunkColumn&)>` |
| `columnCount()` | `size_t` | |
| `totalNonAirBlocks()` | `size_t` | |
| `setColumnGenerator(callback)` | -- | Called when new columns created |

### SubChunk Access

| Method | Returns |
|--------|---------|
| `getSubChunk(ChunkPos)` | `SubChunk*` (nullptr if empty/unloaded) |
| `getSubChunkShared(ChunkPos)` | `shared_ptr<SubChunk>` |
| `getAllSubChunkPositions()` | `vector<ChunkPos>` |

### Integration Points

| Method | Notes |
|--------|-------|
| `setLightEngine(LightEngine*)` | Enables automatic lighting |
| `setUpdateScheduler(UpdateScheduler*)` | Required for `placeBlock`/`breakBlock` |
| `registerForceLoader(pos, radius)` | Prevent chunk unloading |
| `unregisterForceLoader(pos)` | |
| `clear()` | Empty entire world |

---

## Block Handlers (`core/block_handler.hpp`)

### BlockHandler Interface

All methods have default no-op implementations. Override what you need.

| Virtual Method | When Called | Context |
|----------------|------------|---------|
| `onPlace(BlockContext&)` | Block placed via `placeBlock()` | `previousType()` available |
| `onBreak(BlockContext&)` | Block broken via `breakBlock()` | `previousType()` = broken type |
| `onTick(BlockContext&, TickType)` | Scheduled/repeat/random tick fires | |
| `onNeighborChanged(BlockContext&, Face)` | Adjacent block changed | `face` = which neighbor |
| `onBlockUpdate(BlockContext&)` | Redstone-like propagation | |
| `onUse(BlockContext&, Face) -> bool` | Right-click interaction | Return true if handled |
| `onHit(BlockContext&, Face) -> bool` | Left-click | Return true if handled |
| `onRepaint(BlockContext&)` | Mesh update requested | |

### BlockContext (`core/block_handler.hpp`)

Ephemeral context passed to all handler callbacks.

| Category | Method | Returns |
|----------|--------|---------|
| Position | `pos()` | `BlockPos` |
| | `localPos()` | `LocalBlockPos` |
| | `chunkPos()` | `ChunkPos` |
| World | `world()` | `World&` |
| | `subChunk()` | `SubChunk&` |
| Block state | `blockType()` | `const BlockType&` |
| | `type()` | `BlockTypeId` |
| | `rotation()` | `Rotation` |
| | `setRotation(Rotation)` | |
| | `isAir()`, `isOpaque()`, `isTransparent()` | `bool` |
| Light | `skyLight()`, `blockLight()` | `uint8_t` (0-15) |
| | `combinedLight()` | `uint8_t` (max of sky+block) |
| Neighbors | `getNeighbor(Face)` | `BlockTypeId` |
| | `notifyNeighbors()` | Triggers neighbor callbacks |
| Modify | `setBlock(BlockTypeId)` | Changes this block |
| | `requestMeshRebuild()` | |
| | `markDirty()` | Flags for persistence |
| Ticks | `scheduleTick(ticksFromNow)` | Schedule future tick |
| | `setRepeatTickInterval(interval)` | Recurring ticks |
| Previous | `previousType()` | `BlockTypeId` (for place/break) |
| Data | `data()` | `DataContainer*` (nullable) |
| | `getOrCreateData()` | `DataContainer&` |

### TickType Enum
`Scheduled` | `Repeat` | `Random`

---

## Event System (`core/block_event.hpp`, `core/event_queue.hpp`)

### EventType Enum (full list)

`BlockPlaced`, `BlockBroken`, `BlockChanged`, `TickGame`, `TickScheduled`, `TickRepeat`, `TickRandom`, `NeighborChanged`, `BlockUpdate`, `PlayerUse`, `PlayerHit`, `PlayerPosition`, `PlayerLook`, `PlayerJump`, `PlayerStartSprint`, `PlayerStopSprint`, `PlayerStartSneak`, `PlayerStopSneak`, `ChunkLoaded`, `ChunkUnloaded`, `RepaintRequested`

### BlockEvent Factory Methods

| Factory | Key Fields |
|---------|-----------|
| `blockPlaced(pos, newType, oldType, rotation)` | |
| `blockBroken(pos, oldType)` | |
| `blockChanged(pos, oldType, newType)` | |
| `neighborChanged(pos, face)` | |
| `tick(pos, tickType)` | |
| `playerUse(pos, face)` / `playerHit(pos, face)` | |
| `blockUpdate(pos)` | Redstone-like propagation |

### UpdateScheduler (`core/event_queue.hpp`)

| Method | Notes |
|--------|-------|
| `setTickConfig(TickConfig)` | Configure tick intervals |
| `scheduleTick(pos, ticksFromNow, type)` | Schedule a block tick |
| `cancelScheduledTicks(pos)` | |
| `processEvents() -> size_t` | Process until stable |
| `advanceGameTick()` | Generate game/random ticks |
| `pushExternalEvent(BlockEvent)` | Thread-safe injection |

### TickConfig

| Field | Default | Meaning |
|-------|---------|---------|
| `gameTickIntervalMs` | `50` | ms between game ticks |
| `randomTicksPerSubchunk` | `3` | Random ticks per subchunk per game tick |
| `randomSeed` | `0` (=system) | RNG seed |
| `gameTicksEnabled` | `true` | |
| `randomTicksEnabled` | `true` | |

---

## Physics (`core/physics.hpp`)

### AABB

| Method | Signature |
|--------|-----------|
| Constructor | `AABB(glm::vec3 min, glm::vec3 max)` |
| `forBlock(x,y,z)` | Static: unit cube at block position |
| `fromHalfExtents(center, extents)` | Static |
| `intersects(other)` | `bool` |
| `contains(point)` | `bool` |
| `sweepCollision(other, velocity, outNormal)` | `float` (toi, 1.0 = no hit) |
| `rayIntersect(origin, dir, tMin, tMax, outFace)` | `bool` |
| `expanded(amount)`, `translated(offset)` | `AABB` |

### CollisionShape

Collection of AABBs in [0,1] local space. Static presets:

| Preset | Description |
|--------|-------------|
| `CollisionShape::NONE` | No collision |
| `CollisionShape::FULL_BLOCK` | Unit cube |
| `CollisionShape::HALF_SLAB_BOTTOM` | Lower half |
| `CollisionShape::HALF_SLAB_TOP` | Upper half |
| `CollisionShape::FENCE_POST` | Thin center post |
| `CollisionShape::THIN_FLOOR` | Carpet-like |

Methods: `addBox(aabb)`, `atPosition(BlockPos)`, `transformed(Rotation)`, `computeRotations(base)`

### PhysicsSystem

Constructor: `PhysicsSystem(BlockShapeProvider provider)` or `PhysicsSystem(World& world)` (uses `createBlockShapeProvider`)

| Method | Returns |
|--------|---------|
| `moveBody(PhysicsBody&, desiredMovement)` | `glm::vec3` actual movement |
| `applyGravity(body, deltaTime)` | |
| `update(body, deltaTime)` | Combined gravity + move |
| `raycast(origin, dir, maxDist, mode)` | `RaycastResult` |
| `setGravity(float)` | Default: `-32.0` |

### RaycastResult

Fields: `bool hit`, `BlockPos blockPos`, `Face face`, `glm::vec3 hitPoint`, `float distance`

### RaycastMode

`Collision` (physics shapes) | `Interaction` (hit shapes) | `Both`

---

## World Generation (`finevox::worldgen`)

### Noise Types (`worldgen/noise.hpp`)

| Class | Dim | Description |
|-------|-----|-------------|
| `PerlinNoise2D/3D` | 2D/3D | Classic Perlin gradient noise |
| `OpenSimplex2D/3D` | 2D/3D | Patent-free simplex alternative |

All return approximately `[-1, 1]`. Constructed with `(uint64_t seed)`.

### Noise Operations (`worldgen/noise_ops.hpp`)

All wrap `unique_ptr<Noise2D/3D>`, composable arbitrarily.

| Class | Constructor Args | Effect |
|-------|-----------------|--------|
| `FBMNoise2D/3D` | `(base, octaves=6, lacunarity=2.0, persistence=0.5)` | Fractal Brownian Motion |
| `RidgedNoise2D/3D` | `(base, octaves=6, lacunarity=2.0, persistence=0.5)` | Sharp ridges |
| `BillowNoise2D/3D` | `(base, octaves=6, lacunarity=2.0, persistence=0.5)` | Puffy/billowy |
| `DomainWarp2D/3D` | `(base, warpX, warpZ, strength)` | Distorted coordinates |
| `ScaledNoise2D/3D` | `(base, freqScale, ampScale, offset)` | Frequency/amplitude adjust |
| `ClampedNoise2D/3D` | `(base, min, max)` | Clamp output |
| `CombinedNoise2D/3D` | `(a, b, CombineOp)` | Add/Multiply/Min/Max/Lerp |

### Voronoi (`worldgen/noise_voronoi.hpp`)

```cpp
VoronoiNoise2D(uint64_t seed, float cellSize = 256.0f);
```

Returns `VoronoiResult { float distance1, distance2; glm::vec2 cellCenter; uint32_t cellId; }`

Convenience: `evaluateF1(x, z)`, `evaluateF2MinusF1(x, z)` (edge detection)

### Seed Utilities (`worldgen/noise.hpp`)

```cpp
NoiseHash::deriveSeed(baseSeed, salt)  // Independent sub-seeds
NoiseHash::hash2D(x, z, seed)         // Position hash
```

### BiomeId (`worldgen/biome.hpp`)

`BiomeId { InternedId id; }` -- interned via `StringInterner`, same as `BlockTypeId`.

`BiomeId::fromName("demo:plains")`, `biomeId.name()`

### BiomeProperties (`worldgen/biome.hpp`)

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | `BiomeId` | | |
| `displayName` | `string` | | |
| `temperatureMin/Max` | `float` | 0.0/1.0 | Climate range |
| `humidityMin/Max` | `float` | 0.0/1.0 | Climate range |
| `baseHeight` | `float` | 64.0 | Terrain height |
| `heightVariation` | `float` | 16.0 | Height noise amplitude |
| `heightScale` | `float` | 1.0 | Height multiplier |
| `surfaceBlock` | `string` | "grass" | Top layer block name |
| `fillerBlock` | `string` | "dirt" | Sub-surface block name |
| `fillerDepth` | `int32_t` | 3 | Filler layer thickness |
| `stoneBlock` | `string` | "stone" | Base rock |
| `underwaterBlock` | `string` | "sand" | Below water |
| `treeDensity` | `float` | 0.0 | Trees per surface block |
| `oreDensity` | `float` | 1.0 | Ore frequency multiplier |
| `decorationDensity` | `float` | 1.0 | Decoration frequency multiplier |

### BiomeRegistry (`worldgen/biome.hpp`)

Singleton: `BiomeRegistry::global()`

| Method | Signature |
|--------|-----------|
| `registerBiome` | `void (string_view name, BiomeProperties)` |
| `getBiome` | `const BiomeProperties* (BiomeId)` -- nullptr if missing |
| `getBiome` | `const BiomeProperties* (string_view)` |
| `allBiomes` | `vector<BiomeId>` |
| `selectBiome` | `BiomeId (float temp, float humidity)` |
| `size` | `size_t` |

### BiomeMap (`worldgen/biome_map.hpp`)

```cpp
BiomeMap(uint64_t worldSeed, const BiomeRegistry& registry);
BiomeId getBiome(float worldX, float worldZ) const;
float getBlendedHeight(float worldX, float worldZ, const Noise2D& heightNoise) const;
```

Uses Voronoi tessellation + climate noise for natural biome boundaries.

### Generation Pipeline (`worldgen/world_generator.hpp`)

**GenerationPriority enum:** `TerrainShape(1000)`, `Surface(2000)`, `Carving(3000)`, `Ores(4000)`, `Structures(5000)`, `Decoration(6000)`, `Finalization(9000)`

**GenerationPass interface:**
```cpp
virtual string_view name() const = 0;
virtual int32_t priority() const = 0;
virtual void generate(GenerationContext& ctx) = 0;
virtual bool needsNeighbors() const { return false; }
```

**GenerationContext fields:**
`ChunkColumn& column`, `ColumnPos pos`, `World& world`, `const BiomeMap& biomeMap`, `uint64_t worldSeed`, `array<int32_t, 256> heightmap`, `array<BiomeId, 256> biomes`

Helper: `columnSeed()`, `hmIndex(localX, localZ)` = `localX * 16 + localZ`

**GenerationPipeline:**
```cpp
pipeline.addPass(make_unique<MyPass>());
pipeline.removePass("core:caves");
pipeline.replacePass(make_unique<MyCustomCaves>());
pipeline.setWorldSeed(42);
pipeline.generateColumn(column, world, biomeMap);
```

### Standard Passes (`worldgen/generation_passes.hpp`)

| Class | Name | Priority | Constructor | Effect |
|-------|------|----------|-------------|--------|
| `TerrainPass` | `core:terrain` | 1000 | `(worldSeed)` | Fill stone, populate heightmap/biomes |
| `SurfacePass` | `core:surface` | 2000 | `()` | Replace top layers with biome blocks |
| `CavePass` | `core:caves` | 3000 | `(worldSeed)` | Cheese + spaghetti caves |
| `OrePass` | `core:ores` | 4000 | `()` | Place ore veins from FeatureRegistry |
| `StructurePass` | `core:structures` | 5000 | `()` | Trees, buildings (needsNeighbors=true) |
| `DecorationPass` | `core:decoration` | 6000 | `()` | Single-block surface decorations |

### Feature System (`worldgen/feature.hpp`)

**Feature interface:**
```cpp
virtual string_view name() const = 0;
virtual FeatureResult place(FeaturePlacementContext& ctx) = 0;
virtual BlockPos maxExtent() const { return {1,1,1}; }
```

**FeatureResult enum:** `Placed`, `Skipped`, `Failed`

**FeaturePlacementContext:** `World& world`, `BlockPos origin`, `BiomeId biome`, `uint64_t seed`, `GenerationContext* genCtx`

### Built-in Features

| Feature | Config Struct | Key Fields |
|---------|--------------|------------|
| `TreeFeature` | `TreeConfig` | `trunkBlock`, `leavesBlock`, `minTrunkHeight(4)`, `maxTrunkHeight(7)`, `leafRadius(2)`, `requiresSoil(true)` |
| `OreFeature` | `OreConfig` | `oreBlock`, `replaceBlock`, `veinSize(8)`, `minHeight(0)`, `maxHeight(64)`, `veinsPerChunk(8)` |
| `SchematicFeature` | -- | Stamps a `Schematic` at position |

### FeatureRegistry (`worldgen/feature_registry.hpp`)

Singleton: `FeatureRegistry::global()`

| Method | Signature |
|--------|-----------|
| `registerFeature` | `void (unique_ptr<Feature>)` |
| `getFeature` | `Feature* (string_view name)` |
| `addPlacement` | `void (FeaturePlacement)` |
| `placements` | `const vector<FeaturePlacement>&` |

**FeaturePlacement struct:** `featureName`, `density(0.01)`, `minHeight(0)`, `maxHeight(256)`, `vector<BiomeId> biomes` (empty=all), `requiresSurface(true)`, `BlockTypeId requiredSurface` (empty=any)

### Data-Driven Loading

```cpp
BiomeLoader::loadDirectory(dirPath, namePrefix) -> size_t  // Loads all .biome files
FeatureLoader::loadDirectory(dirPath, namePrefix) -> size_t // Loads all .feature/.ore files
```

### Data File Formats

**.biome file:**
```
name: Plains
temperature_min: 0.4
temperature_max: 0.7
humidity_min: 0.2
humidity_max: 0.6
base_height: 64
height_variation: 6
surface: grass
filler: dirt
filler_depth: 3
stone: stone
underwater: sand
tree_density: 0.02
```

**.feature file (tree):**
```
type: tree
trunk: oak_log
leaves: oak_leaves
min_trunk_height: 4
max_trunk_height: 7
leaf_radius: 2
requires_soil: true
```

**.ore file:**
```
block: iron_ore
replace: stone
vein_size: 8
min_height: 0
max_height: 64
veins_per_chunk: 8
```

---

## Block Models (`core/block_model.hpp`, `core/block_model_loader.hpp`)

### .model File

```
include: base/slab
texture: stone
rotation_set: vertical
```

Fields: `include`, `texture`, `geometry` (path to .geom), `collision` (path to .collision), `rotation_set` (None/Vertical/Horizontal/HorizontalFlip/All)

### .geom File

```
face:top:
    0 0.5 0  1 0.5 0  1 0.5 1  0 0.5 1
    0 0  1 0  1 1  0 1
face:bottom:
    0 0 0  1 0 0  1 0 1  0 0 1
    0 0  1 0  1 1  0 1
```

Each `face:<name>:` block has vertex positions (4 verts x 3 coords) then UV coords (4 verts x 2 coords).

Face name aliases: `west/w/-x`=0, `east/e/+x`=1, `down/bottom/-y`=2, `up/top/+y`=3, `north/n/-z`=4, `south/s/+z`=5

### .collision File

```
box:
    0 0 0  1 0.5 1
```

Each `box:` block has min/max corners in [0,1] local space.

### RotationSet Values

`None` (identity only), `Vertical` (4 Y-axis rotations), `Horizontal` (8: 4 Y-axis x 2 X-axis), `HorizontalFlip` (16: + Z-axis flips), `All` (24 rotations), `Custom`

---

## Persistence

### Serialization (`core/serialization.hpp`)

CBOR (RFC 8949) format. `SubChunkSerializer::serialize()/deserialize()`, `ColumnSerializer::serialize()/deserialize()`.

### RegionFile (`core/region_file.hpp`)

32x32 columns per region file. `RegionFile(path)`, `readColumn(localX, localZ)`, `writeColumn(localX, localZ, data)`.

### IOManager (`core/io_manager.hpp`)

Async persistence. `IOManager(basePath)`, `requestSave(ColumnPos, data)`, `requestLoad(ColumnPos, callback)`. Background thread processes queue.

---

## Rendering (`finevox::render`)

### WorldRenderer (`render/world_renderer.hpp`)

Coordinates mesh generation, LOD, GPU upload.

### TextureManager (`render/texture_manager.hpp`)

Loads textures, builds atlas.

### BlockAtlas (`render/block_atlas.hpp`)

UV coordinate mapping for block face textures.

### SubChunkView (`render/subchunk_view.hpp`)

Read-only accessor for neighbor subchunk data during mesh generation.

---

## Module System (`core/module.hpp`)

### GameModule Interface

| Virtual | Required | Meaning |
|---------|----------|---------|
| `name() -> string_view` | Yes | Module ID + namespace prefix |
| `version() -> string_view` | Yes | Version string |
| `dependencies() -> vector<string_view>` | No | Required modules |
| `onLoad(ModuleRegistry&)` | No | Post-dependency init |
| `onRegister(ModuleRegistry&)` | Yes | Register blocks/entities/items |
| `onUnload()` | No | Cleanup |

### ModuleRegistry

| Method | Returns |
|--------|---------|
| `moduleNamespace()` | `string_view` |
| `blocks()` | `BlockRegistry&` |
| `entities()` | `EntityRegistry&` |
| `items()` | `ItemRegistry&` |
| `qualifiedName(localName)` | `string` (e.g., `"mymod:stone"`) |
| `log/warn/error(msg)` | |

### ModuleLoader

```cpp
ModuleLoader loader;
loader.load("path/to/module.so");
loader.registerBuiltin(make_unique<MyModule>());
loader.initializeAll(blocks, entities, items);
// ... game runs ...
loader.shutdownAll();
```

### Shared Object Export

```cpp
FINEVOX_MODULE(MyModuleClass)
// Expands to: extern "C" GameModule* finevox_create_module() { return new MyModuleClass(); }
```

---

## Config System (`core/config_parser.hpp`)

### Format

```
# Comment
key: value
key:suffix: value
key:suffix:
    1.0 2.0 3.0      # data line (parsed as floats)
    4.0 5.0 6.0
include: other_file   # include directive
```

### ConfigDocument API

| Method | Returns |
|--------|---------|
| `get(key)` | `const ConfigEntry*` (last match, nullptr if missing) |
| `get(key, suffix)` | `const ConfigEntry*` |
| `getString(key, default)` | `string_view` |
| `getFloat(key, default)` | `float` |
| `getInt(key, default)` | `int` |
| `getBool(key, default)` | `bool` |
| `getAll(key)` | `vector<const ConfigEntry*>` (all entries with key) |
| `entries()` | `const vector<ConfigEntry>&` |

### ConfigEntry

Fields: `string key`, `string suffix`, `ConfigValue value`, `vector<vector<float>> dataLines`

### ConfigValue

`asString()`, `asStringOwned()`, `asFloat(default)`, `asInt(default)`, `asBool(default)`, `asNumbers() -> vector<float>`

### ConfigParser

```cpp
ConfigParser parser;
parser.setIncludeResolver([](const string& path) -> string { ... });
auto doc = parser.parseFile("path/to/file.conf");
auto doc2 = parser.parseString("key: value");
```

---

## Common Patterns

| Pattern | Example |
|---------|---------|
| Global singletons | `BlockRegistry::global()`, `BiomeRegistry::global()`, `FeatureRegistry::global()`, `StringInterner::global()` |
| Builder pattern | `BlockType().setOpaque(true).setHardness(1.5f).setLightEmission(12)` |
| Data-driven loading | `BiomeLoader::loadDirectory(dir, prefix)`, `FeatureLoader::loadDirectory(dir, prefix)` |
| Namespaced names | `"namespace:localname"` everywhere (blocks, biomes, features, modules) |
| Internal vs external API | `setBlock()` = bulk/init, `placeBlock()` = event-driven gameplay |
| Forward decl across namespaces | Core types in separate `namespace finevox { class World; }` block, NOT inside `finevox::worldgen { }` |

---

## Complete Example: World Generation Setup

```cpp
#include "finevox/core/world.hpp"
#include "finevox/worldgen/biome_loader.hpp"
#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/feature_registry.hpp"
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"
#include "finevox/worldgen/biome_map.hpp"

using namespace finevox;
using namespace finevox::worldgen;

void generateWorld(World& world, const std::string& resourceDir) {
    uint64_t seed = 42;

    // Load data files
    BiomeLoader::loadDirectory(resourceDir + "/biomes", "demo");
    FeatureLoader::loadDirectory(resourceDir + "/features", "demo");

    // Configure feature placement
    FeaturePlacement trees;
    trees.featureName = "demo:oak_tree";
    trees.density = 0.02f;
    trees.requiresSurface = true;
    FeatureRegistry::global().addPlacement(trees);

    // Build pipeline
    GenerationPipeline pipeline;
    pipeline.setWorldSeed(seed);
    pipeline.addPass(std::make_unique<TerrainPass>(seed));
    pipeline.addPass(std::make_unique<SurfacePass>());
    pipeline.addPass(std::make_unique<CavePass>(seed));
    pipeline.addPass(std::make_unique<OrePass>());
    pipeline.addPass(std::make_unique<StructurePass>());
    pipeline.addPass(std::make_unique<DecorationPass>());

    // Generate columns
    BiomeMap biomeMap(seed, BiomeRegistry::global());
    for (int32_t cx = -3; cx < 3; ++cx)
        for (int32_t cz = -3; cz < 3; ++cz) {
            auto& col = world.getOrCreateColumn(ColumnPos(cx, cz));
            pipeline.generateColumn(col, world, biomeMap);
        }
}
```
