# finevox User Guide

A practical guide to building games with the finevox voxel engine.

---

## 1. What is finevox?

finevox is a game-agnostic voxel engine. It provides the data structures, rendering, physics, persistence, and world generation infrastructure needed to build voxel games. Games are loaded as modules (shared objects) that register block types, behaviors, biomes, and features.

### Three Libraries

finevox is split into three shared libraries:

| Library | Namespace | What it provides |
|---------|-----------|-----------------|
| `libfinevox` | `finevox::` | Core engine: world, blocks, physics, events, persistence, mesh generation |
| `libfinevox_worldgen` | `finevox::worldgen::` | Procedural generation: noise, biomes, features, generation pipeline |
| `libfinevox_render` | `finevox::render::` | Vulkan rendering: world renderer, texture atlas, LOD |

`finevox_worldgen` and `finevox_render` both depend on `finevox` (core) but are independent of each other. Your game links whichever libraries it needs.

### Architecture

```
Game Modules (.so/.dll)
        |
   finevox Engine
   +-- libfinevox (core)
   |   +-- World, SubChunk, BlockType, Physics
   |   +-- Persistence, Events, Mesh generation
   |   +-- Block Models, Module Loader
   +-- libfinevox_worldgen
   |   +-- Noise, Biomes, Features, Schematics
   |   +-- Generation Pipeline
   +-- libfinevox_render
       +-- WorldRenderer, TextureManager
       +-- SubChunkView, BlockAtlas
        |
   FineStructureVK (Vulkan wrapper)
        |
   Vulkan / GLFW / GLM
```

---

## 2. Building and Linking

### Prerequisites

- C++20 compiler (GCC 11+, Clang 14+, MSVC 2022+)
- CMake 3.20+
- For rendering: Vulkan SDK, GLFW

### CMake Integration

Add finevox as a subdirectory or use `FetchContent`:

```cmake
add_subdirectory(path/to/FineStructureVoxel)

# Link the libraries you need
target_link_libraries(my_game PRIVATE
    finevox           # Core (always needed)
    finevox_worldgen  # World generation (optional)
    finevox_render    # Vulkan rendering (optional, requires FINEVOX_BUILD_RENDER=ON)
)
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `FINEVOX_BUILD_TESTS` | `ON` | Build unit tests |
| `FINEVOX_BUILD_RENDER` | `OFF` | Build Vulkan render module |
| `FINEVOX_BUILD_EXAMPLES` | `ON` | Build render demo (when render is enabled) |
| `FINEVK_DIR` | `../FineStructureVK` | Path to FineStructureVK |

GLM and LZ4 are fetched automatically via CMake `FetchContent`.

---

## 3. Core Concepts

### Coordinate System

The world is divided into:

- **Blocks** -- the basic unit. Each has a position (`BlockPos { int32_t x, y, z }`).
- **SubChunks** -- 16x16x16 cubes of blocks. Position: `ChunkPos { int32_t x, y, z }`.
- **Columns** -- full-height stacks of subchunks at an (X, Z) position. Position: `ColumnPos { int32_t x, z }`.

Conversions:
```cpp
BlockPos block(100, 64, -200);
ChunkPos chunk = ChunkPos::fromBlock(block);     // (6, 4, -13)
ColumnPos col = ColumnPos::fromBlock(block);      // (6, -13)
LocalBlockPos local = block.local();              // (4, 0, 8)
BlockPos back = chunk.toWorld(local);             // (100, 64, -200)
```

Block index within a subchunk: `y * 256 + z * 16 + x` (Y-major layout).

### Block Types

Block types are identified by string-interned IDs. The naming convention is `"namespace:localname"`:

```cpp
#include "finevox/core/string_interner.hpp"

// Intern a block name (creates the ID if it doesn't exist)
BlockTypeId stoneId = StringInterner::global().internString("mymod:stone");

// Look up an existing ID
auto id = StringInterner::global().find("mymod:stone"); // std::optional<InternedId>
```

Each subchunk uses a palette to map block IDs to compact indices, enabling efficient storage and compression.

### The World

The `World` class is the central container for all block data:

```cpp
#include "finevox/core/world.hpp"
using namespace finevox;

World world;

// Create a column and place blocks
auto& col = world.getOrCreateColumn(ColumnPos(0, 0));
world.setBlock(BlockPos(0, 0, 0), stoneId);

