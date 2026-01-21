# 19. Block Models and Resource Format

[Back to Index](INDEX.md) | [Previous: Open Questions](18-open-questions.md)

---

## 19.1 Design Goals

Block models in finevox should be:

1. **Data-driven** - No hardcoded collision shapes; all loaded from files
2. **Hierarchical** - Model files can include other model files for composition
3. **Concise** - Common variants should require minimal specification
4. **Reusable** - Same geometry file can serve multiple purposes (render, collision, hit)
5. **Resource-locator integrated** - File resolution via ResourceLocator, not raw paths

---

## 19.2 The Three Shapes

Every block has up to three distinct geometric definitions:

| Shape | Purpose | Example |
|-------|---------|---------|
| **Render Model** | Visual appearance for mesh generation | Full OBJ model with UVs |
| **Collision Shape** | Physics - prevents entities passing through | Collection of AABBs |
| **Hit Shape** | Raycasting - player selection/interaction | Collection of AABBs (often same as collision) |

### Shape Fallback Chain

When a block doesn't explicitly specify all three shapes, automatic fallbacks apply:

```
hit → collision → render → full
```

**Fallback rules:**
1. If `hit` is not specified, use `collision`
2. If `collision` is not specified, use `render` (converted to AABB bounding box)
3. If `render` is not specified, use `full` (standard cube)

This means most blocks only need to specify what's different:
- A decorative block with pass-through physics: just set `collision: none`
- A complex model with full-cube collision: just specify `render`
- An interactable with special hit box: specify `hit` explicitly

### Examples

| Block | Render | Collision | Hit |
|-------|--------|-----------|-----|
| Stone | Full cube | Full cube | Full cube (same) |
| Glass Pane | Thin cross | Thin cross | Full cube (easier to click) |
| Tall Grass | Crossed quads | None (walk through) | Full cube (can break) |
| Torch | Small flame model | None (walk through) | Small box (around base) |
| Fence | Post + connections | 1.5-block-high post | Same as collision |
| Stairs | Complex geometry | 2 AABBs | Same as collision |

---

## 19.3 Hierarchical Model Format

Model files use a YAML-like format with `include:` directives for composition:

```yaml
# stone_brick_stairs.model
# A specific textured variant of stairs

include: base/stairs.model           # Inherits render, collision, hit shapes
texture: blocks/stone_brick.png      # Overrides texture
```

```yaml
# base/stairs.model
# Base stair shape - used by all stair variants

collision: base/shapes/collision_stairs   # Load geometry as collision
# hit: (omitted - falls back to collision automatically)
render: base/models/stairs.obj            # Visual mesh

# These are inherited by all stair variants
sounds: stone                        # Step, break, place sounds
hardness: 1.5
blast_resistance: 6.0
```

```yaml
# base/shapes/collision_stairs.geom
# Collision boxes for stairs (geometry only, no purpose assigned)

boxes:
  - [0.0, 0.0, 0.0, 1.0, 0.5, 1.0]   # Bottom slab
  - [0.0, 0.5, 0.5, 1.0, 1.0, 1.0]   # Back half (top step)
```


### Hierarchy Benefits

**Adding a new wood stair:**
```yaml
# oak_stairs.model
include: base/stairs.model
texture: blocks/oak_planks.png
sounds: wood
```

**Adding a new stone stair:**
```yaml
# granite_stairs.model
include: base/stairs.model
texture: blocks/granite.png
# sounds: stone (inherited)
```

Two lines for a complete new block!

---

## 19.4 Model File Specification

### Include Directive

```yaml
include: path/to/other.model
```

- Paths resolved via ResourceLocator (logical hierarchy, not raw filesystem)
- Multiple includes allowed (processed in order)
- Later values override earlier ones
- Hierarchy can be any depth

### Collision Shape

```yaml
collision: none                      # Pass-through block
collision: full                      # Standard full cube
collision:
  - [x1, y1, z1, x2, y2, z2]        # AABB in [0,1] local coordinates
  - [x1, y1, z1, x2, y2, z2]        # Multiple boxes allowed
```

### Hit Shape

```yaml
hit: inherit                         # Same as collision shape (default if omitted)
hit: full                            # Standard full cube
hit:
  - [x1, y1, z1, x2, y2, z2]        # Custom AABB(s)
```

### Render Model

```yaml
render: full                         # Standard full cube with texture
render: models/custom.obj            # OBJ file reference
render:
  model: models/custom.obj
  scale: 0.5                         # Optional scaling
  offset: [0, 0.5, 0]                # Optional offset
```

### Reusing Geometry Files with Purpose Assignment

