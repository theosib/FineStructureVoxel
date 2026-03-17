# System: Entities

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/entity*.hpp`, `ai_brain.hpp`, `pathfinder.hpp`, `skeleton.hpp`, `animation_controller.hpp`
**Old docs:** [old_docs/25-entity-system.md](../../old_docs/25-entity-system.md) (1685 lines)

---

## Overview

Phase 24A/B complete (2099 tests). Entities use `EntityTypeId` (interned), loaded from `.entity` files. `MobEntity` extends `Entity` with AI brain, senses, health, and movement commands. AI uses priority-based goal selection with configurable param structs. A* pathfinding over block grid. Skeletal animation with crossfade blending. EntityRenderer on graphics thread interpolates snapshots. CBOR serialization per-column. Full script event handler support.

**Phase 24A/B additions:** AIDriver adapter pattern (BrainAIDriver, PlayerInputDriver), MobEventHooks for script lifecycle callbacks, AI goal parameterization (all magic numbers replaced with param structs), player is now a MobEntity with PlayerInputDriver, landing detection for fall damage, entity-to-script bridge natives for HUD.

---

## Key Types

| Type | Description |
|------|-------------|
| `EntityTypeId` | Interned ID; `.id` is public `uint32_t` |
| `EntityTypeDef` | Data struct loaded from `.entity` files; size, health, behavior config; `properties` DataContainer for mod-extensible attributes |
| `EntityTypeRegistry` | Singleton; `EntityTypeRegistry::global()` |
| `Entity` | Base class: position, velocity, AABB, events |
| `MobEntity` | Extends Entity: AIBrain, EntitySenses, health/combat, movement commands |
| `EntityId` | Defined in `entity_state.hpp` (not `block_event.hpp`) |
| `AIBrain` | Priority-based goal selector; selects highest-priority valid goal each tick |
| `Goal` / `GoalSelector` | Abstract interface; `isValid()` checked each tick |
| `EntitySenses` | Vision (cone + distance), hearing (radius) |
| `Pathfinder` | A* over block grid; uses entity `maxStepHeight()` for walkability |
| `Skeleton` | Bone hierarchy with parent-relative transforms |
| `AnimationClip` | Frame-based bone keyframes; sample at arbitrary time |
| `AnimationController` | Manages clips with crossfade blending; updates bone transforms each tick |
| `EntityRenderer` | Graphics thread; processes `GraphicsEventQueue`; interpolates between snapshots |
| `GraphicsEventQueue` | `Queue<GraphicsMessage>` — POD snapshots + `finescript::Value` events; `AlarmQueue`-based |
| `SpawnManager` | Rules-based surface spawning; mob cap per region; evaluates `SpawnPredicateRegistry` |
| `SpawnPredicateRegistry` | Extensible spawn conditions; named predicates with AND logic; `SpawnPredicateRegistry::global()` |
| `SpawnerBlockHandler` | Block handler for spawner blocks (data-driven, per-block config) |
| `ScriptEntityHandler` | `BlockHandler`-like for entities; caches finescript event closures |
| `EntityContextProxy` | `finescript::ProxyMap` wrapping `MobEntity` for script access |
| `EntitySerializer` | CBOR serialization to/from `SerializedEntity` |
| `EntitySpatialIndex` | Grid-based spatial index (cell=16); O(cells_in_range) queries; auto-updated by EntityManager |
| `AIDriver` | Top-level decision-maker adapter (base class); `BrainAIDriver` wraps goal system, `PlayerInputDriver` consumes input events |
| `MobEventHooks` | Virtual interface for entity lifecycle callbacks (onSpawn/onTick/onDamage/onDeath/onInteract/onStrike) |
| `ScriptMobEventHooks` | Header-only adapter bridging `MobEventHooks` → `ScriptEntityHandler` |
| `EntityManager` | Owns all entities; tick AI/physics; manage transfers/despawns; persist; auto-configures AI presets on mob spawn |

---

## EntityTypeId / EntityTypeRegistry

```cpp
#include <finevox/core/entity_type.hpp>

// Creating IDs
EntityTypeId zombieId(StringInterner::global().intern("finevox:zombie"));

// Registry
EntityTypeRegistry& reg = EntityTypeRegistry::global();
reg.registerType("finevox:zombie", zombieDef);
const EntityTypeDef& def = reg.getType(zombieId);

// EntityTypeDef fields
def.name;         // string name
def.size;         // AABB (entity bounding box)
def.maxHealth;    // float
def.behaviorConfig; // variant per mob type
```

---

## Entity & MobEntity

```cpp
#include <finevox/core/entity.hpp>