// Read blocks
BlockTypeId type = world.getBlock(BlockPos(0, 0, 0));
```

**Important distinction:**
- `setBlock()` -- Direct block placement. No events, no lighting updates, no handler callbacks. Use for terrain generation, chunk loading, and bulk initialization.
- `placeBlock()` / `breakBlock()` -- Event-driven. Triggers handler callbacks (`onPlace`, `onBreak`), neighbor updates, and lighting recalculation. Use for gameplay actions. Requires an `UpdateScheduler` to be set.

---

## 4. Creating a Game Module

Modules are the primary way to add game content to finevox. A module registers block types, handlers, entities, and items.

### Minimal Module

```cpp
#include "finevox/core/module.hpp"
#include "finevox/core/block_type.hpp"

using namespace finevox;

class MyGameModule : public GameModule {
public:
    std::string_view name() const override { return "mygame"; }
    std::string_view version() const override { return "1.0.0"; }

    void onRegister(ModuleRegistry& registry) override {
        // Register a simple opaque block
        registry.blocks().registerType("mygame:stone",
            BlockType().setOpaque(true).setHardness(2.0f));

        // Register a light-emitting block
        registry.blocks().registerType("mygame:glowstone",
            BlockType().setOpaque(true).setLightEmission(15));

        // Register a transparent block
        registry.blocks().registerType("mygame:glass",
            BlockType().setTransparent(true).setOpaque(false)
                       .setLightAttenuation(1));
    }
};

// Export from shared object
FINEVOX_MODULE(MyGameModule)
```

### Module Lifecycle

1. `ModuleLoader::load("path/to/module.so")` -- opens the shared object
2. Dependencies resolved via topological sort
3. `onLoad(registry)` -- called after dependencies are loaded
4. `onRegister(registry)` -- register blocks, entities, items
5. Game runs...
6. `onUnload()` -- cleanup in reverse order

### Loading Modules

```cpp
ModuleLoader loader;
loader.registerBuiltin(std::make_unique<MyGameModule>());  // Built-in module
loader.load("mods/extra_blocks.so");                        // From shared object
loader.initializeAll(BlockRegistry::global(), entityReg, itemReg);
```

---

## 5. Registering Block Types

`BlockType` uses a builder pattern -- chain setters to configure properties:

```cpp
BlockType()
    .setOpaque(true)              // Blocks light, enables face culling
    .setHardness(1.5f)            // Mining time factor
    .setLightEmission(12)         // Light source (0-15)
    .setShape(CollisionShape::FULL_BLOCK)  // Physics + raycasting shape
    .setWantsGameTicks(true)      // Receive periodic tick events
    .setHasCustomMesh(true)       // Non-cube geometry
```

### Collision vs Hit Shapes

finevox separates physics collision from raycasting:

- **Collision shape** -- used for entity movement and physics. Set with `setCollisionShape()`.
- **Hit shape** -- used for block selection and mining. Set with `setHitShape()`. Falls back to collision shape if not set.
- `setShape()` -- sets both to the same value.

Predefined shapes are available:

```cpp
CollisionShape::FULL_BLOCK        // Standard cube
CollisionShape::HALF_SLAB_BOTTOM  // Lower half slab
CollisionShape::HALF_SLAB_TOP     // Upper half slab
CollisionShape::FENCE_POST        // Thin center post
CollisionShape::THIN_FLOOR        // Carpet-like
CollisionShape::NONE              // No collision
```

All shapes automatically precompute 24 rotated variants for O(1) lookup.

---

## 6. Block Behavior (Handlers)

Static properties go in `BlockType`. Dynamic behavior goes in `BlockHandler`:

```cpp
#include "finevox/core/block_handler.hpp"

class TNTHandler : public BlockHandler {
public:
    void onPlace(BlockContext& ctx) override {
        // Start a fuse timer when placed
        ctx.scheduleTick(40);  // 40 ticks = 2 seconds at 20 TPS
    }

    void onTick(BlockContext& ctx, TickType type) override {
        if (type == TickType::Scheduled) {
            // Explode! Replace with air
            ctx.setBlock(StringInterner::global().internString("air"));
            // Notify neighbors
            ctx.notifyNeighbors();
        }
    }