A single geometry file can contain just shapes without specifying their purpose. The loading file assigns purpose:

```yaml
# shapes/stair_collision.geom - Just geometry, no purpose
boxes:
  - [0.0, 0.0, 0.0, 1.0, 0.5, 1.0]   # Bottom slab
  - [0.0, 0.5, 0.5, 1.0, 1.0, 1.0]   # Back half
```

```yaml
# blocks/oak_stairs.model - Assigns purpose when loading
collision: shapes/stair_collision.geom    # Use as collision
hit: shapes/stair_collision.geom          # Same file, also use as hit
render: models/stairs.obj
texture: blocks/oak_planks.png
```

**Purpose override rules:**
1. If included file specifies `collision:`, it provides collision shape
2. If loader specifies `collision: some_file.geom`, loader's purpose assignment wins
3. Files with just `boxes:` or OBJ geometry have no inherent purpose until assigned

This allows maximum reuse - the same stair geometry file can be:
- Loaded as collision by one model
- Loaded as hit by another
- Used for both in a third

### Texture

```yaml
texture: blocks/stone.png            # Single texture for all faces
texture:
  top: blocks/grass_top.png
  bottom: blocks/dirt.png
  sides: blocks/grass_side.png
texture:
  +x: blocks/furnace_side.png        # Per-face specification
  -x: blocks/furnace_side.png
  +y: blocks/furnace_top.png
  -y: blocks/furnace_top.png
  +z: blocks/furnace_front.png       # Front face (north)
  -z: blocks/furnace_side.png
```

### Texture Atlases

Textures can come from individual files or from atlases:

```yaml
# Individual file
texture: blocks/stone.png

# Mini-atlas (few related textures)
texture:
  atlas: blocks/wood_variants.png
  region: [0, 0, 16, 16]             # x, y, w, h in pixels

# Named region in atlas
texture:
  atlas: blocks/terrain.atlas
  name: stone_brick                  # Looked up in atlas manifest

# Per-face from atlas
texture:
  top:
    atlas: blocks/terrain.atlas
    name: grass_top
  sides:
    atlas: blocks/terrain.atlas
    name: grass_side
  bottom: blocks/dirt.png            # Can mix atlas and individual
```

**Atlas manifest format:**
```yaml
# blocks/terrain.atlas.yaml
image: terrain.png
tile_size: 16                        # Default tile size
regions:
  stone: [0, 0]                      # Grid position (uses tile_size)
  dirt: [1, 0]
  grass_top: [2, 0]
  grass_side:
    pos: [3, 0]
    size: [16, 16]                   # Override size if needed
  stone_brick: [0, 1]
```

**Note:** The `TextureManager` class (see §19.10) implements this atlas system with CBOR-based atlas definitions.

### Properties

```yaml
hardness: 1.5                        # Mining time factor
blast_resistance: 6.0                # Explosion resistance
sounds: stone                        # Sound set name
light_emission: 15                   # 0-15, torch = 14
transparent: false                   # Render sorting
opaque: true                         # Light blocking
waterloggable: false                 # Can contain water
```

---

## 19.5 Alternative: OBJ-Based Approach

FineStructureVK already has an OBJ loader. We could use separate OBJ files:

```
stone_stairs/
  render.obj           # Visual mesh
  collision.obj        # Collision boxes as mesh (converted to AABBs)
  hit.obj              # Hit boxes as mesh
  properties.yaml      # Texture, sounds, etc.
```

**Conversion to AABBs:**
- Parse OBJ mesh
- For each connected component, compute axis-aligned bounding box
- Result is `CollisionShape` with multiple AABBs

**Pros:**
- Standard format
- Can edit in Blender
- Single toolchain

**Cons:**
- Verbose for simple shapes
- OBJ is overkill for AABB lists
- Can't express "none" or "full" shortcuts

**Hybrid approach:** Use OBJ for complex render models, YAML shortcuts for collision/hit:

```yaml
render: models/stairs.obj            # OBJ for visual
collision:                           # YAML for collision (simpler)
  - [0, 0, 0, 1, 0.5, 1]
  - [0, 0.5, 0.5, 1, 1, 1]
```

---

## 19.6 Rotation Handling

Blocks with directional variants need rotation-aware models:

```yaml
# base/stairs.model
rotatable: true                      # Enable rotation variants
rotation_axis: y                     # Horizontal rotation only

collision:
  - [0.0, 0.0, 0.0, 1.0, 0.5, 1.0]  # These are for "facing north"
  - [0.0, 0.5, 0.5, 1.0, 1.0, 1.0]  # Engine precomputes all 4 rotations
```

