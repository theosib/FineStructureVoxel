# FineVox Style & Philosophy Document

## Context

This document captures finevox's design philosophy, coding conventions, and patterns as they exist in the codebase. It serves as the reference for contributors and AI assistants working on the project.

For the Vulkan framework conventions, see the [FineVK Style Guide](../../FineStructureVK/docs/STYLE_GUIDE.md). This document covers finevox-specific patterns.

---

## 1. Core Philosophy

**"Data-driven game engine with modules that define all content."**

Finevox is a voxel game engine where the engine provides infrastructure and all game content (blocks, entities, items) comes from loaded modules. The key distinction:

- **Engine provides:** World storage, mesh generation, physics, persistence, event scheduling, rendering pipeline
- **Modules provide:** Block types, block handlers, entity types, game rules, textures, models

### Design Tenets

1. **Bottom-up, layered construction** - Lower layers never depend on higher layers. Each phase is testable independently.
2. **VK-independent where possible** - Core data structures, physics, persistence, and game logic work without Vulkan. Only rendering and GPU mesh upload require FineVK.
3. **Data-driven blocks** - Block shapes, collision, textures, and properties come from spec files (`.model`/`.geom`/`.collision`), not hardcoded classes.
4. **Push over pull** - Block changes push mesh rebuild requests; events push through handler chains. Avoids polling and version-comparison overhead.
5. **Subchunk boundaries are sacred** - Greedy meshing, face culling, lighting, and LOD all operate within 16x16x16 subchunk boundaries. No cross-boundary merging.
6. **Modules are stateless handlers** - Block behavior lives in `BlockHandler` callbacks, not in per-block objects. State lives in `DataContainer` extra data.
7. **View-relative rendering** - Camera is always at the origin in GPU space. World geometry is translated relative to the camera using double-precision math on the CPU.
8. **String interning everywhere** - Block type names, data container keys, and other repeated strings are interned to integer IDs for O(1) comparison.

---

## 2. Naming Conventions

| Element | Convention | Examples |
|---------|-----------|----------|
| Classes/structs | PascalCase | `BlockType`, `MeshBuilder`, `ChunkColumn`, `UpdateScheduler` |
| Methods | camelCase | `getBlock()`, `setOpaque()`, `buildGreedyMesh()`, `scheduleTick()` |
| Member variables | camelCase + trailing `_` | `collisionShapes_`, `lightEmission_`, `shutdownFlag_`, `mutex_` |
| Free functions | camelCase | `faceNormal()`, `oppositeFace()`, `toVec3()` |
| Constants | UPPER_SNAKE_CASE | `FACE_COUNT`, `AIR_INTERNED_ID`, `CHUNK_SIZE` |
| Enum classes | PascalCase class, PascalCase values | `Face::NegX`, `ColumnState::Active`, `RotationSet::Horizontal` |
| Type aliases | PascalCase | `BlockTypeId`, `InternedId`, `IncludeResolver` |
| Template params | PascalCase | `<typename T>`, `<typename Clock>` |
| Files | snake_case | `block_type.hpp`, `mesh_worker_pool.cpp`, `test_block_model.cpp` |
| Spec files | snake_case | `stairs_faces.geom`, `solid_cube.model` |

### Boolean naming

Boolean queries use `is`/`has`/`can`/`wants` prefixes:
```cpp
bool isEmpty() const;
bool hasCustomMesh() const;
bool canUnloadChunk(ChunkPos pos) const;
bool wantsGameTicks() const;
bool isOpaque() const;
```

### Builder setters

Builder-style setters are named `set*` and return `*this`:
```cpp
BlockType& setCollisionShape(const CollisionShape& shape);
BlockType& setOpaque(bool opaque);
BlockType& setLightEmission(uint8_t level);
```

---

## 3. File Organization

### Header guards
```cpp
#pragma once
```
Always `#pragma once`, never `#ifndef` guards.

### Include ordering

1. Corresponding header (in `.cpp` files)
2. Same-library headers (`"finevox/worldgen/..."`)
3. Cross-library headers (`"finevox/core/..."`)
4. Standard library (`<vector>`, `<memory>`)
5. Third-party (`<glm/glm.hpp>`)

### File header

Every header includes a Doxygen file comment with a design document reference:
```cpp
/**
 * @file mesh.hpp
 * @brief Greedy meshing for SubChunk rendering
 *
 * Design: [06-rendering.md] Section 6.2 Mesh Generation
 */
```

### Namespaces

The project uses three namespaces corresponding to the three shared libraries:

