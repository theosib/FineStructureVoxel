# System: UI

**Library:** `apps/render_demo` (UI is app-level, not engine library)
**Dependencies:** finegui (`/Users/theosib/projects/FineStructure/finegui/`), `finevox_script`
**CMake flag:** `FINEVOX_HAS_SCRIPT_GUI` — defined when both finegui and finevox_script available
**Links:** `libfinegui.a` + `libfinegui-script.a` + `libfinegui-retained.a` + `finevox_script`
**UI scripts:** `resources/ui/*.fs` (finescript files)
**Old docs:** [old_docs/finegui-design.md](../../old_docs/finegui-design.md), [old_docs/AI-NOTES.md](../../old_docs/AI-NOTES.md) — Phase 19 section

---

## Overview

All UI is driven by finegui `MapRenderer` + finescript `.fs` files. The UI definitions live in `resources/ui/` as finescript files loaded at runtime. Native C++ functions are registered on the `guiEngine` for UI callbacks (menus, settings, console commands). A deferred action pattern prevents mutation during `renderAll()` iteration. For multiplayer, `ScriptGuiManager::loadUIFromValue()` accepts pre-built Value maps received via CBOR from the server.

---

## Key Types

| Type | Description |
|------|-------------|
| `MapRenderer` | finegui retained-mode renderer; UI state as a nested map of maps |
| `ScriptGuiManager` | Wires finescript UI definitions to MapRenderer; `loadUIFromValue()` for server-sendable UI |
| `VoxelResourceFinder` | Adapts finescript `ResourceFinder` to finevox `ResourceLocator`; resolves `"ui/X.fs"` paths |

---

## UI Files (resources/ui/)

| File | Purpose |
|------|---------|
| `resources/ui/pause_menu.fs` | Pause menu (Resume, Settings, Quit) |
| `resources/ui/overlays.fs` | HUD overlays (debug stats, coordinates) |
| `resources/ui/console.fs` | In-game console widget |
| `resources/ui/hotbar.fs` | Hotbar (8 block slots, selection highlight via `ui.push_color`) |
| `resources/ui/inventory.fs` | Player inventory window (4x9 bag + 2x2 hand-crafting grid + output) |
| `resources/ui/workbench.fs` | Workbench crafting window (3x3 crafting grid + output + bag) |
| `resources/ui/container.fs` | Generic container window template (configurable owner/section/dimensions) |
| `resources/ui/recipe_browser.fs` | Scrollable recipe list showing available recipes + ingredients |
| `resources/ui/hud.fs` | Health + hunger bars (progress_bar widgets, per-frame update via findById) |
| `resources/ui/death_screen.fs` | Death screen with respawn button |

---

## In-Game Console

