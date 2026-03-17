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

| Aspect                 | Mobs                | Player                                                          |
| ------------------------| ---------------------| -----------------------------------------------------------------|
| **Decision source**    | AIBrain (goals)     | Input from graphics-thread proxy                                |
| **Movement authority** | Game thread physics | Graphics-thread proxy for responsiveness; game thread validates |
| **Camera**             | N/A                 | Graphics thread reads proxy position for camera                 |
| **Persistence**        | CBOR per-column     | CBOR in player save file                                        |

The graphics-thread **player proxy** queries a lightweight world shell (loaded chunks)
for collision to avoid visual glitches at frame rate. All mutations (block break, attack,
use) are *actions* sent to the game thread's `MobEntity`, which decides what's real.

Player actions serve the same role as AI goals — they're just input to the entity's
tick cycle. The entity doesn't know or care whether its commands come from a keyboard
or an AIBrain.

### AI Driver Adapter

Both script-driven and C++-driven AI use the same adapter interface:

```cpp
class AIDriver {
public:
    virtual ~AIDriver() = default;

    // Periodic update — called every game tick
    virtual void tick(MobEntity& mob, float dt) = 0;

    // Event delivery — driver receives only events it subscribed to.
    // The game thread looks up the target entity, checks subscriptions,
    // and delivers immediately (not batched — the thread is already awake
    // processing the command queue).
    virtual void onEvent(MobEntity& mob, const finescript::Value& event) = 0;

    // Declare which event types this driver consumes (checked once at registration).
    // Events not in this set are silently dropped for this entity.
    virtual std::vector<InternedId> subscribedEvents() const = 0;
};
```

Three implementations:
- **`ScriptAIDriver`** — delegates to finescript `on` handlers. Subscribes to:
  `damage`, `death`, `strike`, `landing`, `interact`, `spawn`. Default for mobs.
- **`PlayerInputDriver`** — consumes input messages from the graphics thread proxy.
  Subscribes to: `move`, `jump`, `look`, `attack`, `use`, `select_slot`,
  `open_inventory`. Player movement messages from the network are consumed by this
  driver, which translates them into entity actions. First example of C++ AI adapter.
- **`NativeAIDriver`** — loads from a game module shared library (dlopen). For
  compute-intensive AI that games want in C++ (server-side only; client needs no
  special mods, animations still scripted). Subscribes to whatever events it declares.

**Event routing flow:**
1. Action arrives on game command queue (player input, damage, use, etc.)
2. Game thread pops it, identifies target entity from the Value's `:target` field
3. Looks up entity's `AIDriver`, checks `subscribedEvents()`
4. If subscribed, calls `driver->onEvent(mob, event)` immediately
5. If not subscribed, event is silently dropped for that entity

This means `PlayerInputDriver` isn't special — it's just a driver that subscribes
to input event types. A hypothetical "possessed mob" (player controlling a mob)
would work by swapping its driver to a `PlayerInputDriver`.

The `AIDriver` replaces the current `AIBrain` as the top-level decision-maker.
`AIBrain` (with its goal system) becomes one tool *within* `ScriptAIDriver` — scripts
can still use `mob_add_goal` to configure the C++ goal system as an acceleration
primitive, or bypass it entirely and drive behavior from pure script.

**World collision** (preventing entities from walking through walls, step height,
swimming) stays in C++ as physics infrastructure — it's not policy.

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

### A2: AI Driver + Brain Wiring

`configureAIPreset()` exists but is never called. Replace the direct AIBrain model
with the `AIDriver` adapter:

**New files:**
- `include/finevox/core/ai_driver.hpp` — `AIDriver` base, `ScriptAIDriver`, `PlayerInputDriver`
- `src/core/ai_driver.cpp` — implementations

**New native functions** (registered in `finevox_script`):
- `mob_add_goal(id, priority, goal_type, params_map)` — add a goal to entity's AIBrain
- `mob_clear_goals(id)` — remove all goals
- `mob_set_animation(id, slot)` — set animation state
- `mob_apply_impulse(id, vx, vy, vz)` — knockback / jump
- `mob_get_data(id, key)` / `mob_set_data(id, key, value)` — read/write entity DataContainer

**Example onSpawn script** (replaces C++ `configureAIPreset`):
```finescript
on spawn [mob] do
    {mob_clear_goals mob.id}
    {mob_add_goal mob.id 0 "idle" {}}
    {mob_add_goal mob.id 1 "wander" {:range 8.0}}
    {mob_add_goal mob.id 5 "chase" {:range 16.0}}
    {mob_add_goal mob.id 6 "attack" {:damage 3.0 :range 1.5 :cooldown 1.0}}
end
```

### A3: Remove Hardcoded Policy from ai_goals.cpp

Move magic numbers to goal parameter maps (DataContainer or finescript Value):

| Current hardcoded                      | Becomes                                                       |
| ----------------------------------------| ---------------------------------------------------------------|
| `idleDuration = randomFloat(2.0, 5.0)` | `params.get("min_idle", 2.0)` / `params.get("max_idle", 5.0)` |
| `wanderChance = 0.1f`                  | `params.get("chance", 0.1)`                                   |
| `fleeDuration = 5.0f`                  | `params.get("duration", 5.0)`                                 |
| `fleeSpeed = 1.5f`                     | `params.get("speed_mult", 1.5)`                               |
| `recentlyDamaged = 10.0f`              | `params.get("memory", 10.0)`                                  |
| `setAnimation(2)`                      | `params.get("anim_slot", 2)`                                  |