```cpp
// Core library (include/finevox/core/, src/core/)
namespace finevox {
// BlockPos, World, BlockType, MeshBuilder, etc.
}  // namespace finevox

// World generation library (include/finevox/worldgen/, src/worldgen/)
namespace finevox::worldgen {
// Noise2D, BiomeRegistry, GenerationPipeline, Feature, Schematic, etc.
}  // namespace finevox::worldgen

// Vulkan render library (include/finevox/render/, src/render/)
namespace finevox::render {
// WorldRenderer, BlockAtlas, TextureManager, SubChunkView
}  // namespace finevox::render
```

Since `finevox::worldgen` and `finevox::render` are nested inside `finevox`, unqualified name lookup automatically finds core types from within worldgen/render code — no `using` declarations needed in library code.

**Forward declarations of core types in worldgen/render headers** must be in a separate `namespace finevox { }` block, not inside the nested namespace (which would create the wrong type):

```cpp
// CORRECT: forward-declare in parent namespace
namespace finevox {
class World;
}

namespace finevox::worldgen {
struct FeaturePlacementContext {
    World& world;  // Found via parent namespace lookup
};
}  // namespace finevox::worldgen
```

**`std::hash<>` specializations** must use fully qualified types:
```cpp
template<>
struct std::hash<finevox::worldgen::BiomeId> { ... };
```

### Section markers

Major sections within a class or file use visual separators:
```cpp
// ============================================================================
// Builder-style setters
// ============================================================================
```

### Directory layout

Headers and sources are organized into subdirectories by library:

```
include/finevox/
├── core/          # Core engine (namespace finevox)
├── worldgen/      # World generation (namespace finevox::worldgen)
└── render/        # Vulkan rendering (namespace finevox::render)

src/
├── core/          # Core implementations
├── worldgen/      # Worldgen implementations
└── render/        # Render implementations
```

Include paths reflect the subdirectory:
```cpp
#include "finevox/core/world.hpp"
#include "finevox/worldgen/biome.hpp"
#include "finevox/render/world_renderer.hpp"
```

---

## 4. Class Structure

### Member ordering

1. **Public** first: constructors, factories, main API
2. **Protected** (rare): for inheritance points
3. **Private** last: member variables, helpers

Within public:
1. Constructors and destructor
2. Deleted/defaulted copy/move
3. Builder-style setters (grouped under section header)
4. Accessors/getters (grouped under section header)
5. Main operations
6. Static methods and constants

### Builder pattern

Configuration objects use chained setters that return `*this`:
```cpp
class BlockType {
public:
    BlockType() = default;

    // Setters (builder-style)
    BlockType& setCollisionShape(const CollisionShape& shape);
    BlockType& setOpaque(bool opaque);
    BlockType& setHasCustomMesh(bool custom);

    // Getters
    [[nodiscard]] bool isOpaque() const { return opaque_; }
    [[nodiscard]] bool hasCustomMesh() const { return hasCustomMesh_; }

private:
    bool opaque_ = true;
    bool hasCustomMesh_ = false;
};
```

This is not the FineVK builder pattern (separate Builder class with `build()`). Finevox uses in-place chaining because these objects are configured once and stored, not created from complex dependency graphs.

### Forward declarations

Used consistently to break circular dependencies:
```cpp
class SubChunk;
class World;
struct BlockChange;
```

---

## 5. Ownership and Memory

| Type | Meaning | Example |
|------|---------|---------|
| `std::unique_ptr<T>` | Exclusive ownership | `unique_ptr<ChunkColumn>` in ColumnManager |
| `std::shared_ptr<T>` | Shared ownership | `shared_ptr<SubChunk>` for cross-thread access |
| `std::weak_ptr<T>` | Non-owning observer | Mesh cache referencing SubChunks |
| `T*` (raw pointer) | Non-owning reference | `LightEngine* lightEngine_` on World |
| Stack value | Transient, short-lived | `BlockContext`, `MeshData`, `ConfigEntry` |

### Rules

- Parents own children via smart pointers
- Children hold raw pointers to parents (non-owning, documented)
- Raw pointers are explicitly documented with lifetime expectations:
  ```cpp
  /// Set light engine. World does not take ownership; caller must ensure lifetime.
  void setLightEngine(LightEngine* engine);
  ```
- Default-initialize raw pointers to `nullptr`
- `std::make_unique` / `std::make_shared` for construction

---

## 6. Error Handling

Finevox uses a layered error strategy:

| Situation | Approach |
|-----------|----------|
| Query that may find nothing | `std::optional<T>` return |
| Operation that may not apply | `bool` return (true = success) |
| Invalid configuration at startup | `std::runtime_error` exception |
| Graceful degradation | Silent no-op (e.g., push to shutdown queue) |
| Missing resource at load time | `std::optional` from loader, caller decides |

