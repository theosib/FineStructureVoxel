# System: Physics & Collision

**Library:** `finevox` core (`finevox::`)
**Headers:** `include/finevox/core/physics.hpp`, `aabb.hpp`
**Old docs:** [old_docs/08-physics.md](../../old_docs/08-physics.md)

---

## Overview

AABB-based collision detection with step-climbing. Separates **collision shapes** (physics — what you can walk through) from **hit shapes** (interaction — what you can click on). `PhysicsSystem` handles entity movement, gravity, ground detection, and collision resolution via swept AABB. Entity step heights are per-entity-configurable.

---

## Key Types

| Type | Description |
|------|-------------|
| `AABB` | Axis-aligned bounding box (glm::vec3 min/max) |
| `CollisionShape` | Collection of AABBs for a block face; supports all 24 rotations |
| `RaycastMode` | `Collision` (physics), `Interaction` (hit boxes), `Both` |
| `RaycastResult` | `{bool hit, BlockCoord blockPos, Face face, glm::vec3 hitPoint, float distance}` |
| `CollisionResponse` | `Hard` (cannot overlap), `Soft` (repulsion force), `None` (pass-through) |
| `PhysicsSystem` | Owns gravity, collision resolution; requires World reference |
| `PhysicsBody` | Virtual interface for entities; provides `maxStepHeight()` |

---

## AABB

```cpp
#include <finevox/core/aabb.hpp>

AABB box(glm::vec3 min, glm::vec3 max);
AABB box = AABB::centered(glm::vec3 center, glm::vec3 size);

bool hit = box.intersects(other);
bool inside = box.contains(point);
AABB expanded = box.expanded(margin);
AABB translated = box.translated(offset);

// Swept collision — returns time of impact [0,1]; >1 = no hit during movement
float toi = box.sweepCollision(other, velocity, outNormal);
```

---

## CollisionShape

```cpp
#include <finevox/core/collision_shape.hpp>

// Standard shapes (defined as constants)
CollisionShape::FULL_BLOCK         // standard 1×1×1 cube
CollisionShape::HALF_SLAB_BOTTOM   // bottom half
CollisionShape::HALF_SLAB_TOP      // top half
CollisionShape::FENCE_POST         // thin vertical column
CollisionShape::THIN_FLOOR         // thin horizontal layer
CollisionShape::NONE               // no collision (air, torches, etc.)

// Rotation support
CollisionShape rotated = shape.transformed(rotation);

// Precompute all 24 rotations at once
auto rotations = CollisionShape::computeRotations(baseShape);
// Access: rotations[rotation]
```

---

## PhysicsSystem

```cpp
#include <finevox/core/physics.hpp>

PhysicsSystem physics(world);
physics.setGravity(20.0f);  // default ~20 blocks/s²

// Entity movement (resolves collision)
glm::vec3 actual = physics.moveEntity(entity, desiredMovement, dt);

// Gravity (modifies entity.velocity.y)
physics.applyGravity(entity, dt);

// Ground detection
bool onGround = physics.isOnGround(entity);

// Raycasting (DDA algorithm)
RaycastResult result = physics.raycast(origin, direction, maxDistance, RaycastMode::Both);
if (result.hit) {
    BlockCoord pos = result.blockPos;
    Face hitFace = result.face;
}
```

---

## PhysicsBody Interface

```cpp
class MyEntity : public PhysicsBody {
public:
    AABB bounds() const override;               // entity AABB in world space
    glm::vec3& velocity() override;             // mutable velocity
    float maxStepHeight() const override;        // how high entity can step up (default 0.6)
    CollisionResponse collisionResponse() const override;  // Hard, Soft, or None
};
```

---

## Collision vs Hit Shapes

Block types have TWO separate shapes:
- **Collision shape**: used by `PhysicsSystem` for entity movement
- **Hit shape**: used by raycast in `RaycastMode::Interaction` for click detection

Example differences:
- Torch: `CollisionShape::NONE` (walk through) + full 1×1×1 hit shape (easy to click)
- Pressure plate: thin slab collision + full 1×1×1 hit shape
- Fence: post + panel collision + full hit shape

