# Phase 23: Input Actions, Inventory UI & Crafting Integration

> **Status:** Complete — all sub-phases implemented
> **Depends on:** Phase 22 (Flexibility Initiative), Phase 22-1 (Recipe data model)
> **Prerequisite data model:** ItemStack, InventoryView, Recipe, RecipeRegistry, CraftingHelper — all complete

---

## Design Philosophy

Every component follows the engine's flexibility principle: non-hot-path systems use flexible
serializable data (finescript Values, DataContainer, config files) rather than rigid C++ structs.
The client is thin — the server sends scripts, assets, configs, and the client renders and
translates user input into named actions.

**Single-player and multiplayer are the same path.** Messages always flow through the action
queue. In single-player the network layer is absent (or a trivial passthrough). The only
divergence is that in multiplayer, server-side scripts validate actions and the server's
response is authoritative. The client runs the same scripts speculatively for responsiveness.

**C++ provides fast primitives; scripts compose them.** InventoryView operations (add, take,
swap, count) are C++ for server performance. Slot acceptance logic, UI layout, drag-drop
behavior, and crafting workflows are scripts — different games write different scripts.
Hot-path operations (bulk recipe matching, mass transfers) have C++ implementations that
scripts can call.

**Inventory slots are message receivers.** The UI knows which widgets are inventory slots.
When a user drops an item onto a slot, the slot's handler is called with the item as an
argument. The handler returns what it didn't accept (remainder). This is script code, but
it typically delegates to C++ inventory primitives. Arrays of identical slots share the same
handler code.

**Unified inventory updates.** Whether a transfer was initiated by the player clicking, a
hopper pushing, another player, or an admin command, the client receives the same
`inventory_update` message with the new slot contents. The client doesn't need to know
*why* the inventory changed, just *what* changed.

---

## Sub-Phase A: Input Action System

**Goal:** Context-aware, config-driven input bindings where key/mouse events map to named actions
with finescript argument expressions. The client translates physical input to `(action_name, args)`
Values; scripts handle the rest.

### Existing Infrastructure

- `InputManager` (finevk): priority-ordered listener chain, fat events with full InputState,
  action mapping (`mapAction`/`isActionActive`), cursor modes
- `KeyBindings` (finevox core): simple struct (action, keyCode, isMouse), ConfigManager persistence
- `InputContext` enum: Gameplay, Menu, Chat
- `ResourceLocator`: resolves logical paths like `"game/input/default.bindings"` to filesystem
- render_demo.cpp: hardcoded listener chain with priority 200/300/500

### New/Modified Components

#### 1. Input binding config format (`.bindings` files)

```
# resources/input/default.bindings
context: gameplay
  key_e: open_inventory
  key_escape: open_menu
  key_tab: cycle_hotbar
  key_1: select_hotbar_slot 0
  key_2: select_hotbar_slot 1
  keypress_w: begin_move_forward
  keyrelease_w: end_move_forward
  click_left: primary_action
  click_right: secondary_action
  mod_shift+click_left: secondary_action {=shift true}
  scroll: scroll_hotbar

context: inventory
  key_e: close_inventory
  key_escape: close_inventory
  click_left: inventory_interact :left
  click_right: inventory_interact :right
  mod_shift+click_left: quick_transfer :left

context: menu
  key_escape: close_menu

context: chat
  key_escape: close_chat
  keypress_enter: submit_chat
```

Event spec grammar:
- `key_<name>` — key typed (press + release)
- `keypress_<name>` / `keyrelease_<name>` — separate press/release
- `click_<name>` — mouse button click (press + release)
- `mousepress_<name>` / `mouserelease_<name>` — separate mouse press/release
- `mod_<modifier>+<event>` — modifier prefix (shift, ctrl, alt, super; combinable)
- `scroll` — mouse wheel; delta passed as argument, receiver ignores for discrete scrolling

Key names map to GLFW codes via a lookup table (e.g., `e` → GLFW_KEY_E, `space` → GLFW_KEY_SPACE,
`left` → mouse button 0).

#### 2. InputActionSystem (new, finevox core)

```
include/finevox/core/input_action_system.hpp
src/core/input_action_system.cpp
```