### std::optional for queries

```cpp
[[nodiscard]] std::optional<LocalBlockPos> neighbor(Face face) const;
[[nodiscard]] std::optional<ConfigDocument> parseFile(const std::string& path) const;
[[nodiscard]] std::optional<BlockModel> load(const std::string& logicalPath);
```

### Bool for operations

```cpp
bool placeBlock(BlockPos pos, BlockTypeId type);
bool save();
bool canUnloadChunk(ChunkPos pos) const;
```

### Default values for missing data

```cpp
float getFloat(const std::string& key, float defaultVal = 0.0f) const;
BlockTypeId getBlockType(const DataContainer& data, const std::string& key, BlockTypeId defaultVal);
```

---

## 7. Threading Patterns

### Synchronization primitives

| Primitive | Use case |
|-----------|----------|
| `std::shared_mutex` | Read-heavy data (column map, force-loader registry) |
| `std::mutex` | Exclusive access (queues, caches) |
| `std::condition_variable` | Queue wake patterns |
| `std::atomic<bool>` | Shutdown flags, dirty flags |
| `std::atomic<uint64_t>` | Version counters (blockVersion, lightVersion) |

### Lock patterns

Always use RAII locks:
```cpp
std::lock_guard<std::mutex> lock(mutex_);           // Simple exclusive
std::unique_lock<std::mutex> lock(mutex_);           // When CV needed
std::shared_lock<std::shared_mutex> lock(mutex_);    // Read access
std::unique_lock<std::shared_mutex> lock(mutex_);    // Write access
```

### Mutable for const-correct locking

```cpp
mutable std::shared_mutex columnMutex_;
mutable std::mutex forceLoaderMutex_;
```

### Queue architecture

Thread-safe queues use internal CV + optional external `WakeSignal` attachment for multi-queue coordination:
```cpp
template<typename T>
class SimpleQueue : public Queue {
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<T> queue_;
    std::atomic<bool> shutdownFlag_ = false;
    WakeSignal* signal_ = nullptr;  // Optional multi-queue wakeup
};
```

### Push-based data flow

Block changes and lighting updates push work to downstream queues rather than relying on polling:
```
Block change → Event queue → Handler → Lighting queue → Mesh rebuild queue → Worker threads → GPU upload
```

---

## 8. Type Usage

### `[[nodiscard]]`

Applied to all getters, queries, and factory methods:
```cpp
[[nodiscard]] bool isEmpty() const;
[[nodiscard]] BlockTypeId getBlock(BlockPos pos) const;
[[nodiscard]] std::optional<BlockModel> load(const std::string& path);
```

### `constexpr`

Used for position types, face utilities, and compile-time data:
```cpp
constexpr Face oppositeFace(Face f);
[[nodiscard]] constexpr BlockPos apply(BlockPos pos) const;
constexpr LocalBlockPos() = default;
```

### `auto`

Used sparingly. Prefer explicit types for clarity:
```cpp
// Preferred: explicit type
BlockTypeId type = subChunk.getBlock(x, y, z);
const auto& entry = doc.get("key");  // OK when type is obvious from RHS

// Avoid: unclear type
auto result = computeSomething();  // What type is this?
```

### const correctness

Applied consistently:
- `const` on all query methods
- `const&` for input parameters that are not modified
- `const` return references where caller should not modify

### `std::string_view`

Used for lookup parameters to avoid copies:
```cpp
const ConfigEntry* get(std::string_view key) const;
std::optional<InternedId> find(std::string_view str) const;
```

### Comparison operators

Use C++20 defaulted spaceship operator where applicable:
```cpp
constexpr auto operator<=>(const LocalBlockPos& other) const = default;
constexpr bool operator==(const LocalBlockPos& other) const = default;
```

---

## 9. Callbacks and Function Types

### Named type aliases

```cpp
using ColumnGenerator = std::function<void(ChunkColumn&)>;
using BlockTextureProvider = std::function<glm::vec4(BlockTypeId, Face)>;
using BlockLightProvider = std::function<float(const glm::ivec3&)>;
using IncludeResolver = std::function<std::string(const std::string&)>;
using CanUnloadCallback = std::function<bool(ColumnPos)>;
```

### Callback setters

```cpp
void setBlockChangeCallback(std::function<void(BlockPos, BlockTypeId, BlockTypeId)> cb);
void setChunkLoadCallback(std::function<void(ChunkPos, std::shared_ptr<SubChunk>)> cb);
```

### Lambda test doubles