    bool onUse(BlockContext& ctx, Face face) override {
        // Right-click to defuse
        ctx.scheduleTick(0);  // Cancel by setting to 0
        return true;
    }
};

// Register in your module's onRegister():
registry.blocks().registerHandler("mygame:tnt", std::make_unique<TNTHandler>());
```

### What BlockContext Provides

When a handler callback fires, `BlockContext` gives you access to:

- **Position**: `pos()`, `localPos()`, `chunkPos()`
- **Block state**: `blockType()`, `type()`, `rotation()`, `isAir()`, `isOpaque()`
- **Light**: `skyLight()`, `blockLight()`, `combinedLight()`
- **Neighbors**: `getNeighbor(Face)` -- returns the `BlockTypeId` of an adjacent block
- **Modification**: `setBlock(type)`, `setRotation(r)`, `requestMeshRebuild()`, `markDirty()`
- **Scheduling**: `scheduleTick(delay)`, `setRepeatTickInterval(interval)`
- **Extra data**: `data()` (returns `DataContainer*`, null if none), `getOrCreateData()`
- **Previous state**: `previousType()` (available in `onPlace`/`onBreak`)

### Tick Types

- **Scheduled** -- `scheduleTick(delay)` fires once after N ticks
- **Repeat** -- `setRepeatTickInterval(interval)` fires every N ticks
- **Random** -- Random blocks in each subchunk get ticked periodically (like Minecraft's random tick for grass spread)

### Setting Up the Event System

```cpp
UpdateScheduler scheduler(world);
TickConfig config;
config.gameTickIntervalMs = 50;      // 20 TPS
config.randomTicksPerSubchunk = 3;
scheduler.setTickConfig(config);

// In your game loop:
scheduler.advanceGameTick();   // Generate scheduled/random ticks
scheduler.processEvents();     // Process all events until stable
```

---

## 7. Block Models (Non-Cube Geometry)

For blocks that aren't simple cubes (slabs, stairs, wedges), finevox uses a data-driven model system with three file types:

### .model File

The main definition. References geometry and collision shapes:

```
# blocks/stone_slab.model
include: base/slab
texture: stone
rotation_set: vertical
```

- `include:` -- inherit from another model file (supports chains)
- `texture:` -- texture name for all faces
- `geometry:` -- path to .geom file (custom render geometry)
- `collision:` -- path to .collision file (physics shape)
- `rotation_set:` -- allowed rotations: `none`, `vertical`, `horizontal`, `horizontal_flip`, `all`

### .geom File

Defines face geometry as vertex positions and UV coordinates:

```
# shapes/slab_faces.geom
face:top:
    0 0.5 0  1 0.5 0  1 0.5 1  0 0.5 1
    0 0  1 0  1 1  0 1
face:bottom:
    0 0 0  1 0 0  1 0 1  0 0 1
    0 0  1 0  1 1  0 1
face:north:
    0 0 0  1 0 0  1 0.5 0  0 0.5 0
    0 0  1 0  1 0.5  0 0.5
```

Each face has two data lines: 4 vertex positions (x y z) and 4 UV coordinates (u v).

Face names support aliases: `west/w/-x`, `east/e/+x`, `down/bottom/-y`, `up/top/+y`, `north/n/-z`, `south/s/+z`

### .collision File

Defines collision boxes in [0,1] local space:

```
# shapes/slab_collision.collision
box:
    0 0 0  1 0.5 1
```

Each `box:` entry has min and max corners (6 floats).

### Loading Models

```cpp
BlockModelLoader loader;
auto model = loader.loadModel("resources/blocks/stone_slab.model");
```

---

## 8. World Generation

finevox provides a complete procedural generation framework in `finevox::worldgen`.

### Generation Pipeline

World generation runs as an ordered sequence of passes over each column:

```
TerrainPass (1000) -- Fill stone based on noise heightmap
    |
SurfacePass (2000) -- Replace top layers with biome-specific blocks
    |
CavePass    (3000) -- Carve caves using 3D noise
    |
OrePass     (4000) -- Place ore veins
    |
StructurePass (5000) -- Place trees, buildings
    |
DecorationPass (6000) -- Place flowers, grass
```

### Setting Up World Generation

```cpp
#include "finevox/worldgen/biome_loader.hpp"
#include "finevox/worldgen/feature_loader.hpp"
#include "finevox/worldgen/feature_registry.hpp"
#include "finevox/worldgen/generation_passes.hpp"
#include "finevox/worldgen/world_generator.hpp"
#include "finevox/worldgen/biome_map.hpp"