At registration time, the engine:
1. Loads the base model
2. Precomputes rotated collision/hit shapes using `CollisionShape::computeRotations()`
3. Stores all variants for O(1) lookup based on block rotation state

---

## 19.7 Resource Locator Integration

Model files are resolved via the ResourceLocator system (see [15 - Configuration](15-configuration.md)), not raw filesystem paths. This provides:

- Module isolation and override chains
- Logical hierarchy independent of physical layout
- Cross-module references

### Logical Resource Paths

```yaml
# In a model file
include: base/stairs                 # Logical name, not filesystem path
texture: blocks/oak_planks           # ResourceLocator finds the file
collision: shapes/stair_collision    # Extension optional
```

ResourceLocator resolves `blocks/oak_planks` by searching:
1. Current module's texture directories
2. Dependency modules
3. Base engine assets

### Module Resource Layout

```
mymod/
  mod.yaml                           # Module metadata
  models/
    blocks/
      custom_ore.model               # Block models
      base/
        full_block.model             # Shared base models
        stairs.model
      shapes/
        stair_collision.geom         # Reusable geometry
    items/
      custom_tool.model              # Item models
    entities/
      custom_mob.model               # Entity models
  textures/
    blocks/
      custom_ore.png
      custom_ore_top.png
      terrain.atlas.yaml             # Atlas manifest
      terrain.png                    # Atlas image
    items/
      custom_tool.png
  sounds/
    custom_ore_break.ogg
```

### Resolution Examples

| Reference | Resolved Path |
|-----------|---------------|
| `blocks/stone` | `mymod/models/blocks/stone.model` or `base/models/blocks/stone.model` |
| `base:stairs` | Engine's `base/models/blocks/stairs.model` |
| `othermod:blocks/ore` | `othermod/models/blocks/ore.model` |
| `textures/blocks/stone` | `mymod/textures/blocks/stone.png` |

---

## 19.8 Implementation Notes

### Shape Loading with Fallback Chain

```cpp
struct LoadedModel {
    std::optional<CollisionShape> render;     // Converted to AABB if OBJ
    std::optional<CollisionShape> collision;
    std::optional<CollisionShape> hit;
    // ... other properties
};

CollisionShape resolveCollisionShape(const LoadedModel& model) {
    // Explicit collision shape
    if (model.collision.has_value()) {
        return *model.collision;
    }
    // Fallback to render bounding box
    if (model.render.has_value()) {
        return *model.render;  // Already converted to AABB bounds
    }
    // Final fallback: full block
    return CollisionShape::FULL_BLOCK;
}

CollisionShape resolveHitShape(const LoadedModel& model) {
    // Explicit hit shape
    if (model.hit.has_value()) {
        return *model.hit;
    }
    // Fallback to collision
    return resolveCollisionShape(model);
}

CollisionShape loadCollisionFromSource(const std::string& source, ResourceLocator& locator) {
    if (source == "none") {
        return CollisionShape::NONE;
    }
    if (source == "full") {
        return CollisionShape::FULL_BLOCK;
    }
    if (source == "inherit") {
        return {};  // Signal to use fallback
    }

    // Load from file (could be .geom YAML or .obj)
    auto path = locator.resolve(source);
    if (path.extension() == ".obj") {
        return loadObjAsCollision(path);  // Convert mesh to AABBs
    }
    return loadGeomFile(path);  // Load AABB list from YAML
}
```

### Hierarchical Include Processing

```cpp
ModelFile loadModelWithIncludes(const std::string& logicalPath, ResourceLocator& locator) {
    ModelFile result;

    auto physicalPath = locator.resolve(logicalPath);
    auto file = parseYaml(physicalPath);

    // Process includes first (in order)
    for (const auto& include : file.includes) {
        ModelFile parent = loadModelWithIncludes(include, locator);
        result.merge(parent);  // Parent values as base
    }

    // Then apply this file's values (override parents)
    result.merge(file);

    return result;
}
```

### Greedy Meshing Integration

Blocks with custom render models (non-cube geometry) require special handling in the mesh generation pipeline:

**The Problem:**
Greedy meshing merges adjacent coplanar faces of the same block type into larger quads. This works for standard cubes but breaks for:
- Stairs (multiple faces at different heights)
- Slabs (half-height blocks)
- Fences (posts and rails)
- Any block with orientation-dependent geometry

**Solution: `hasCustomMesh` Flag**

When the model loader detects a non-trivial render model, set a flag on `BlockType`:

```cpp
class BlockType {
    // ... existing fields ...
    bool hasCustomMesh_ = false;  // True if render model is not a standard cube

public:
    BlockType& setHasCustomMesh(bool custom) { hasCustomMesh_ = custom; return *this; }
    [[nodiscard]] bool hasCustomMesh() const { return hasCustomMesh_; }
};
```

