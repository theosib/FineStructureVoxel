# System: Persistence & Serialization

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/data_container.hpp`, `io_manager.hpp`, `resource_locator.hpp`
**Old docs:** [old_docs/11-persistence.md](../../old_docs/11-persistence.md)

---

## Overview

All persistent data uses CBOR (RFC 8949). `DataContainer` is the universal in-memory/serialization container. Region files use a journal-style TOC for crash safety. `IOManager` provides async save/load with thread-safe region file caching. `ResourceLocator` resolves logical path names across engine/game/user/world scopes.

---

## DataContainer

```cpp
#include <finevox/core/data_container.hpp>

// Keys are interned from the global StringInterner (same pool as block types)
DataContainer dc;

// Basic types: int64_t, double, bool, std::string, std::vector<uint8_t>
// Nested DataContainer, std::vector<DataValue>

dc.set("health", int64_t(100));
dc.set("name", std::string("finevox:stone"));
dc.set("sub", std::make_unique<DataContainer>());  // nested

int64_t hp = dc.get<int64_t>("health", 0);  // 0 = default
bool has = dc.has("health");

// Serialization
auto bytes = dc.toCBOR();
auto dc2 = DataContainer::fromCBOR(bytes);  // re-interns keys
```

**Important:** Keys are interned in-memory (O(1) uint32_t lookup). When serialized to CBOR, keys are stored as strings for portability. On load, keys are re-interned (intern IDs not stable across sessions).

**DataKey / DataValue** are namespace-level types, NOT members of DataContainer:
```cpp
// CORRECT
finevox::DataKey key = StringInterner::global().intern("my_key");

// WRONG — DataContainer::DataKey does not exist
DataContainer::DataKey key = ...;
```

**InternedString** in DataContainer: stored as `uint32_t`, serialized as CBOR tagged string (tag 39); must be re-interned on load.

---

## SubChunk Serialization Format (CBOR)

```
SubChunk {
  y: int                        // subchunk Y coordinate
  palette: [string...]          // block type names
  blocks: bytes                 // 8-bit indices (palette ≤256) or 16-bit (palette >256)
  rotations: bytes (optional)   // omitted if all-zero (most subchunks)
  data: {index: DataContainer}  // sparse per-block extra data
  fluid_palette: [string...]    // fluid type names (optional)
  fluid_data: bytes             // packed uint8_t[4096] upper=palette idx, lower=level
}
```

---

## Region Files

Region files cover 64×64 columns each. Two files per region: `.dat` (data) + `.toc` (table of contents).

```cpp
#include <finevox/core/io_manager.hpp>

// Async save/load
IOManager io;
io.init(worldPath);

// Save (fire-and-forget; callback on complete)
io.queueSave(columnPos, column, [](ColumnPos pos, bool success){});

// Load (returns future)
auto future = io.queueLoad(columnPos);
ChunkColumn col = future.get();
```

**TOC semantics:**
- Append-only; latest entry per (x,z) is authoritative
- `ToC::compact()` removes superseded entries to reclaim space
- Crash-safe: incomplete writes leave old entry valid
- Free space tracking: best-fit allocation; tombstoned spans reused on next write

---

## ResourceLocator

```cpp
#include <finevox/core/resource_locator.hpp>

ResourceLocator& rl = ResourceLocator::instance();

// Register roots (done once at startup)
rl.setEngineRoot("engine/");        // engine built-in resources
rl.setGameRoot("resources/");       // game content
rl.setUserRoot("~/.finevox/");      // user configs/saves

// Per-world registration
rl.registerWorld("my_world", "saves/my_world/");
rl.registerDimension("my_world", "overworld", "");  // default dimension

// Path resolution (logical → absolute)
auto path = rl.resolve("game/blocks/stone.model");   // → resources/blocks/stone.model
auto path = rl.resolve("user/config.cbor");          // → ~/.finevox/config.cbor
auto path = rl.resolve("world/my_world/region/");    // → saves/my_world/region/

// Convenience accessors
auto worldPath = rl.worldPath("my_world");
auto dimPath = rl.dimensionPath("my_world", "overworld");
auto regionPath = rl.regionPath("my_world", "overworld");
```

**Scopes:** `engine/`, `game/`, `user/`, `world/<name>/`, `world/<name>/dim/<dim>/`

---

## ConfigManager

```cpp
#include <finevox/core/config_manager.hpp>

// Global engine settings
ConfigManager& cfg = ConfigManager::global();
cfg.init("user/engine_config.cbor");

// Typed accessors
int viewDist = cfg.get<int>("view_distance", 8);
cfg.set("view_distance", 10);
cfg.save();  // persist to CBOR

// WorldConfig — per-world settings
WorldConfig& wc = world.config();
bool compress = wc.compressionEnabled();
```

---

## Entity Persistence

```cpp
// EntitySerializer — stores entities in ChunkColumn DataContainer
// under "entity_data" key as CBOR byte array

EntitySerializer serializer;
auto bytes = serializer.serialize(*entity);        // → CBOR bytes
auto entity = serializer.deserialize(bytes);       // → unique_ptr<Entity>

// EntityManager manages per-chunk entity persistence
entityManager.saveColumnEntities(column);          // writes to column.data()["entity_data"]
entityManager.loadColumnEntities(column);          // reads from column.data()["entity_data"]
```

---

## Gotchas

- Regions are 64×64 columns, not 16×16 — a 64-column region covers 1024 block XZ range
- `ColumnManager::currentlySaving_` set: `get()` returns nullptr while column is being saved (race prevention)
- Rotations omitted from serialized output when all-zero — compresses well, must handle missing field on load
- Per-block data uses sparse index map — don't iterate all 4096 blocks looking for data
- CBOR integer keys not used (strings for portability) but DataContainer is uint32_t in-memory
- `RegionFile` is NOT thread-safe; IOManager serializes access via dedicated save/load threads
- `LZ4 compression`: flag infrastructure in place (ChunkFlags bit in region header), not yet activated (see ROADMAP.md)