using namespace finevox;
using namespace finevox::worldgen;

void setupWorldGen(World& world) {
    uint64_t seed = 42;

    // 1. Load biome and feature definitions from resource files
    BiomeLoader::loadDirectory("resources/biomes", "mygame");
    FeatureLoader::loadDirectory("resources/features", "mygame");

    // 2. Configure feature placement rules
    FeaturePlacement trees;
    trees.featureName = "mygame:oak_tree";
    trees.density = 0.02f;
    trees.requiresSurface = true;
    FeatureRegistry::global().addPlacement(trees);

    // 3. Build the generation pipeline
    GenerationPipeline pipeline;
    pipeline.setWorldSeed(seed);
    pipeline.addPass(std::make_unique<TerrainPass>(seed));
    pipeline.addPass(std::make_unique<SurfacePass>());
    pipeline.addPass(std::make_unique<CavePass>(seed));
    pipeline.addPass(std::make_unique<OrePass>());
    pipeline.addPass(std::make_unique<StructurePass>());
    pipeline.addPass(std::make_unique<DecorationPass>());

    // 4. Generate columns
    BiomeMap biomeMap(seed, BiomeRegistry::global());
    for (int32_t cx = -8; cx < 8; ++cx) {
        for (int32_t cz = -8; cz < 8; ++cz) {
            auto& col = world.getOrCreateColumn(ColumnPos(cx, cz));
            pipeline.generateColumn(col, world, biomeMap);
        }
    }
}
```

### Custom Generation Passes

Insert your own passes at any priority level:

```cpp
class RiverPass : public GenerationPass {
public:
    std::string_view name() const override { return "mygame:rivers"; }
    int32_t priority() const override { return 2500; }  // After surface, before caves

    void generate(GenerationContext& ctx) override {
        // ctx.column -- the column being generated
        // ctx.heightmap -- surface Y per (x,z), populated by TerrainPass
        // ctx.biomes -- biome per (x,z)
        // ctx.world -- for cross-column reads
        // ctx.worldSeed -- for deterministic noise
    }
};

pipeline.addPass(std::make_unique<RiverPass>());
```

You can also replace or remove standard passes:
```cpp
pipeline.replacePass(std::make_unique<MyCustomTerrain>(seed));
pipeline.removePass("core:caves");  // No caves in this world
```

### Biome Data Files

Create `.biome` files in your resources directory:

```
# resources/biomes/desert.biome
name: Desert
temperature_min: 0.7
temperature_max: 1.0
humidity_min: 0.0
humidity_max: 0.3
base_height: 62
height_variation: 4
surface: sand
filler: sandstone
filler_depth: 5
stone: stone
tree_density: 0.001
```

### Feature Data Files

Trees (`.feature` files):
```
# resources/features/cactus.feature
type: tree
trunk: cactus_block
leaves: air
min_trunk_height: 2
max_trunk_height: 5
leaf_radius: 0
requires_soil: true
```

Ore veins (`.ore` files):
```
# resources/features/gold_ore.ore
block: gold_ore
replace: stone
vein_size: 6
min_height: 0
max_height: 32
veins_per_chunk: 4
```

### Noise Library

For custom terrain shaping, the noise library provides composable noise functions:

```cpp
#include "finevox/worldgen/noise.hpp"
#include "finevox/worldgen/noise_ops.hpp"

using namespace finevox::worldgen;

// Basic Perlin noise
auto base = std::make_unique<PerlinNoise2D>(seed);

// Fractal Brownian Motion (natural-looking terrain)
auto terrain = std::make_unique<FBMNoise2D>(
    std::make_unique<PerlinNoise2D>(seed),
    6,      // octaves
    2.0f,   // lacunarity (frequency multiplier per octave)
    0.5f    // persistence (amplitude multiplier per octave)
);

// Ridged noise (mountain ridges)
auto mountains = std::make_unique<RidgedNoise2D>(
    std::make_unique<OpenSimplex2D>(seed), 6);

