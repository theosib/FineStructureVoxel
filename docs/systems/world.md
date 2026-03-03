# System: World & Block Data

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/`
**Old docs:** [old_docs/04-core-data-structures.md](../../old_docs/04-core-data-structures.md), [old_docs/05-world-management.md](../../old_docs/05-world-management.md)

---

## Overview

The core data model: position types (BlockCoord → ChunkPos → ColumnPos), interned block type IDs, per-subchunk block storage with palettes, full-height column management, and the World class. Block data storage uses word-aligned bit-packing; the global StringInterner handles all name→ID mappings.

---

## Position Types

```cpp
#include <finevox/core/block_coord.hpp>
#include <finevox/core/chunk_pos.hpp>

// BlockCoord — world block position
struct BlockCoord {
    int32_t x, y, z;
    uint64_t pack() const;
    static BlockCoord unpack(uint64_t packed);
    int toLocalIndex() const;         // index within 16×16×16 subchunk
    BlockCoord neighbor(Face f) const;
    ChunkPos toChunkPos() const;
    ColumnPos toColumnPos() const;
};

// ChunkPos — subchunk position (subchunk grid)
struct ChunkPos {
    int32_t x, y, z;
    uint64_t pack() const;
    static ChunkPos unpack(uint64_t);
    ColumnPos toColumnPos() const;
    int subchunkLocalY() const;  // y-index within column
};

// ColumnPos — XZ column position only
struct ColumnPos {
    int32_t x, z;
    uint64_t pack() const;
};
```

---

## String Interning & IDs

```cpp
#include <finevox/core/string_interner.hpp>
#include <finevox/core/block_type.hpp>

// Global singleton
StringInterner& si = StringInterner::global();
InternedId id = si.intern("finevox:stone");     // thread-safe, idempotent
std::string_view name = si.lookup(id);          // O(1)

// BlockTypeId — typed wrapper around InternedId
BlockTypeId stone(si.intern("finevox:stone"));  // CORRECT
// BlockTypeId bad("finevox:stone");             // WRONG — no string ctor

// BlockTypeId.id is a public uint32_t member
uint32_t raw = stone.id;  // NOT stone.id()

// Special value
extern const BlockTypeId AIR_BLOCK_TYPE;  // id = 0
```

---

## BlockType

```cpp
#include <finevox/core/block_type.hpp>

struct BlockType {
    // Properties
    bool isSolid() const;
    bool isTransparent() const;
    bool hasCustomMesh() const;  // true = custom geometry path, false = standard cube+AO
    bool wantsGameTicks() const;
    uint8_t lightEmission() const;    // 0-15
    uint8_t lightAttenuation() const; // 0-15
    bool blocksSkyLight() const;

    // Collision/hit shapes (24-rotation precomputed)
    const CollisionShape& collisionShape(uint8_t rotation = 0) const;
    const CollisionShape& hitShape(uint8_t rotation = 0) const;

    // Handler (game logic behavior)
    BlockHandler* handler() const;  // may be nullptr
};

// Registry
BlockRegistry& reg = BlockRegistry::global();
const BlockType& type = reg.getType(blockTypeId);  // ref, not pointer; asserts if not found
```

---

## SubChunk (16×16×16)

```cpp
#include <finevox/core/subchunk.hpp>

SubChunk& sc = column.subchunk(subchunkY);

// Block access (via palette)
BlockTypeId type = sc.getBlockType(localIndex);  // localIndex = BlockCoord.toLocalIndex()
sc.setBlockType(localIndex, blockTypeId);

// Extra data per block
DataContainer* data = sc.blockData(localIndex);           // nullptr if none
DataContainer& data = sc.getOrCreateBlockData(localIndex); // creates if needed

// Subchunk-level data
DataContainer* data = sc.data();
DataContainer& data = sc.getOrCreateData();

// Version tracking (atomic, for mesh staleness detection)
uint32_t v = sc.blockVersion();
uint32_t lv = sc.lightVersion();

// Fluid access
sc.setFluid(localIndex, fluidTypeId, level);
auto [ftype, flevel] = sc.getFluid(localIndex);