```cpp
const BlockType& type = BlockRegistry::global().getType(id);
const CollisionShape& cshape = type.collisionShape(rotation);  // for physics
const CollisionShape& hshape = type.hitShape(rotation);         // for raycasting
```

---

## Step-Climbing Algorithm

Tries step heights from 0.0 → `maxStepHeight()` in 0.0625-block increments. For each step:
1. Attempt horizontal movement at given step height
2. Keep result with most horizontal progress, stopping at the first height that goes all the way

Applies only when entity is on ground AND has horizontal movement intent.

**Max step height** per entity type:
- Player: 0.6 blocks (standard 1-block step)
- Mobs with short legs: may be less
- Horses etc.: may be higher

---

## Solid slopes

With `solid_slopes: true` in `game.conf`, bodies stand on smoothed ground (rendering.md, "Smooth
ground") as it's drawn, not on its cubes: a hole a block deep is a dip you walk through, not a
hole you drop into. `GroundSurface` (`ground_surface.hpp`) does it, set on a World with
`World::setGroundSurface`. `GameRuntime` sets one on the server's world, and `GameClient` on its own
for the Steward, whose physics runs there.

- **The shape.** Each smoothed top collides as a 4x4 grid of columns (`GROUND_COLUMNS`), each as
  high as the surface over its middle, to the nearest 1/16 (`GROUND_QUANTUM`). Where it dips, the
  block's cell is cut down; where it rises, the open cell above gets the rest. Columns stop a block
  either way from the face: where steps climb diagonally a corner is drawn up to 0.075 past that.
- **What still keeps to the grid.** Undersides (ceilings) are drawing only, and picking
  (`RaycastMode::Interaction`) hits the cubes, so placing and breaking work on whole blocks. Paths
  are still searched over cells (navigation.hpp); a body whose feet are inside a block where the
  ground dips starts its search from the cell above, as from a slab.
- **What follows it.** Anything through `createBlockShapeProvider`: movement, `isClear`,
  `hasFooting`, collision raycasts (`line_of_sight`). Projectiles hit the surface (`solidAt`).
- **Walking down.** Going down the slope is many small steps. `PhysicsSystem::setGroundSnap` puts a
  body that was on the ground back on it after each move, up to `GROUND_SNAP` (half a block) below,
  rather than have it fall each one. Both runtimes turn it on with solid slopes; it's 0 otherwise.
- **Stepping up.** Where ground meets something built its edge stays level, but the corners beside
  it can sit up to half a block lower, so a block up onto it is more than a block from there.
  Path-following bodies step `GROUND_STEP_EXTRA` (half a block) higher with solid slopes. Routes
  still only climb a cell at a time.
- **Kept, and forgotten.** A face's columns are worked out when something first collides with it
  and kept per column. A face reads one cell across, three below and four above
  (`groundPatch`), so a change in any of those forgets it: World's block writes, a cell getting wet
  or drying, a column coming or going, and on a client, each subchunk it's sent. Changes may come
  from any thread; queries come from the one that runs physics. Working out a face costs about 100
  cell lookups. `GroundSurface.KeepsUpWithChanges` checks that every face stays as drawn through
  3000 random changes.

---

## Fluid Physics (see systems/fluids.md)

`EntityManager::physicsPass()` handles fluid interaction:
1. `computeFluidContact()` — scans entity bounding box for fluid
2. `applyBuoyancy()` — counteracts gravity proportional to submersion
3. `applyFluidDrag()` — velocity *= (1 - viscosity * submersion * dt)
4. `applyFlowForce()` — adds directional push from flowing fluid

---

## Gotchas

- **Collision margin**: 0.001f minimum gap between entities and blocks (prevents float-precision glitches after save/load)
- **Axis resolution order**: Y first, then X, then Z — allows stepping around corners correctly
- **Ground state**: Updated after movement; `isOnGround()` returns true when downward movement was blocked
- **Soft collision**: Generates repulsion force; used for mob crowding or bounce effects; both entities call `entityCollisionResponse()`
- **Swept collision**: `sweepCollision()` returns time [0,1]; caller must handle sub-tick collision by splitting movement at time of impact
- **Pathfinder step height**: `Pathfinder::findPath()` queries `entity.maxStepHeight()` for walkable node determination