// Domain warping (natural distortion)
auto warped = std::make_unique<DomainWarp2D>(
    std::move(terrain),
    std::make_unique<PerlinNoise2D>(seed + 1),
    std::make_unique<PerlinNoise2D>(seed + 2),
    0.3f  // warp strength
);

// Evaluate at world coordinates (scale frequency as needed)
float height = warped->evaluate(worldX * 0.01f, worldZ * 0.01f);
```

All noise functions return values in approximately [-1, 1] and are fully deterministic (same seed + coordinates = same output).

---

## 9. Physics

### Raycasting

```cpp
#include "finevox/core/physics.hpp"

PhysicsSystem physics(world);

// Find what block the player is looking at
RaycastResult result = physics.raycast(
    cameraPos,       // glm::vec3 origin
    lookDirection,   // glm::vec3 direction
    10.0f,           // max distance
    RaycastMode::Interaction  // use hit shapes (for block selection)
);

if (result.hit) {
    BlockPos target = result.blockPos;
    Face hitFace = result.face;
    // Place a block on the face the player clicked
    BlockPos placePos = target.neighbor(hitFace);
    world.placeBlock(placePos, stoneId);
}
```

### Entity Movement

```cpp
SimplePhysicsBody player;
player.setPosition({0, 70, 0});
player.setVelocity({0, 0, 0});

// In game loop:
physics.applyGravity(player, deltaTime);
glm::vec3 actualMovement = physics.moveBody(player, desiredMovement);
bool onGround = physics.checkOnGround(player);
```

---

## 10. Persistence

Persistence is mostly automatic. The engine provides:

- **RegionFile** -- stores 32x32 columns per file using CBOR serialization
- **IOManager** -- async save/load on a background thread
- **ColumnManager** -- handles the lifecycle state machine (Active -> SaveQueue -> Saving -> Unloaded)

To use persistence:

```cpp
#include "finevox/core/io_manager.hpp"

IOManager io("world_saves/my_world");

// Save a column
auto data = ColumnSerializer::serialize(column);
io.requestSave(columnPos, std::move(data));

// Load a column
io.requestLoad(columnPos, [](ColumnPos pos, std::vector<uint8_t> data) {
    auto column = ColumnSerializer::deserialize(data);
    // ... insert into world
});
```

---

## 11. Rendering Integration

Rendering uses FineStructureVK (Vulkan wrapper). The key class is `WorldRenderer` in `finevox::render`:

```cpp
#include "finevox/render/world_renderer.hpp"
#include "finevox/render/texture_manager.hpp"

using namespace finevox::render;

// Setup (during initialization)
WorldRenderer renderer;
TextureManager textures;
textures.loadAtlas("resources/textures/");
renderer.setBlockAtlas(textures.atlas());

// Each frame:
renderer.updateCamera(cameraPosition);  // glm::dvec3 for precision
renderer.render(frame);                 // Renders all visible chunks
```

Mesh generation is automatic -- when blocks change, the mesh worker pool regenerates affected subchunk meshes in background threads. The LOD system reduces detail for distant chunks.

---

## 12. Resource Files

### Directory Structure

```
resources/
+-- blocks/          # .model files for block definitions
|   +-- base/        # Base templates (slab.model, stairs.model)
+-- shapes/          # .geom and .collision files
+-- biomes/          # .biome files
+-- features/        # .feature and .ore files
+-- textures/        # Block textures (PNG)
```

### ConfigParser Format

All resource files use a simple key-value format:

```
# This is a comment
key: value
key:suffix: value
include: path/to/other_file

# Data blocks (indented lines parsed as float arrays)
vertex_data:
    0.0 1.0 0.0
    1.0 1.0 0.0
```

### ResourceLocator

For resolving resource paths at runtime:

```cpp
#include "finevox/core/resource_locator.hpp"

ResourceLocator locator;
std::string path = locator.findResource("engine", "resources");
// Returns the filesystem path to the resources directory
```

---

## Further Reading

- [LLM_REFERENCE.md](LLM_REFERENCE.md) -- Dense API reference (every method signature)
- [INDEX.md](INDEX.md) -- Full design document index
- [STYLE_GUIDE.md](STYLE_GUIDE.md) -- Coding conventions and patterns
- [SOURCE-DOC-MAPPING.md](SOURCE-DOC-MAPPING.md) -- Which source file implements which design doc section
- `examples/render_demo.cpp` -- Working example that uses all major engine features
