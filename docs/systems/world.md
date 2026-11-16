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
    MoveRule moveRule() const;        // how a block mover treats it (see Block Movers)
    uint32_t gripGroup() const;       // interned; 0 = grips nothing

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

// A block's data goes when a block of another family takes its cell (BlockPropertyRegistry::
// sameFamily: the name without properties or namespace). A state change (a kiln lighting
// up) keeps it; stone in its place doesn't. Data written in place marks nothing: call
// World::noteBlockDataChanged(pos) so the column is saved.

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

## Replacing a block outside block events

Breaking and placing through block events run the handlers. Anything else that replaces a
block calls `beforeBlockReplaced(world, pos, newType)` first and `afterBlockReplaced(world, pos,
oldType)` after (`block_handler.hpp`): when the family changes, the old block's `onDestroy` runs
with its data still there and its scheduled ticks are cancelled, and the new block's `onPlace`
runs. `world_set_block`, `explode`, a block mover's broken cells, and placing over a block do
this. Blocks without handlers cost nothing.

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

// Watching changes: blockChanged / columnAdded / columnRemoved
world.addBlockObserver(std::make_shared<MyObserver>());
world.notifyBlockChanged(pos);      // after writing a subchunk directly
```

A `BlockObserver` hears of every block type change World makes (`setBlock`, `setBlocks`,
`BlockContext::setBlock`) and every column added or removed, from whichever thread made it and
often under World's writer lock: it should record and act later, never call back into World.
Columns a `ColumnManager` brings and takes away reach it too (`GameSession` calls
`notifyColumnAdded`/`notifyColumnRemoved`). The observer list has its own lock, taken last.
`SignalBinding` is one.

---

## Block Movers

```cpp
#include <finevox/core/block_mover.hpp>

MoveRequest r{.start = pos, .dir = Face::PosX, .pull = false, .limit = 32, .anchor = moverPos};
if (auto plan = planMove(world, r)) applyMove(world, *plan, r.dir, &changed);
```

Moving one block one cell (a ram, a lift, a sliding wall) moves a group. Two things each block
type says decide it, both `.model` keys that can differ by state (`set:out=true: move=fixed`):

| `move:` | pushed (a moving block goes into its cell) | dragged (gripped, or pulled by the mover) |
|---|---|---|
| `moves` (default) | goes | goes |
| `fixed` | stops the whole move | stays |
| `breaks` | becomes air | stays |
| `no-drag` | goes | stays |

`move-grip: <group>` makes a block grip its six neighbours: when it goes, every neighbour not
straight ahead of it (that one is pushed) is dragged, except a gripping block of another group.
Same-group blocks hold each other, so a mass of them goes as one. What goes pushes what's ahead of
it and drags what it grips, until the group closes. The move fails, changing nothing, if a `fixed`
block or the anchor is pushed, or more than `limit` blocks would go (broken ones don't count).
`pull` drags the first block instead of pushing it, so a mover can't pull a `no-drag`, `fixed` or
`breaks` block. The anchor is the mover's own cell: never moved, never gripped.

`applyMove` empties the `breaks` cells, then moves every block in `moves` one cell together, with
its rotation and block data. It writes raw (`setBlock`; observers hear it, handlers don't), so the
caller relights and remeshes; the script native `world_push_blocks` does all of that.

**What a block has elsewhere goes with it.** Rotation and block data live in the subchunk and move
with the block. Anything kept by position somewhere else has to be told: `applyMove` ends with
`World::notifyBlocksMoved(moves)`, a list of `BlockMove{from, to}`, which reaches every
`BlockObserver::blocksMoved` (after the cells' `blockChanged`, with no World lock held) and the
`UpdateScheduler`, whose scheduled ticks for a `from` cell fire at its `to` cell instead.
`SignalBinding` carries its own (signals.md). Scripts hear `on :moved` (`moved.from`, `moved.to`)
from `world_push_blocks` before it returns, so a game moves what its tables keep by cell. Read every
`from` before writing any `to`: a move's cells overlap.
`World::moveBlocks` is the older single-line form, with no rules.

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
