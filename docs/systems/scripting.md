# System: Script Integration

**Library:** `finevox_script` (`finevox::script::`)
**Headers:** `include/finevox/script/`
**External dep:** finescript at `/Users/theosib/projects/FineStructure/game-language/` (headers: `.h` not `.hpp`)
**Old docs:** [old_docs/AI-NOTES.md](../../old_docs/AI-NOTES.md) — Phase 17 section

---

## Overview

`finevox_script` integrates the `finescript` language interpreter into the engine. The integration uses a **shared StringInterner** so block/entity IDs and script symbols live in the same namespace. `ScriptBlockHandler` delegates block events to finescript closures; `ScriptEntityHandler` does the same for entities.

---

## Key Types

| Type | Description |
|------|-------------|
| `GameScriptEngine` | Central owner: `ScriptEngine`, `ScriptCache`, native function registration |
| `FineVoxInterner` | Adapts `StringInterner::global()` to `finescript::Interner` — shared symbol IDs |
| `ScriptCache` | File-mtime aware script loading; hot-reloads on file change |
| `ScriptBlockHandler` | `BlockHandler` subclass; owns persistent `ExecutionContext`; caches event closures |
| `ScriptEntityHandler` | Like ScriptBlockHandler but for entity events |
| `BlockContextProxy` | `finescript::ProxyMap` wrapping `BlockContext`; pre-interned field IDs |
| `DataContainerProxy` | `finescript::ProxyMap` wrapping `DataContainer`; uint32_t keys = zero overhead |
| `EntityContextProxy` | `finescript::ProxyMap` wrapping `MobEntity` for script field access |
| `EventSymbols` | Pre-interned symbol cache singleton for all event Value map field names |
| `event_value.hpp` | Builder functions: `makeBlockPlacedValue()`, `makeSoundEventValue()`, etc. — build `finescript::Value` maps for all event types |

---

## Block Script Lifecycle

1. `.model` file specifies `script: scripts/my_block.fsc`
2. `BlockModelLoader` stores path in `BlockModel::script_`
3. At registration time, `ScriptBlockHandler` is created with the script path
4. On first event, `ScriptCache::get()` loads and compiles the script
5. Closures for each event type are cached (not re-looked-up each call)

---

## Event Symbols

Block events (use `on :symbol` in .fsc files):

| Symbol | When fired |
|--------|-----------|
| `:place` | Block placed in world |
| `:destroy` | Block broken |
| `:tick` | Game tick (if `wantsGameTicks()` = true) |
| `:neighbor_updated` | Adjacent block changed |
| `:block_update` | Generic block update |
| `:interact` | Player right-clicks block |
| `:strike` | Player left-clicks block |
| `:repaint` | Mesh needs repaint |

Entity events: `:spawn`, `:tick`, `:damage`, `:death`, `:interact`, `:strike`

Face symbols: `:pos_x`, `:neg_x`, `:pos_y`, `:neg_y`, `:pos_z`, `:neg_z`

---

## Native Functions Registered in GameScriptEngine

### World/Block functions (`world.*` in older API, now direct names)
```
place pos_x pos_y pos_z block_name   -- place a block
break_block pos_x pos_y pos_z        -- break a block
tp pos_x pos_y pos_z                 -- teleport player
set_time ticks                       -- set world time
print message                        -- print to console (overrides finescript default)
```

### Context functions (in block handler scripts, `ctx.*`)
```
ctx.block_type                       -- current block type name
ctx.position                         -- {x, y, z} map
ctx.rotation                         -- current block rotation
ctx.set_block type_name              -- change block type
ctx.schedule_tick ticks              -- schedule future tick
ctx.get_neighbor face_symbol         -- block type at adjacent face
```

### Fluid native functions
```
fluid_at x y z          -- returns FluidTypeId name (or nil)
fluid_level x y z       -- returns level 0-15
fluid_place x y z name level  -- place fluid
fluid_remove x y z      -- remove fluid
fluid_set_level x y z level   -- change level only
```

### Entity/mob native functions (underscore naming)
```
mob_health entity_id           -- get health
mob_set_health entity_id h     -- set health
mob_position entity_id         -- get {x,y,z}
mob_move_to entity_id x y z    -- move entity
mob_spawn type_name x y z      -- spawn mob, returns EntityId
```

---

## Pre-game Data Access (Console Scripts)

The in-game console loads a `game` map before executing commands:

```fsc
-- Available pre-loaded in all console commands:
game.fps        -- current FPS
game.time       -- current world ticks
game.day        -- current day number
game.ticks      -- total ticks
game.pos        -- player position {x,y,z}
game.fly        -- fly mode enabled (bool)
game.chunks     -- loaded chunk count
```

The console uses a **persistent ExecutionContext** so variables persist across commands.

---

## Key APIs

```cpp
// GameScriptEngine
auto engine = std::make_unique<GameScriptEngine>(world, session);
engine->registerNativeFunctions();  // registers all ctx.*,world.*,mob_* functions

// Execute a command string (console)
engine->executeCommand("tp 0 64 0");

// ScriptCache
ScriptCache cache;
auto script = cache.get("scripts/my_block.fsc");  // hot-reload on file change

// BlockContextProxy — pre-intern field names once
static const auto fieldPos = StringInterner::global().intern("position");
proxy.set(fieldPos, value);  // O(1) lookup, no string compare
```

---

## Gotchas

- finescript headers use `.h` not `.hpp`: `#include <finescript/script_engine.h>`
- `#define MINIAUDIO_IMPLEMENTATION` is in audio, not script — no similar macro needed here
- `ScriptEntityHandler` caches event closures per event type on first call (not re-fetched)
- `BlockContextProxy` uses **pre-interned** field IDs (stored as static singletons) — not string lookup per call
- `DataContainerProxy` uses `uint32_t` keys = zero overhead, identical to direct DataContainer access
- Persistent `ExecutionContext` in console retains variables across commands — intended behavior
- `CMake`: `FINEVOX_HAS_SCRIPT_GUI` defined when both finegui and finevox_script available
- finegui script links: `libfinegui.a` + `libfinegui-script.a` + `libfinegui-retained.a` + `finevox_script`
