# Phase 24 — Player Survival & Combat

> **Goal:** Establish the C++ scaffolding for combat, survival, and AI — then implement
> game mechanics as finescript policy on top.
>
> **Principle:** C++ provides *acceleration primitives* and *communication bridges*.
> All game rules, damage formulas, stat drain, death behavior, HUD layout, and AI goal
> configuration live in scripts. If finescript performance becomes a bottleneck, we add
> internal acceleration (bytecode, JIT) transparently — not by moving policy back to C++.

---

## Architecture: Player ↔ Entity Unification

The player is a `MobEntity` in the game thread, identical to any mob. Differences:

| Aspect | Mobs | Player |
|--------|------|--------|
| **Decision source** | AIBrain (goals) | Input from graphics-thread proxy |
| **Movement authority** | Game thread physics | Graphics-thread proxy for responsiveness; game thread validates |
| **Camera** | N/A | Graphics thread reads proxy position for camera |
| **Persistence** | CBOR per-column | CBOR in player save file |

The graphics-thread **player proxy** queries a lightweight world shell (loaded chunks)
for collision to avoid visual glitches at frame rate. All mutations (block break, attack,
use) are *actions* sent to the game thread's `MobEntity`, which decides what's real.

Player actions serve the same role as AI goals — they're just input to the entity's
tick cycle. The entity doesn't know or care whether its commands come from a keyboard
or an AIBrain.

---

## Sub-Phase A: Entity System Fixes & Script Wiring

**Fix the broken wiring from Phase 20.** No new features — just connect what exists.

### A1: Wire ScriptEntityHandler Calls

The hooks exist (`onSpawn`, `onTick`, `onDamage`, `onDeath`, `onStrike`) but are
never invoked. Wire them:

**Files to modify:**
- `src/core/mob_entity.cpp` — call `onDamage` from `damage()`, `onDeath` when `isDead()` first detected (don't immediately `markForRemoval`)
- `src/core/entity_manager.cpp` — call `onSpawn` when entity enters world, `onTick` per mob per game tick
- `include/finevox/core/mob_entity.hpp` — add `ScriptEntityHandler*` member (set at spawn)

**Key change in death flow:**
```
Current:  isDead() → markForRemoval() (immediate, no hooks)
New:      isDead() → onDeath(mob, killer) → script decides: play animation,
          drop loot, set timer, THEN call mob.markForRemoval()
```

Scripts get a `mob_remove(id)` native to trigger removal when ready.

### A2: AI Brain Wiring

`configureAIPreset()` exists but is never called. Rather than calling it, **move AI
configuration to script `onSpawn`**:

**New native functions** (registered in `finevox_script`):
- `mob_add_goal(id, priority, goal_type, params_map)` — add a goal to entity's AIBrain
- `mob_clear_goals(id)` — remove all goals
- `mob_set_animation(id, slot)` — set animation state
- `mob_apply_impulse(id, vx, vy, vz)` — knockback / jump
- `mob_get_data(id, key)` / `mob_set_data(id, key, value)` — read/write entity DataContainer

**Example onSpawn script** (replaces C++ `configureAIPreset`):
```finescript
set on_spawn fn [mob] do
    {mob_clear_goals mob.id}
    {mob_add_goal mob.id 0 "idle" {}}
    {mob_add_goal mob.id 1 "wander" {:range 8.0}}
    {mob_add_goal mob.id 5 "chase" {:range 16.0}}
    {mob_add_goal mob.id 6 "attack" {:damage 3.0 :range 1.5 :cooldown 1.0}}
end
```

### A3: Remove Hardcoded Policy from ai_goals.cpp

Move magic numbers to goal parameter maps (DataContainer or finescript Value):

| Current hardcoded | Becomes |
|-------------------|---------|
| `idleDuration = randomFloat(2.0, 5.0)` | `params.get("min_idle", 2.0)` / `params.get("max_idle", 5.0)` |
| `wanderChance = 0.1f` | `params.get("chance", 0.1)` |
| `fleeDuration = 5.0f` | `params.get("duration", 5.0)` |
| `fleeSpeed = 1.5f` | `params.get("speed_mult", 1.5)` |
| `recentlyDamaged = 10.0f` | `params.get("memory", 10.0)` |
| `setAnimation(2)` | `params.get("anim_slot", 2)` |

Defaults stay the same — existing behavior unchanged if no params provided.

**Tests:** Existing AI tests pass with default params. New tests verify param overrides.

---

## Sub-Phase B: Player Entity Unification

### B1: Player as MobEntity

Currently the player is an `Entity` managed separately. Make it a `MobEntity`:

**Files to modify:**
- `src/core/entity_manager.cpp` — player creation returns `MobEntity*` with special flag
- `include/finevox/core/mob_entity.hpp` — `bool isPlayer() const` flag
- Player's `EntityTypeDef` loaded from `resources/entities/player.entity`

**player.entity:**
```
name: finevox:player
max_health: 20
half_extents: 0.35 0.925 0.35
eye_height: 1.65
ai_type: none
script: scripts/player
```

`ai_type: none` → no AIBrain goals. Input comes from the action queue instead.

### B2: Player Script Hooks

`scripts/player.fs` handles player-specific events via the same `onDamage`/`onDeath`
hooks as mobs:

```finescript
set on_damage fn [mob amount source] do
    # Apply armor reduction (read from DataContainer)
    set armor {mob_get_data mob.id "armor"}
    set reduced (amount * (1.0 - armor * 0.04))
    # Clamp and apply
    {mob_set_data mob.id "health" (mob.health - reduced)}
end

set on_death fn [mob killer] do
    # Drop inventory
    {player_drop_inventory mob.id}
    # Show death screen
    {show_ui "death_screen"}
    # Respawn after delay
    {schedule_respawn mob.id 3.0}
end
```

### B3: Fall Damage (Script Policy)

C++ detects landing (velocity transition from downward to `onGround`). Sends a
`"landing"` event with velocity to the entity's script handler.

**C++ addition** (mob_entity.cpp or entity.cpp):
```cpp
// In physics/movement update:
if (onGround_ && !wasOnGround && velocity_.y < -fallDamageThreshold) {
    // Push landing event to script
    scriptHandler->onLanding(mob, previousVelocityY);
}
```

**Script policy** (scripts/player.fs):
```finescript
set on_landing fn [mob velocity_y] do
    if (velocity_y < -10.0) do
        set damage ((0.0 - velocity_y - 10.0) * 0.5)
        {mob_damage mob.id damage "fall"}
    end
end
```

The threshold, formula, and damage type are all script-side.

---

## Sub-Phase C: AI Acceleration Primitives

C++ provides fast operations that scripts call. Scripts direct; C++ executes.

### C1: Spatial Index for Entity Queries

`EntityManager` currently does O(n) scans. Add a grid-based spatial index:

**New class:** `EntitySpatialIndex` (cell size = 16 blocks, matching chunks)
- `insert(EntityId, position)`
- `remove(EntityId)`
- `update(EntityId, oldPos, newPos)`
- `queryRadius(center, radius) → vector<EntityId>`
- `queryAABB(min, max) → vector<EntityId>`

**Native functions:**
- `entities_in_radius(x, y, z, radius)` → array of entity IDs
- `entities_in_box(x1, y1, z1, x2, y2, z2)` → array of entity IDs
- `nearest_entity(x, y, z, radius, type_filter)` → entity ID or nil

**Performance:** Grid lookup is O(cells_in_range) not O(total_entities).
For 1000 mobs, a 16-block radius query touches ~8 cells vs scanning 1000 entities.

### C2: Line-of-Sight Native

DDA raycasting already exists for block targeting. Expose it:

- `mob_can_see(id, target_id)` → bool (raycast between eye positions, checks for solid blocks)
- `raycast_blocks(x, y, z, dx, dy, dz, max_dist)` → hit info map

### C3: Pathfinding (Fix & Expose)

A* pathfinder exists but is dead code. Wire it and expose:

- `mob_pathfind(id, target_x, target_y, target_z, max_dist)` → path array or nil
- Budget-capped: max iterations per call (default 200), returns partial path if exceeded
- Path cached per entity, re-requested only when target moves significantly

### C4: Flow Field (Horde Scale)

For many mobs targeting one location, individual A* is wasteful. A flow field computes
one BFS from the target outward; each mob reads its next-step from the field in O(1).

**New class:** `FlowField`
- `compute(target, radius)` — BFS outward, stores direction per cell
- `getDirection(position) → Vec3` — which way to walk
- Shared among all mobs targeting the same location
- Re-computed when target moves or terrain changes

**Native function:**
- `flow_field_create(target_x, target_y, target_z, radius)` → field handle
- `flow_field_direction(handle, x, y, z)` → direction map
- `flow_field_destroy(handle)`

**Deferred until Phase 25 (Horde Defense)** — not needed for basic combat.

### C5: Batch Sense Scans

Instead of per-mob spatial queries each tick, `EntityManager` does one spatial pass
and distributes results. EntitySenses stores a cached list refreshed every N ticks.

This is internal optimization — transparent to scripts.

---

## Sub-Phase D: Combat Bridge

### D1: Attack Action

New `GameActions` method:
```cpp
void attackEntity(EntityId attacker, EntityId target, finescript::Value damageInfo);
```

`damageInfo` is a flexible Value map:
```
{:type "slash" :amount 5.0 :knockback 0.4 :source attacker_id}
```

The game thread receives this, looks up the target mob, and calls its script
`onDamage(mob, damageInfo)`. The script decides actual damage after armor/resistance.

**Native function for scripts:**
- `attack_entity(attacker_id, target_id, damage_info_map)` — queues the attack action

### D2: Melee Sweep (Script + Native)

When the player clicks attack:
1. Script calls `entities_in_radius(pos, weapon_reach)` (C++ native)
2. Script filters by angle (dot product with look direction — could be another native: `entity_in_cone(pos, dir, angle, range)`)
3. Script calls `attack_entity(player, target, damage_info)` for each hit entity

No special C++ melee system needed — it's composed from primitives.

### D3: Knockback

`mob_apply_impulse(id, vx, vy, vz)` — applies velocity delta on game thread.
Graphics thread sees updated velocity in next EntityState snapshot.

Script computes knockback direction and magnitude based on weapon/damage type.

---

## Sub-Phase E: Survival Stats & HUD (All Script)

### E1: Survival Stats via DataContainer

Player's `MobEntity` has `extra` DataContainer. Script `onTick` manages stats:

```finescript
set on_tick fn [mob dt] do
    # Drain hunger
    set hunger {mob_get_data mob.id "hunger"}
    set new_hunger (hunger - 0.001 * dt)
    {mob_set_data mob.id "hunger" new_hunger}

    # Regen health if well-fed
    if (new_hunger > 15.0) do
        set hp {mob_get_data mob.id "health"}
        {mob_set_data mob.id "health" (hp + 0.5 * dt)}
    end

    # Starving damage
    if (new_hunger <= 0.0) do
        {mob_damage mob.id (1.0 * dt) "starvation"}
    end
end
```

No C++ code for hunger/stamina — pure script policy.

### E2: HUD Bridge Functions

C++ natives that expose read-only player state to UI scripts:

- `player_health()` → float
- `player_max_health()` → float
- `player_get_stat(name)` → float (reads from DataContainer: "hunger", "stamina", etc.)
- `player_hotbar_slots()` → array of slot data
- `player_selected_slot()` → int

### E3: HUD Script (resources/ui/hud.fs)

```finescript
# Health bar
set health_bar {ui.progress_bar {player_health} / {player_max_health}
    =width 200 =height 20 =id "health_bar"
    =overlay {format "HP: %.0f / %.0f" {player_health} {player_max_health}}}

# Hunger bar
set hunger_bar {ui.progress_bar ({player_get_stat "hunger"} / 20.0)
    =width 200 =height 20 =id "hunger_bar"}

set hud_window {ui.window "##hud"
    =window_flags [:no_title_bar :no_resize :no_move :no_background :always_auto_resize]
    =children [health_bar hunger_bar]}
```

Per-frame update reads current values via bridge functions.

### E4: Death Screen Script

```finescript
set death_screen {ui.window "You Died"
    =window_flags [:no_resize :no_collapse :no_move :always_auto_resize]
    =children [
        {ui.text "You were slain"}
        {ui.button "Respawn" =on_click fn [] do
            {player_respawn}
            {hide_ui "death_screen"}
        end}
    ]}
```

---

## Sub-Phase F: Entity Animation Audit (Deferred)

> **NOTE:** The entire AI goal system uses magic animation slot numbers
> (0=idle, 1=walk/run/flee/panic, 2=attack). This needs a proper audit and
> refactor — either named animation states in EntityTypeDef or a script-driven
> animation state machine. Deferred to a separate pass since it's a cross-cutting
> concern affecting rendering, entities, and scripts.

Items to address:
- Named animation states (string → slot mapping in `.entity` files)
- Walk vs run vs panic differentiation (currently all slot 1)
- Death animation support (currently entities vanish instantly)
- Attack animation timing (sync damage frame with animation)
- Script-driven animation (`mob_set_animation` already proposed above)

---

## Sequencing & Dependencies

```
Sub-Phase A (Entity fixes + script wiring)  ← no deps, pure bugfix
    ├─→ Sub-Phase B (Player unification)     ← needs A (script hooks)
    │     └─→ Sub-Phase E (Survival + HUD)   ← needs B (player entity)
    ├─→ Sub-Phase C (Acceleration primitives) ← needs A (entity manager changes)
    │     └─→ Sub-Phase D (Combat bridge)     ← needs C (spatial queries)
    └─→ Sub-Phase F (Animation audit)         ← deferred, independent
```

A and C can partially overlap. E is pure script, no C++ beyond bridge functions.

## Verification

After each sub-phase:
1. `cmake --build build` — compiles cleanly
2. `cd build && ctest` — all tests pass
3. `./build/bin/render_demo` — no crashes, expected behavior
4. New tests for each C++ addition (spatial index, natives, player entity)
