# FineStructureVoxel — Code Patterns & Gotchas

> Conventions, recurring patterns, and sharp edges. Read before writing new code.

---

## Naming Conventions

| Type | Pattern | Example |
|------|---------|---------|
| Interned ID wrapper | `XxxTypeId` with `.id` public InternedId | `BlockTypeId`, `EntityTypeId`, `FluidTypeId` |
| Singleton registry | `XxxRegistry::global()` private ctor | `BlockRegistry::global()`, `FluidRegistry::global()` |
| Loader | `XxxLoader` with `loadDirectory()` | `FluidLoader`, `BiomeLoader` |
| Proxy (for scripting) | `XxxContextProxy` / `XxxProxy` | `BlockContextProxy`, `DataContainerProxy` |
| Resource files | `resources/Xxx/*.xxx` (lowercase, plural) | `resources/fluids/*.fluid` |
| Config file format | `.model`, `.fluid`, `.biome`, `.feature`, `.entity`, `.sound` | |
| Event symbols in .fsc | Colon prefix: `:place`, `:destroy`, `:tick` | |
| Native script functions | Underscore naming | `mob_health`, `mob_set_health`, not `mob.health` |

---

## Pattern: InternedId / StringInterner

All typed IDs use the same global `StringInterner`. IDs are `uint32_t` wrapped in a named type.

```cpp
// Creating an ID from a string name
auto id = BlockTypeId(StringInterner::global().intern("finevox:stone"));

// Comparing IDs — always use == on the wrapper, not on the InternedId member
if (blockTypeId == AIR_BLOCK_TYPE) { ... }

// Getting the name back
std::string name = StringInterner::global().lookup(blockTypeId.id);

// WRONG — ctor takes InternedId, not string
BlockTypeId bad("finevox:stone");  // won't compile
```

**Gotcha:** `BlockTypeId.id` and `FluidTypeId.id` are public `uint32_t` members, NOT functions. Write `id.id`, not `id.id()`.

---

## Pattern: Singleton Registry

```cpp
BlockRegistry& reg = BlockRegistry::global();
// Private constructor — can't instantiate directly
// Thread-safe after init (shared_mutex for reads, exclusive for writes)
const BlockType& type = reg.getType(blockTypeId);  // returns const ref, not pointer
// No tryGetType() — getType() asserts if not found; register before use
```

---

## Pattern: BlockHandler Extension

```cpp
class MyBlockHandler : public BlockHandler {
public:
    void onPlace(BlockContext& ctx) override {
        // ctx.setBlock() to undo placement
        // ctx.scheduleTick(ticks) to schedule a future tick
        // ctx.getNeighbor(face) to read adjacent block
    }
    void onDestroy(BlockContext& ctx) override { ... }
    void onTick(BlockContext& ctx, TickType type) override { ... }
    bool onFluidUpdate(BlockContext& ctx, FluidTypeId fluid, uint8_t level) override {
        return false;  // false = default behavior
    }
};
// Register in BlockType:
blockType.setHandler(std::make_unique<MyBlockHandler>());
```

**Gotcha:** `onDestroy` is called BEFORE the block is removed. `onPlace` is called AFTER placement (use `ctx.previousType()` + `ctx.setBlock()` to undo).

---

## Pattern: ConfigParser / .model File

Block model files are loaded via `BlockModelLoader`. The `.model` format uses ConfigParser:

```
# .model file
name: finevox:my_block
texture: blocks/my_block.png
collision: shapes/full_block.collision
geometry: shapes/my_block.geom   # Only for non-cube blocks! Omit for standard cubes.
script: scripts/my_block.fsc      # Optional finescript handler
```

**Critical gotcha:** Standard cube blocks MUST NOT have `geometry:` in their .model file. Adding it routes them through the custom mesh renderer and breaks AO calculation.

The `ConfigParser` include resolver receives raw paths WITHOUT extensions. The resolver must add `.model` before searching.

---

## Pattern: Forward Declaration Gotcha

Forward declarations of core types used in worldgen or render MUST be in the outer `finevox::` namespace block, NOT inside `finevox::worldgen::` or `finevox::render::`.

```cpp
// CORRECT — creates finevox::BlockTypeId
namespace finevox {
    class BlockTypeId;
}

// WRONG — creates finevox::worldgen::BlockTypeId (wrong type!)
namespace finevox::worldgen {
    class BlockTypeId;  // DO NOT DO THIS
}
```

---

## Pattern: DataContainer Keys

`DataKey` and `DataValue` are namespace-level types (`finevox::DataKey`), NOT members of `DataContainer`.

```cpp
// CORRECT
finevox::DataKey key = StringInterner::global().intern("my_key");
container.set(key, someValue);

// WRONG — DataContainer::DataKey does not exist
DataContainer::DataKey key = ...;
```

