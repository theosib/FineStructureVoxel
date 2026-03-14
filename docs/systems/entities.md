# System: Entities

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/entity*.hpp`, `ai_brain.hpp`, `pathfinder.hpp`, `skeleton.hpp`, `animation_controller.hpp`
**Old docs:** [old_docs/25-entity-system.md](../../old_docs/25-entity-system.md) (1685 lines)

---

## Overview

Phase 20 complete (1570 tests: 1526 main + 44 script). Entities use `EntityTypeId` (interned), loaded from `.entity` files. `MobEntity` extends `Entity` with AI brain, senses, health, and movement commands. AI uses priority-based goal selection with built-in goals. A* pathfinding over block grid. Skeletal animation with crossfade blending. EntityRenderer on graphics thread interpolates snapshots. CBOR serialization per-column. Full script event handler support.

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
| `EntityManager` | Owns all entities; tick AI/physics; manage transfers/despawns; persist |

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
entity.bounds();  // AABB in world space
entity.id();      // EntityId

// MobEntity extensions
mob.health();
mob.setHealth(80.0f);    // clamped to maxHealth
mob.maxHealth();
mob.setMaxHealth(100.0f); // CALL THIS FIRST before setHealth!
mob.isAlive();
mob.isDead();

// Movement commands (consumed by AI/physics)
mob.moveTo(targetPos);
mob.lookAt(target);
mob.jump();
mob.stopMoving();

// Senses
mob.senses().canSee(otherEntity);
mob.senses().canHear(position, volume);
```

---

## AIBrain & Goals

```cpp
#include <finevox/core/ai_brain.hpp>

// AIBrain selects highest-priority valid goal each tick
AIBrain brain;
brain.addGoal(std::make_unique<WanderGoal>(speed=1.0f, priority=1));
brain.addGoal(std::make_unique<ChaseGoal>(target, speed=2.0f, priority=5));
brain.addGoal(std::make_unique<AttackGoal>(attackRange=2.0f, damage=5.0f, priority=10));
brain.addGoal(std::make_unique<FleeGoal>(threat, fleeDistance=16.0f, priority=8));

// Each tick (called by EntityManager)
brain.tick(mob, senses, dt);

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
```

---

## Script Integration

Entity events in finescript handlers: `:spawn`, `:tick`, `:damage`, `:death`, `:interact`, `:strike`

Native mob functions (underscore naming, NOT dot):
```
mob_health id         -- get health (float)
mob_set_health id h   -- set health
mob_position id       -- {x,y,z} map
mob_move_to id x y z  -- move entity to position
mob_spawn type x y z  -- spawn mob, returns EntityId
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