// Base Entity
entity.position();
entity.velocity();
entity.boundingBox();  // AABB in world space
entity.id();           // EntityId
entity.isPlayerEntity(); // virtual — works for both Entity and MobEntity players

// MobEntity extensions
mob.health();
mob.setHealth(80.0f);    // clamped to maxHealth
mob.maxHealth();
mob.setMaxHealth(100.0f); // CALL THIS FIRST before setHealth!
mob.isAlive();
mob.isDead();

// Player flag (set by EntityManager::spawnPlayer)
mob.setIsPlayer(true);
mob.isPlayerEntity();  // overrides Entity::isPlayerEntity()

// AI Driver
mob.setDriver(std::make_unique<PlayerInputDriver>());
mob.driver();  // AIDriver* (may be nullptr)

// Event hooks (set by EntityManager from hooks provider)
mob.setEventHooks(hooks);
mob.eventHooks();  // MobEventHooks* (may be nullptr)

// Movement commands (consumed by AI/physics)
mob.moveTo(targetPos);
mob.lookAt(target);
mob.jump();
mob.clearMoveTarget();

// Landing detection (for fall damage scripts)
mob.preLandingVelocityY();  // Y velocity at last landing

// Senses
mob.senses().canSee(otherEntity);
mob.senses().canHear(position, volume);
```

---

## AIDriver

```cpp
#include <finevox/core/ai_driver.hpp>

// AIDriver is the top-level decision-maker adapter.
// MobEntity holds optional AIDriver; falls back to brain.tick() if nullptr.

// BrainAIDriver — wraps existing goal system (default for NPC mobs)
mob.setDriver(std::make_unique<BrainAIDriver>());

// PlayerInputDriver — consumes input events from graphics thread
mob.setDriver(std::make_unique<PlayerInputDriver>());
// Handles: player_look, player_jump, player_sprint, player_sneak

// AIDriver::subscribesTo(eventType) for event filtering
// AIDriver::onEvent(mob, event) dispatches finescript::Value event maps
```

---

## AIBrain & Goals

```cpp
#include <finevox/core/ai_brain.hpp>
#include <finevox/core/ai_goals.hpp>

// AIBrain selects highest-priority valid goal each tick
AIBrain brain;
brain.addGoal(1, std::make_unique<WanderGoal>(1));
brain.addGoal(5, std::make_unique<ChaseGoal>(5));
brain.addGoal(6, std::make_unique<AttackGoal>(6));

// Goals accept optional param structs (defaults match original hardcoded values)
WanderGoalParams wp;
wp.range = 20.0f;        // default: 10.0f
wp.startChance = 0.2f;   // default: 0.1f
brain.addGoal(1, std::make_unique<WanderGoal>(1, wp));

// Param structs: IdleGoalParams, WanderGoalParams, ChaseGoalParams,
//   AttackGoalParams, FleeGoalParams, LookAtPlayerGoalParams, PanicGoalParams

// configureAIPreset(mob, AIType) — auto-populates brain with preset goals
// Called automatically by EntityManager on spawn if brain is empty

// Each tick (called by MobEntity::tick)
brain.tick(mob, dt);

// Built-in goals (priority = higher → selected first when valid)
IdleGoal           // stand still, look around
WanderGoal         // random walk within radius
ChaseGoal          // move toward target entity
AttackGoal         // melee attack when in range
FleeGoal           // run away from threat
LookAtPlayerGoal   // face nearest player
PanicGoal          // random sprint after taking damage
```

---

## Pathfinder

```cpp
#include <finevox/core/pathfinder.hpp>

Pathfinder pathfinder(world);
auto path = pathfinder.findPath(startPos, goalPos, entity);
// Returns: std::vector<BlockCoord> waypoints (empty if no path found)
// Uses entity.maxStepHeight() for walkability
// A* heuristic: Manhattan distance with diagonal penalty

// Follow path (in MobEntity movement):
if (!path.empty()) {
    BlockCoord next = path.front();
    mob.moveTo(glm::vec3(next) + glm::vec3(0.5f, 0, 0.5f));
    if (atNextWaypoint) path.pop_front();
}
```

---

## Skeleton & Animation

```cpp
#include <finevox/core/skeleton.hpp>
#include <finevox/core/animation_controller.hpp>

// Skeleton — bone hierarchy
Skeleton skeleton;
skeleton.addBone("torso", parentId=-1, localTransform);
skeleton.addBone("head", parentId=torsoId, localTransform);
// ... add all bones

// AnimationClip
AnimationClip walk;
walk.name = "walk";
walk.duration = 0.5f;  // seconds
walk.loop = true;
walk.addKeyframe(boneId, time, transform);  // multiple keyframes per bone