```cpp
class InputActionSystem {
public:
    // Load bindings from config file (filesystem path)
    void loadBindings(const std::string& path);
    void loadBindingsFromString(const std::string& content);

    // Set active context (changes which bindings are live)
    void setContext(InputContext ctx);
    InputContext context() const;

    // Process a raw input event → returns action Value (or nil if unbound)
    // The returned Value is: {=type :action =name "action_name" =args <value>}
    finescript::Value processEvent(const finevk::InputEvent& event);

    // Register context names beyond the built-in three
    void registerContext(const std::string& name);

    // Query current bindings (for settings UI)
    struct Binding {
        std::string eventSpec;
        std::string actionName;
        std::string argExpression;  // finescript source
    };
    std::vector<Binding> getBindingsForContext(InputContext ctx) const;

private:
    struct CompiledBinding {
        EventSpec spec;             // parsed event specification
        std::string actionName;
        finescript::Value args;     // pre-compiled argument expression (nil if no args)
    };

    std::unordered_map<InputContext, std::vector<CompiledBinding>> bindings_;
    InputContext currentContext_ = InputContext::Gameplay;
};
```

#### 3. ActionDispatch (new, finevox core)

```
include/finevox/core/action_dispatch.hpp
src/core/action_dispatch.cpp
```

```cpp
class ActionDispatch {
public:
    using ActionHandler = std::function<void(const finescript::Value& args)>;

    // Register a named action handler
    void registerAction(const std::string& name, ActionHandler handler);

    // Dispatch an action by name + args
    void dispatch(const std::string& actionName, const finescript::Value& args);

    // Dispatch a full action Value (reads =name and =args fields)
    void dispatch(const finescript::Value& actionValue);
};
```

Same code path for single-player and multiplayer. In multiplayer the client serializes the
action Value and sends it over the network; the server deserializes and dispatches. The client
also dispatches locally for prediction.

#### 4. Movement as begin/end events

Replace per-frame `isActionActive()` polling with begin/end action events:

```
keypress_w: begin_move_forward
keyrelease_w: end_move_forward
```

The `begin_move_forward` handler sets a flag on PlayerController. The `end_move_forward`
handler clears it. This makes movement bindings fully configurable and network-serializable.

**Safety against stuck keys:** Any context switch (e.g., opening inventory) sends `end_*`
for all active movement actions. The InputActionSystem tracks which begin actions are
active and auto-cancels them on context change. A begin might also get canceled by something
else happening (e.g., a force-teleport clears movement state).

#### 5. Integration in render_demo.cpp

Uses `ResourceLocator` to resolve the bindings file path (virtual path `"game/input/..."`)
to a filesystem path.

Replace the hardcoded listener chain (~400 lines of key/mouse handling) with:

```cpp
auto bindingsPath = ResourceLocator::instance().resolve("game/input/default.bindings");
InputActionSystem inputActions;
inputActions.loadBindings(bindingsPath.string());

ActionDispatch actionDispatch;
actionDispatch.registerAction("open_inventory", [&](auto& args) { ... });
actionDispatch.registerAction("primary_action", [&](auto& args) { ... });
actionDispatch.registerAction("begin_move_forward", [&](auto& args) {
    playerController.setMoveForward(true);
});
actionDispatch.registerAction("end_move_forward", [&](auto& args) {
    playerController.setMoveForward(false);
});
// ... etc

// In input listener (replaces the big switch statements):
inputManager.addListener(InputPriority::Game, [&](const InputEvent& e) {
    auto action = inputActions.processEvent(e);
    if (!action.isNil()) {
        actionDispatch.dispatch(action);
        return ListenerResult::Consumed;
    }
    return ListenerResult::Reject;
});
```

#### 6. Tests

```
tests/test_input_action_system.cpp
```

- Parse `.bindings` format with multiple contexts
- Event spec parsing (key, keypress, click, mousepress, modifiers, scroll)
- Context switching changes active bindings
- Context switch auto-cancels active begin actions
- Finescript argument compilation and evaluation
- ActionDispatch registration and dispatch
- Round-trip: event → action Value → dispatch

### Files Summary (Sub-Phase A)

| File | Action |
|------|--------|
| `include/finevox/core/input_action_system.hpp` | New |
| `src/core/input_action_system.cpp` | New |
| `include/finevox/core/action_dispatch.hpp` | New |
| `src/core/action_dispatch.cpp` | New |
| `resources/input/default.bindings` | New |
| `tests/test_input_action_system.cpp` | New |
| `examples/render_demo.cpp` | Modify — replace hardcoded input with action system |
| `CMakeLists.txt` | Modify — add new sources and test |

---

## Sub-Phase B: Label Registry + Inventory Primitives