Defaults stay the same — existing behavior unchanged if no params provided.

### A4: finescript Standard Library Audit

Most math, random, and string functions already exist as finescript builtins.
Before adding any new natives, audit what's already available to avoid duplication.
Only add what's confirmed missing after checking `game-language/src/`.

**Tests:** Existing AI tests pass with default params. New tests verify param overrides.

---

## Sub-Phase B: Player Entity Unification

### B1: Player as MobEntity

Currently the player is an `Entity` managed separately. Make it a `MobEntity`:

**Files to modify:**
- `src/core/entity_manager.cpp` — player creation returns `MobEntity*` with `PlayerInputDriver`
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

`ai_type: none` → `PlayerInputDriver` assigned instead of `ScriptAIDriver`.
The driver consumes input messages from the graphics-thread proxy and translates
them into entity actions (move, jump, attack, use).

### B2: Player Script Hooks

`scripts/player.fs` handles player-specific events via the same event hooks as mobs.
Uses finescript's `on` syntax for event handlers:

```finescript
on damage [mob amount source] do
    # Apply armor reduction (read from DataContainer)
    set armor {mob_get_data mob.id "armor"}
    set reduced (amount * (1.0 - armor * 0.04))
    {mob_set_data mob.id "health" (mob.health - reduced)}
end

on death [mob killer] do
    {player_drop_inventory mob.id}
    {show_ui "death_screen"}
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
    driver->onEvent(mob, makeLandingEvent(previousVelocityY));
}
```

**Script policy** (scripts/player.fs):
```finescript
on landing [mob velocity_y] do
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

### C1b: Interest-Based Query Cache

For scripts that need to repeatedly locate specific block or entity types, provide
a registration-based cache system rather than per-tick searches:

```cpp
class InterestCache {
public:
    // Register interest — "I want to know about X within radius R of position P"
    QueryHandle registerInterest(EntityId owner, InterestQuery query);
    void unregister(QueryHandle handle);

    // Returns cached results (may be stale by up to N ticks)
    const std::vector<BlockCoord>& getCachedBlocks(QueryHandle handle) const;
    const std::vector<EntityId>& getCachedEntities(QueryHandle handle) const;

    // Called by EntityManager each tick — updates a subset of registered queries
    // to spread work across frames
    void updateIncremental(World& world, EntitySpatialIndex& entities);
};
```

**Native functions:**
- `register_interest(entity_id, type, params_map)` → handle
  - Entity interest: `{:kind "entity" :type "zombie" :radius 32}`
  - Block interest: `{:kind "block" :type "finevox:ore" :radius 16}`
- `query_interest(handle)` → cached result array (entities or positions)
- `unregister_interest(handle)`

**Incremental update:** Each tick, the cache updates a fraction of registered queries
(round-robin or priority-based). Results are latency-tolerant — a mob looking for the
nearest water source gets an answer that's 0-5 ticks old, which is fine for AI decisions.

### C2: Line-of-Sight Native

DDA raycasting already exists for block targeting. Expose it:

- `mob_can_see(id, target_id)` → bool (raycast between eye positions, checks for solid blocks)
- `raycast_blocks(x, y, z, dx, dy, dz, max_dist)` → hit info map

### C3: Pathfinding (Fix & Expose)

A* pathfinder exists but is dead code. Wire it and expose:

- `mob_pathfind(id, target_x, target_y, target_z, max_dist)` → path array or nil
- Budget-capped: max iterations per call (default 200), returns partial path if exceeded
- Path cached per entity, re-requested only when target moves significantly

**Navigator config** — pathfinder respects mob movement capabilities:

```cpp
struct NavigatorConfig {
    float entityWidth = 0.6f;
    float entityHeight = 1.8f;
    int stepHeight = 1;        // max block height entity can step up
    bool canFly = false;       // ignore gravity constraints
    bool canSwim = false;      // traverse fluid blocks
    bool canClimb = false;     // traverse ladder/vine blocks
    float maxFallDistance = 3;  // blocks willing to drop
};
```

Flying mobs ignore step height limits and gravity in pathfinding. Aquatic mobs
can path through water. Config loaded from `EntityTypeDef` properties or set by
script.

**Native:** `mob_pathfind(id, x, y, z, max_dist, config_map)` — config_map overrides
defaults from EntityTypeDef.

### C4: Flow Field (Horde Scale)

For many mobs targeting one location, individual A* is wasteful. A flow field computes
one BFS from the target outward; each mob reads its next-step from the field in O(1).

**New class:** `FlowField`
- `compute(target, radius)` — BFS outward, stores direction per cell
- `getDirection(position) → Vec3` — which way to walk
- Shared among all mobs targeting the same location
- **Incremental updates** — when terrain changes, dirty affected cells and
  re-propagate from boundaries. Mobs following stale directions temporarily
  walk into walls, then the field adapts within a few ticks and they course-correct.
  No need for perfect real-time consistency.

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
on tick [mob dt] do
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
set health_bar {ui.progress_bar ({player_health} / {player_max_health})
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
Sub-Phase A (Entity fixes + script wiring + stdlib)  ← no deps, pure bugfix
    ├─→ Sub-Phase B (Player unification)     ← needs A (AIDriver, script hooks)
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