// AnimationController
AnimationController controller(skeleton);
controller.addClip(walkClip);
controller.addClip(idleClip);

// Playing animations
controller.play("walk", speed=1.0f, layer=0);  // layer 0 = base
controller.play("attack", speed=1.0f, layer=1); // layer 1 overlaid

// Per-tick update
controller.tick(dt);  // advances clip time, updates bone transforms with crossfade

// Get final bone transforms for rendering
for (int i = 0; i < skeleton.boneCount(); i++) {
    glm::mat4 worldTransform = skeleton.evaluateWorldTransform(i, controller);
}
```

**Note:** `skeleton.evaluateTransform(boneId)` returns parent-relative transform. Caller must multiply up the parent chain to get world space.

---

## EntityManager

```cpp
#include <finevox/core/entity_manager.hpp>

EntityManager manager(world, physics);

// Spawn / despawn
EntityId id = manager.spawnEntity("finevox:zombie", spawnPos);
manager.despawnEntity(id);
Entity* e = manager.getEntity(id);
MobEntity* mob = manager.getMob(id);  // nullptr if not MobEntity

// Per-tick (called from game thread)
manager.tick(dt);          // AI, physics, transfers
manager.physicsPass(dt);   // fluid contact, gravity, movement resolution

// Persistence
manager.saveColumnEntities(column);
manager.loadColumnEntities(column);

// Graphics snapshots (called from game thread)
manager.publishEntitySnapshots();  // writes to GraphicsEventQueue

// Spatial queries (O(cells_in_range) via grid index)
auto& idx = manager.spatialIndex();
auto nearby = idx.queryRadius(center, radius);       // → vector<EntityId>
auto inBox = idx.queryAABB(min, max);                 // → vector<EntityId>
EntityId closest = idx.findNearest(center, radius);   // → EntityId or INVALID
```

---

## Script Integration

Entity events in finescript handlers: `:spawn`, `:tick`, `:damage`, `:death`, `:interact`, `:strike`

These fire via MobEventHooks → ScriptMobEventHooks → ScriptEntityHandler (adapter chain).
EntityManager auto-wires hooks on spawn via `MobEventHooksProvider` callback from GameScriptEngine.

Native mob functions (underscore naming, NOT dot; operate on current entity context):
```
mob_health             -- get health (float)
mob_set_health hp      -- set health
mob_max_health         -- get max health (float)
mob_position           -- [x,y,z] array
mob_velocity           -- [vx,vy,vz] array
mob_move_to x y z      -- move entity to position
mob_look_at x y z      -- face a position
mob_jump               -- jump (if on ground)
mob_damage amount [src] -- apply damage
mob_heal amount        -- heal
mob_set_animation id   -- set animation slot
mob_is_dead            -- bool
mob_is_on_ground       -- bool
mob_id                 -- entity id (int)
mob_type               -- type symbol
mob_spawn type x y z   -- spawn mob, returns EntityId
mob_find_path x y z    -- pathfind, returns bool
mob_set_speed mult     -- set speed multiplier
mob_add_goal type prio [params] -- add AI goal from name + params map
mob_clear_goals        -- clear all AI goals
mob_apply_impulse vx vy vz -- add to velocity
mob_get_data key       -- read entity DataContainer
mob_set_data key val   -- write entity DataContainer
mob_remove             -- mark for removal
mob_is_player          -- bool (is this the player entity?)
mob_fall_velocity      -- Y velocity at last landing (for fall damage)
mob_speed_multiplier   -- get current speed multiplier
mob_yaw                -- get yaw (radians)
mob_pitch              -- get pitch (radians)
mob_last_attacker      -- entity id of last attacker (or nil)
mob_time_since_damage  -- seconds since last damage
```

---

## Gotchas

- `EntityTypeId.id` is a public `uint32_t` member, NOT a function
- `EntityId` defined in `entity_state.hpp` (not `block_event.hpp`) — include the right header
- **Call `setMaxHealth()` BEFORE `setHealth()`** — setHealth clamps to maxHealth; wrong order loses max health
- Bone transforms from `skeleton.evaluateTransform()` are parent-relative; multiply up the chain for world space
- `ScriptEntityHandler` caches event closures on first call (not re-fetched each tick)
- `DataKey`/`DataValue`: namespace-level types (`finevox::DataKey`), NOT `DataContainer::DataKey`
- Entity data stored in ChunkColumn DataContainer under `"entity_data"` key (CBOR bytes)
- Native mob functions use underscore: `mob_health`, NOT `mob.health`
- SpawnManager uses region-based mob cap tracking — configure max mobs per region separately from per-type