Standard top-level keys: `"inventory"`, `"geometry"`, `"display"`, `"state"`, `"entity_data"`.

---

## Pattern: unique_ptr + Forward Declaration

`std::unique_ptr<ForwardDeclaredType>` needs the full type visible at the point of destruction (destructor). Include the header, don't just forward-declare, when using unique_ptr as a class member.

```cpp
// In header — WRONG if ForwardType is only forward-declared
class MyClass {
    std::unique_ptr<ForwardType> ptr_;  // compiler error in destructor
};

// CORRECT — include ForwardType's header in the .hpp
#include "forward_type.hpp"
class MyClass {
    std::unique_ptr<ForwardType> ptr_;  // OK
};
```

---

## Pattern: Queue / AlarmQueue

```cpp
// AlarmQueue — thread-safe FIFO with alarm-based wakeup
AlarmQueue<SubChunkPos> queue;

// Producer
queue.push(pos);

// Consumer (blocks until item available OR alarm fires)
auto item = queue.waitForWork(alarmTime);  // returns nullopt on alarm/shutdown

// AlarmQueueWithData<K,V> — deduplicating, keeps latest value per key
AlarmQueueWithData<SubChunkPos, MeshRebuildRequest> meshQueue;
meshQueue.push(pos, request);  // deduplicates by key; newer request wins
```

---

## Pattern: BFS Test Enclosure (Light / Fluid Tests)

BFS light propagation has `maxPropagationDistance_ = 256`. Open-air propagation can exhaust the budget in tests, causing incorrect results.

**Always seal test enclosures with thick walls:**
```
// Use 5x5 cross-section or thicker stone walls/floor/ceiling
// Just 1-block walls → BFS leaks through adjacent open air and exhausts budget
// 3+ block walls → BFS stops before reaching open air
```

Similarly for fluid tests: use sealed chambers to prevent unexpected flow paths.

---

## Pattern: FluidRegistry::registerType

```cpp
// CORRECT — takes (name_string, FluidType_struct)
FluidRegistry::global().registerType("finevox:water", waterType);

// WRONG — wrong signature
FluidRegistry::global().registerType(waterType);  // won't compile
```

---

## Pattern: World vs BlockContext for Block Placement

In tests and direct manipulation: use `World::setBlock()` for immediate placement.
In game logic: use `World::placeBlock()` which defers via command queue (requires UpdateScheduler).

```cpp
// Tests — direct
world.setBlock(pos, blockTypeId);

// Game thread — deferred
world.placeBlock(pos, blockTypeId);  // goes through command queue
```

---

## Pattern: std::hash Specialization

```cpp
// Must use fully-qualified type names
namespace std {
    template<> struct hash<finevox::worldgen::BiomeId> {
        size_t operator()(const finevox::worldgen::BiomeId& id) const {
            return std::hash<uint32_t>{}(id.id);
        }
    };
}
// WRONG — finevox::BiomeId ≠ finevox::worldgen::BiomeId
```

---

## Pattern: World is Non-Copyable

`World` contains `std::shared_mutex` (non-copyable). Use `unique_ptr<World>` for factory returns.

```cpp
// Factory pattern
static std::unique_ptr<World> create(config);

// WRONG
World w2 = w1;  // won't compile
```

---

## Pattern: miniaudio Gotcha

NEVER call `ma_sound_uninit()` from the audio completion callback (audio thread). This causes a deadlock.

```cpp
// CORRECT — queue finished sounds, clean up on main thread in update()
void onSoundComplete(ma_sound* sound) {
    finishedSounds_.push(sound);  // thread-safe queue
}
void update() {
    ma_sound* sound;
    while (finishedSounds_.try_pop(sound)) {
        ma_sound_uninit(sound);  // safe — main thread
        delete sound;
    }
}
```

---

## Pattern: setMaxHealth Before setHealth

`setHealth()` clamps to `maxHealth_`. Call `setMaxHealth()` first.

```cpp
entity.setMaxHealth(100.0f);  // FIRST
entity.setHealth(100.0f);     // THEN (clamped to 100, correct)
// WRONG order:
entity.setHealth(100.0f);     // clamped to default maxHealth (e.g., 20) → only 20
entity.setMaxHealth(100.0f);  // too late
```

---

## Compiler / Build Notes

- finescript headers use `.h` not `.hpp` (unlike all other project headers)
- `#define MINIAUDIO_IMPLEMENTATION` in exactly ONE file: `src/audio/audio_engine.cpp`
- `glm::pi<float>()` requires `#include <glm/gtc/constants.hpp>` (not in default glm headers)
- `FINEVOX_HAS_SCRIPT_GUI` CMake define: defined when both finegui and finevox_script available
- Tests link `finevox_worldgen` (gets `finevox` core transitively)