**Goal:** Convenient localized string lookup via `L` function. Expose InventoryView operations
to scripts as fast C++ native functions. Named inventories via DataContainer children (no new
C++ types for layout/constraints — that's script policy).

### B1: Label Registry

```
include/finevox/core/label_registry.hpp
src/core/label_registry.cpp
```

```cpp
class LabelRegistry {
public:
    static LabelRegistry& global();

    void loadFile(const std::string& path);
    void loadFromString(const std::string& content);
    std::string_view get(std::string_view key) const;  // returns key if not found
    std::string format(std::string_view key, const std::vector<std::string>& args) const;
    bool has(std::string_view key) const;
    void clear();
    void loadFromValue(const finescript::Value& labelMap);  // server-sent

private:
    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> labels_;
    mutable std::shared_mutex mutex_;
};
```

Config format (`.lang` files):

```
# resources/lang/en.lang
inventory.title: Inventory
inventory.crafting: Crafting
hotbar.slot: Slot {0}
item.stone.name: Stone
block.furnace.name: Furnace
```

Finescript function `L` registered for convenient lookup:

```finescript
# Simple lookup
{ui.button {L "inventory.title"} =width 200}

# With format args
{ui.text {L "hotbar.slot" idx}}
```

### B2: Inventory Native Functions

Expose InventoryView operations as C++ native functions callable from scripts. These are the
fast primitives that scripts compose for game-specific behavior.

No new C++ types for layout or constraints. Named inventories are a convention: a DataContainer
has named children, each wrapped with InventoryView. Scripts decide what sections exist and
what each slot accepts.

```
inv_get dc section slot        → item map or nil
inv_count dc section slot      → int
inv_type dc section slot       → string or nil
inv_size dc section            → int
inv_set dc section slot item   → nil
inv_clear dc section slot      → nil
inv_add dc section type count  → int (remainder)
inv_take dc section type count → int (taken)
inv_swap dc_a sec_a slot_a dc_b sec_b slot_b → bool
inv_move dc_src sec_src slot_src dc_dst sec_dst slot_dst count → int (remainder)
inv_init dc section slot_count → nil
```

### B3: Script-side inventory composition

Scripts build game-specific inventory behavior from primitives. See
`resources/scripts/inventory_helpers.fsc` for slot_accept, quick_transfer, etc.

### Files Summary (Sub-Phase B)

| File | Action |
|------|--------|
| `include/finevox/core/label_registry.hpp` | New |
| `src/core/label_registry.cpp` | New |
| `resources/lang/en.lang` | New |
| `resources/scripts/inventory_helpers.fsc` | New |
| `tests/test_label_registry.cpp` | New |
| `tests/test_inventory_natives.cpp` | New |
| `examples/render_demo.cpp` | Modify — register L and inv_* native functions |
| `CMakeLists.txt` | Modify |

---

## Sub-Phase C: Block/Item Icon Widgets

**Goal:** Convenient script functions for displaying block and item previews in UI.
Leverages finegui's existing SceneTexture and TextureRegistry.

### IconAtlas (new, finevox_render)

Pre-renders block type previews lazily on first request. Uses the engine's existing
namespaced naming system for icon texture names.

```cpp
class IconAtlas {
public:
    IconAtlas(finegui::GuiSystem& gui, finevk::Renderer& renderer);
    std::string getBlockIcon(BlockTypeId blockType);  // lazy render + register
    std::string getItemIcon(ItemTypeId itemType);      // block preview or sprite
    std::string getFluidIcon(FluidTypeId fluidType);
    void preRenderAll();
    static constexpr uint32_t ICON_SIZE = 48;
};
```

Script functions: `{block_icon "finevox:stone"}`, `{item_icon "finevox:stone"}`.

Server-sent assets: The server sends block/item model definitions. The client builds
icons locally from those models.

### Files Summary (Sub-Phase C)

| File | Action |
|------|--------|
| `include/finevox/render/icon_atlas.hpp` | New |
| `src/render/icon_atlas.cpp` | New |
| `resources/ui/hotbar.fs` | Modify — use image buttons |
| `examples/render_demo.cpp` | Modify — initialize IconAtlas, register script functions |
| `tests/test_icon_atlas.cpp` | New |
| `CMakeLists.txt` | Modify |

---

## Sub-Phase D: Inventory UI Scripts + Crafting Integration

**Goal:** Server-sent finescript UI definitions for player inventory, crafting grid, and
container access. Demo scripts serve as reference implementations.

### Cursor Item Model

The cursor item (what the player is "holding") is a 1-slot inventory. It represents what's
to be transferred, which might be only part of the source stack. The source keeps its items
until the drop completes. On drop, `inv_move` transfers from cursor slot to target. On partial
acceptance, the cursor slot retains the remainder. If dropped on ground, the transfer creates
a new item entity. No items are ever at risk.

### Generalized Transfers

Entity/player and block inventory slots are all the same — transfers work between any kinds
of inventories. Only special cases (fuel filtering, etc.) do something different, via per-slot
script handlers. Bulk transfers (shift-double-click to move all of a type, shift-drag across
slots) are script operations composing `inv_move` in loops.

### Script Architecture

Complex inventory UIs are built by composing simpler script building blocks via `source`:

```
resources/scripts/slot_widget.fsc        # Reusable slot widget builder
resources/scripts/inventory_helpers.fsc  # Shared slot/transfer logic
resources/ui/inventory.fs                # Player inventory window
resources/ui/container.fs                # Generic container window
resources/ui/crafting_grid.fs            # Crafting grid component
```

### Client/Server Interaction

Same path for single and multiplayer:

```
User clicks slot → Script handler called
  → Script calls inv_move (or other primitive)
  → Primitive sends game action Value through queue
  → Game thread (= "server") processes action
  → Game thread validates and executes
  → Result sent back as inventory_update event
  → Client applies update (corrects any prediction mismatch)

In multiplayer: the queue goes over the network.
In single-player: the queue is local (zero-copy).
```

The `inventory_update` message is the same whether the transfer was player-initiated, caused
by a hopper/pipe, another player, or an admin command. One message type, one handler.

### Files Summary (Sub-Phase D)

| File | Action |
|------|--------|
| `resources/ui/inventory.fs` | New |
| `resources/ui/container.fs` | New |
| `resources/scripts/slot_widget.fsc` | New |
| `resources/blocks/furnace.fsc` | New (or modify existing) |
| `examples/render_demo.cpp` | Modify — register inventory native functions, wire open/close |
| `tests/test_inventory_ui_bridge.cpp` | New |
| `CMakeLists.txt` | Modify |

---

## Sequencing & Dependencies

```
Sub-Phase A (Input Actions)  ←── no deps (foundational)
  │
  ├─→ Sub-Phase B (Labels + Inv Primitives)  ←── independent, but A provides action dispatch
  │
  ├─→ Sub-Phase C (Icon Widgets)  ←── independent of A and B
  │
  └─→ Sub-Phase D (Inventory UI + Crafting)  ←── needs A + B + C
```

A, B, and C can be developed in parallel. D integrates everything.

---

## Resolved Design Decisions

1. **Movement:** Begin/end events, not polling. Context switches auto-cancel active movements.
   Begin actions can also be canceled by external events (teleport, etc.).

2. **Crafting grid:** Just an inventory. Persistence is script policy. Leftover items go back
   to main inventory or are dropped — never lost.

3. **Slot filtering:** Script-side. Scripts call C++ tag-checking primitives for performance.
   Server-side script validates the final result.

4. **Inventory size:** Defined by scripts via `inv_init`. Enforced server-side.

5. **Cursor item on context switch:** Nothing happens. The cursor is purely visual — no
   transfer has occurred, so no items are at risk.

6. **Label lookup:** `L` function — `{L "key"}` or `{L "key" arg0 arg1}`.

---

## Script vs C++ Balance

| What | Where | Why |
|------|-------|-----|
| InventoryView operations (add/take/swap/move) | C++ native functions | Server performance at scale |
| Recipe matching (`CraftingHelper::findRecipe`) | C++ native function | Algorithmic, potentially many recipes |
| Slot acceptance / filtering | Script | Game-specific policy |
| UI layout | Script (.fs files) | Server-sent, game-specific |
| Drag-drop state machine | Script | Game-specific behavior |
| Item transfer validation | Script (server-side) | Game rules, anti-cheat |
| Label lookup | C++ (`LabelRegistry`) | Fast, thread-safe |
| Key binding → action name | C++ (`InputActionSystem`) | Must be responsive |
| Action handlers | Script (via `ActionDispatch`) | Game-specific behavior |

C++ implements **operations** (fast, reusable primitives). Scripts implement **policy**
(game-specific composition of operations).

---

## Verification

After each sub-phase:
1. `cmake --build build` — compiles cleanly
2. `cd build && ctest` — all tests pass
3. `./build/bin/render_demo` — app runs, expected behavior:
   - (A) Input bindings loaded from config, actions dispatched, movement via begin/end
   - (B) Labels display localized strings, inv_* functions work from scripts
   - (C) Hotbar shows block preview icons
   - (D) E opens inventory, drag-drop works, crafting produces output

---

## User notes

Some inventory moves, like bulk moves, require searching the inventory slots to decide how to combine stacks. This can all be done client-side, relying on the server only to validate that the chosen moves are all allowed. Only server-only moves (e.g. hoppers moving items into storage) require this kind of search to be done server-side.