- Toggle: backtick (`` ` ``) key
- Input context: `InputContext::Chat` (disables game input while open)
- Commands executed via `GameScriptEngine::executeCommand()`
- Pre-loaded `game` map: `fps`, `time`, `day`, `ticks`, `pos`, `fly`, `chunks`
- Persistent `ExecutionContext` — variables survive between commands
- Command history: up/down arrow navigation

---

## Native Functions Registered for UI

### Menu callbacks
```
resume_game()          -- hide pause menu, return to gameplay
quit_game()            -- exit application
show_settings()        -- switch to settings page
show_main_menu()       -- return to main menu
```

### Settings callbacks
```
set_view_distance(n)          -- WorldRenderer::setViewDistance(n)
set_fov(degrees)              -- camera FOV
set_sensitivity(s)            -- mouse sensitivity
set_time_speed(mult)          -- WorldTime speed multiplier
set_freeze_time(bool)         -- freeze/unfreeze day-night cycle
set_lighting(bool)            -- toggle dynamic lighting
```

### Hotbar bridge
```
get_hotbar_slots()         -- returns array of block type names
get_selected_slot()        -- returns selected slot index (0-7)
set_selected_slot(idx)     -- set selected slot index
```

### Inventory & Crafting bridge
```
inv_get owner section slot           -- get item at slot → {=type =count} or nil
inv_set owner section slot item      -- set item at slot
inv_move src_owner src_sec src_slot dst_owner dst_sec dst_slot count  -- move items
inv_swap owner1 sec1 slot1 owner2 sec2 slot2  -- swap two slots
inv_size owner section               -- get slot count
inv_count owner section type         -- count items of type
inv_type owner section slot          -- get item type string or nil
item_icon type_name                  -- get icon info → {=texture =uv0 =uv1} or nil
build_inv_grid owner section rows cols size [click_fn]  -- build slot button grid
build_recipe_list [station]          -- build recipe list widgets
craft_find owner section w h [station]   -- preview recipe match → {=recipe =output =count} or nil
craft_execute owner section w h [station]  -- consume ingredients → {=type =count} or nil
craft_recipes [station]              -- list recipes → [{=recipe =output =count =ingredients}]
```

### Player stats bridge (`PlayerStatsBridge`)
```
player_health()           -- current health (float)
player_max_health()       -- max health (float)
player_get_stat(name)     -- read stat from DataContainer (float)
player_set_stat(name, v)  -- write stat to DataContainer
player_is_alive()         -- bool
player_position()         -- [x, y, z] array
```

### Console commands (also registered as native functions)
```
tp x y z              -- teleport player
set_time ticks        -- set world time
place x y z name      -- place block
break_block x y z     -- break block
print message         -- output to console (overrides finescript default print)
```

---

## Helper Scripts (resources/scripts/)

| File | Purpose |
|------|---------|
| `scripts/inventory_helpers.fsc` | `slot_accept`, `quick_transfer`, `swap_or_stack` — composable inventory operations |
| `scripts/slot_widget.fsc` | `make_slot`, `make_slot_row` — reusable slot button builders with icon support |

---

## Inventory UI Architecture

The inventory system bridges C++ data (`DataContainer`-backed `InventoryView`) to finescript UI:

1. **InventoryBridge** registers "owners" (player, containers) with their `DataContainer` + `NameRegistry`
2. **Native functions** (`inv_get/set/move/swap`) read/write slot data in the DC
3. **`build_inv_grid`** constructs button widget arrays from C++ (handles icon lookup, click handlers)
4. **Per-frame updates** in `render_demo.cpp` sync slot labels from DC to widget `=label` fields via `mapRenderer.findById()`
5. **Crafting preview** — `CraftingHelper::findRecipe()` runs each frame to show output in craft output slot
6. **Cursor item** — "cursor" section (1 slot) holds the item being moved; `swap_or_stack` handles click logic
7. **Workbench interaction** — right-clicking a workbench block opens the 3x3 crafting UI via `open_workbench` action

---

## Debug Stats Overlay

- Toggle: **F7**
- Position: upper-right corner
- Shows: FPS, loaded chunks, triangle count, world time, rendering mode

---

## Key Patterns

### Deferred Action Pattern

Button callbacks are called during `renderAll()` iteration. Mutating state inside the callback causes invalidation. Use a deferred queue:

```cpp
std::vector<std::function<void()>> deferredActions_;

// In native callback:
deferredActions_.push_back([this]{ doTheThing(); });

// After renderAll():
for (auto& action : deferredActions_) action();
deferredActions_.clear();
```

### Per-frame Overlay Updates

```cpp
// Update a specific element by ID
auto elem = mapRenderer.findById("fps_label");
if (elem) elem->set("text", std::to_string(fps));
```

### HiDPI Positioning

**Always use `window->windowSize()`** (screen coordinates) for UI positioning. Do NOT use `window->width()` / `window->height()` (framebuffer pixels) — on HiDPI displays these differ by the content scale factor.

```cpp
auto size = window->windowSize();  // CORRECT — screen coords for UI
float scale = window->contentScale();  // HiDPI scale factor (e.g., 2.0 on Retina)
```

---

## Gotchas

- `show_main_menu()` crossing finescript execution contexts is architecturally awkward — known design issue, see [ROADMAP.md](../ROADMAP.md)
- finegui user guide: `/Users/theosib/projects/FineStructure/finegui/` (local project)
- `VoxelResourceFinder` resolves logical paths like `"ui/pause_menu.fs"` via `ResourceLocator::instance().resolve("game/...")`
- All UI positioning in screen coords (windowSize), not framebuffer pixels
