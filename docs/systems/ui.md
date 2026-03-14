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

### Console commands (also registered as native functions)
```
tp x y z              -- teleport player
set_time ticks        -- set world time
place x y z name      -- place block
break_block x y z     -- break block
print message         -- output to console (overrides finescript default print)
```

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