Tests define inline lambdas for callbacks:
```cpp
BlockTextureProvider simpleTexProvider = [](BlockTypeId, Face) {
    return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
};

auto nothingOpaque = [](BlockTypeId) { return false; };
auto everythingOpaque = [](BlockTypeId) { return true; };
```

---

## 10. Data-Driven Block System

### Spec file format

Block definitions use ConfigParser format (not YAML). Three file types:

| Extension | Purpose | Example |
|-----------|---------|---------|
| `.model` | Block properties, texture, includes | `stone.model`, `base/stairs.model` |
| `.geom` | Render geometry (face vertices + UVs) | `stairs_faces.geom` |
| `.collision` | Collision boxes (AABB lists) | `stairs_collision.collision` |

### Inheritance via `include:`

```
# stone_slab.model
include: base/slab
texture: stone
```

### Face naming

Standard faces 0-5 have aliases: `negx`/`west`, `posx`/`east`, `negy`/`down`/`bottom`, `posy`/`up`/`top`, `negz`/`north`, `posz`/`south`. Custom faces use names like `step_top`, `riser`.

### Mesh path routing

- Blocks WITHOUT `geometry:` in their model file: standard cube path (greedy meshing + AO)
- Blocks WITH `geometry:`: custom mesh path (`hasCustomMesh` flag, `addCustomFace()`)
- Standard cubes should NOT have `geometry:` even if they use `solid_cube.geom` - this incorrectly routes them through the custom mesh path

---

## 11. Test Conventions

### Framework

Google Test (gtest). Tests live in `tests/test_*.cpp`.

### Fixture pattern

```cpp
class MeshTest : public ::testing::Test {
protected:
    MeshBuilder builder;

    void SetUp() override {
        builder.setGreedyMeshing(false);
    }
};
```

### Test naming

- `TEST(ClassName, DescriptiveTestName)` for standalone tests
- `TEST_F(FixtureClass, DescriptiveTestName)` for fixture tests

```cpp
TEST(ChunkVertexTest, DefaultConstruction)
TEST(FaceGeometryTest, ComputeBoundsFromVertices)
TEST_F(MeshTest, SingleBlockAllFacesExposed)
TEST_F(GreedyMeshTest, TwoAdjacentBlocksMerged)
```

### Assertions

- `EXPECT_*` for non-fatal checks (test continues)
- `ASSERT_*` for fatal checks (test stops)
- `EXPECT_FLOAT_EQ` for floating-point comparison
- `SUCCEED()` for compile-and-link verification tests

### Test independence

Each test creates its own `World`, `StringInterner::global().reset()` as needed. No shared mutable state across tests.

---

## 12. Architectural Boundaries

### VK-independent vs VK-dependent

| VK-Independent | VK-Dependent |
|---------------|--------------|
| `position.hpp`, `subchunk.hpp`, `world.hpp` | `world_renderer.hpp`, `subchunk_view.hpp` |
| `physics.hpp`, `mesh.hpp` (CPU mesh gen) | `texture_manager.hpp`, `block_atlas.hpp` |
| `block_type.hpp`, `block_model.hpp` | `examples/render_demo.cpp` |
| `event_queue.hpp`, `light_engine.hpp` | |
| All test files | |

VK-dependent code lives in `finevox_render` (separate CMake target). VK-independent code lives in `finevox` (core library).

### Library split

Three shared libraries with clear dependency direction:

```
finevox              # Core (namespace finevox::)
  └─ finevox_worldgen  # World gen (namespace finevox::worldgen::), links finevox
  └─ finevox_render    # Vulkan render (namespace finevox::render::), links finevox
```

- `finevox_worldgen` does NOT depend on `finevox_render` (and vice versa)
- Tests link `finevox_worldgen` (gets core transitively)
- `render_demo` links both `finevox_render` and `finevox_worldgen`

### External dependencies

| Dependency | Usage | Linked via |
|------------|-------|-----------|
| GLM | Math (vec3, mat4, etc.) | FetchContent |
| LZ4 | Region file compression | FetchContent |
| GoogleTest | Unit tests | FetchContent |
| FineStructureVK | Vulkan rendering | External (FINEVK_DIR) |
| finegui | GUI overlays | External (FINEGUI_DIR), optional |

---

## 13. Documentation References

Every significant class or subsystem should reference its design document:

```cpp
/**
 * @file event_queue.hpp
 * @brief Three-queue event architecture for block updates
 *
 * Design: [24-event-system.md] Section 24.6, 24.13
 */
```

The authoritative phase checklist is [17-implementation-phases.md](17-implementation-phases.md). The source-to-doc mapping is [SOURCE-DOC-MAPPING.md](SOURCE-DOC-MAPPING.md).

---

*Last Updated: 2026-02-07*