**Mesh Builder Check:**

In `greedyMeshFace()`, skip blocks with custom meshes:

```cpp
BlockTypeId blockType = subChunk.getBlock(x, y, z);
if (blockType == AIR_BLOCK_TYPE) continue;

// Check if this block uses a custom mesh
if (blockRegistry.getType(blockType).hasCustomMesh()) {
    continue;  // Don't include in greedy mask - will render separately
}
```

Custom mesh blocks are then rendered via `buildSimpleMesh()` or a dedicated custom mesh pass.

**Note:** The current `FaceMaskEntry` comparison already prevents merging blocks with different AO values, which provides partial protection for orientation-dependent blocks. The explicit flag provides complete coverage.

---

## 19.9 Comparison with Minecraft VoxelShapes

Minecraft's `VoxelShape` system:
- Represents shapes as boolean voxel grids
- Supports set operations (union, intersection, subtraction)
- Can simplify complex shapes to AABB lists

**We differ:**
- Use AABB lists directly (simpler, sufficient for most blocks)
- Focus on data-driven loading rather than runtime construction
- Hierarchical model files instead of Java builder APIs

If complex shape operations become needed, we could add a shape DSL:

```yaml
collision:
  union:
    - full                           # Start with full cube
    - subtract: [0, 0.5, 0, 1, 1, 0.5]  # Remove top-front quarter
```

But this adds complexity. Start with explicit AABB lists; add DSL if needed.

---

## 19.10 TextureManager Implementation

The `TextureManager` class provides the abstraction layer between texture references and atlas details:

### Core Concepts

1. **TextureRegion** - A rectangular UV region within a texture/atlas
2. **TextureHandle** - Opaque reference containing atlas index + region
3. **AtlasDefinition** - Config file describing atlas organization
4. **TextureManager** - Registry with named lookups

### Naming Convention

Texture names follow a consistent pattern:

```
"atlasName:regionName"  - For atlas textures (e.g., "blocks:stone_top")
"textureName"           - For standalone textures (e.g., "logo")
```

### Atlas Definition Format

Atlas definitions use the same human-readable format as block models:

```
# blocks.atlas
name: blocks
image: game://textures/blocks/terrain.png
grid: 16 16

# Grid-based regions (x y)
region:stone: 0 0
region:dirt: 1 0
region:grass_top: 2 0
region:grass_side: 3 0

# Pixel-based regions (x y w h) - use 4 values
region:custom_sprite: 128 64 32 32
```

### Usage Pattern

```cpp
TextureManager textures(device, commandPool);

// Load atlas from definition file
textures.loadAtlas("game://textures/blocks.atlas");

// Or register programmatically
textures.registerGridAtlas("items", "game://textures/items.png", 16, 16);
textures.registerGridRegion("items", "sword", 0, 0);
textures.registerGridRegion("items", "pickaxe", 1, 0);

// Standalone textures work too (degenerate single-region atlas)
textures.registerTexture("logo", "game://textures/ui/logo.png");

// Lookup by name
auto handle = textures.getTexture("blocks:stone");
if (handle) {
    finevk::Texture* atlas = textures.getAtlasTexture(handle->atlasIndex);
    glm::vec4 uvBounds = handle->region.bounds();
}

// Create provider for MeshBuilder
auto provider = textures.createBlockProvider(
    [](BlockTypeId id, Face face) -> std::string {
        // Return texture name for this block/face
        return getBlockTextureName(id, face);
    }
);
```

### Block Texture Config

The manager can load block-to-texture mappings from a config file:

```yaml
# block_textures.cbor
stone:
  all: "blocks:stone"

grass:
  top: "blocks:grass_top"
  bottom: "blocks:dirt"
  sides: "blocks:grass_side"

furnace:
  top: "blocks:furnace_top"
  bottom: "blocks:furnace_top"
  north: "blocks:furnace_front"
  sides: "blocks:furnace_side"
```

```cpp
textures.loadBlockTextureConfig("game://config/block_textures.cbor");

// Now can use built-in block name lookup
std::string texName = textures.getBlockTextureName(grassId, Face::PosY);
// Returns "blocks:grass_top"
```

### Design Benefits

1. **Decoupled references** - Code uses names, not atlas coordinates
2. **Single textures as degenerate case** - Unified API regardless of source
3. **Config-driven** - Atlas organization in data files, not code
4. **ResourceLocator integration** - Paths resolved through resource system
5. **Prepared for dynamic packing** - When finevk adds runtime atlas packing, the TextureManager can use it transparently

---

[Next: TBD]