// Game tick registration
bool wantsTick = sc.hasGameTickBlocks();
// auto-maintained: blocks with BlockType::wantsGameTicks() register on setBlockType
```

---

## ChunkColumn

```cpp
#include <finevox/core/chunk_column.hpp>

// Column position
ColumnPos pos = col.position();  // NOT col.pos()
int nonAir = col.nonAirCount();  // NOT col.blockCount()

// SubChunk access
SubChunk* sc = col.getSubchunk(subchunkY);  // nullptr if not allocated
SubChunk& sc = col.getOrCreateSubchunk(subchunkY);

// Column-level data
DataContainer* data = col.data();

// Light
bool initialized = col.lightInitialized();

// Game tick registries (rebuilt on load, not serialized)
col.rebuildGameTickRegistries();
```

---

## World

```cpp
#include <finevox/core/world.hpp>

// Block operations
BlockTypeId type = world.getBlock(blockCoord);
world.setBlock(pos, typeId);        // direct (for tests, initial setup)
world.placeBlock(pos, typeId);      // deferred via command queue (requires UpdateScheduler)

// Column access
ChunkColumn* col = world.getOrLoadColumn(columnPos);
ChunkColumn* col = world.getColumn(columnPos);  // nullptr if not loaded

// Fluid operations (see systems/fluids.md)
world.setFluid(pos, typeId, level);
world.getFluid(pos);

// Force-loaders (prevent chunk unload)
world.registerForceLoader(pos, radius);
world.unregisterForceLoader(pos);
bool canUnload = world.canUnloadChunk(chunkPos);

// Non-copyable — use unique_ptr
static std::unique_ptr<World> create(config);
```

---

## ColumnManager & Lifecycle

Column lifecycle states:
```
Unloaded → Loading → Loaded → Active
Active → SaveQueued → Saving → UnloadQueued → Evicted (LRU cache)
```

Key rules:
- Columns in `currentlySaving_` set: `get()` returns nullptr until save completes (prevents stale load)
- Activity timer resets on block events — prevents premature unload during cross-chunk updates
- LRU eviction: only after save completes AND column idle for `activityTimeoutMs_` (default 5s)
- RefCount: dropping to 0 triggers save or unload

---

## Face Enum

```cpp
enum class Face : int {
    NegX = 0, PosX = 1, NegY = 2, PosY = 3, NegZ = 4, PosZ = 5
};

Face opp = Face::opposite(face);
glm::ivec3 n = Face::faceNormal(face);  // {-1,0,0}, {1,0,0}, etc.
BlockCoord neighbor = pos + Face::faceOffset(face);
```

---

## Rotation

24 valid rotations (0-23). Precomputed lookup tables:

```cpp
#include <finevox/core/rotation.hpp>

// Transform a face through a rotation
Face worldFace = Rotation::transformFace(rotation, localFace);
Face localFace = Rotation::inverseTransformFace(rotation, worldFace);

// CollisionShape with all 24 rotations precomputed
static std::array<CollisionShape, 24> CollisionShape::computeRotations(const CollisionShape& base);
```

---

## Block Data Helpers

```cpp
#include <finevox/core/block_data_helpers.hpp>

// Store block type reference by name (stable across sessions)
setBlockType(data, "material", blockTypeId);
BlockTypeId mat = getBlockType(data, "material", AIR_BLOCK_TYPE);
bool has = hasBlockType(data, "material");
```

---

## Gotchas

- `ChunkColumn::position()` not `pos()`; `nonAirCount()` not `blockCount()`
- `BlockRegistry::getType()` returns `const BlockType&` (not pointer) — no `tryGetType()`
- Standard cubes: do NOT add `geometry:` in `.model` file (routes to custom mesh path, breaks AO)
- `hasCustomMesh` flag controls cube renderer (with AO) vs custom geometry path
- `World` is non-copyable (shared_mutex) — use `unique_ptr<World>` for factory
- `unique_ptr<ForwardDeclaredType>` needs full type visible at destructor site — include header
- `InternedString` stored as `uint32_t`, serialized as CBOR tagged string (tag 39); re-interned on load
