# 8. Physics and Collision

[Back to Index](INDEX.md) | [Previous: Level of Detail](07-lod.md)

---

## 8.1 Linear Algebra Library

**Use GLM** - FineStructureVK already depends on GLM for linear algebra. Using GLM ensures:
- Consistent math types across the codebase
- Well-tested, widely-used library
- No row/column order bridging issues

If more advanced features are needed later (eigenvalue decomposition, sparse matrices), Eigen3 could be considered, but requires careful attention to memory layout conventions.

---

## 8.2 Collision Shapes

```cpp
namespace finevox {

// Axis-aligned bounding box (implemented - uses glm::vec3)
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    bool intersects(const AABB& other) const;
    bool contains(glm::vec3 point) const;

    // Swept collision (returns time of impact, 0-1, or >1 if no collision)
    float sweepCollision(const AABB& other, glm::vec3 velocity, glm::vec3* outNormal = nullptr) const;

    AABB expanded(glm::vec3 amount) const;
    AABB translated(glm::vec3 offset) const;
};

// Collection of AABBs for complex block shapes (implemented)
class CollisionShape {
public:
    void addBox(const AABB& box);

    const std::vector<AABB>& boxes() const { return boxes_; }

    // Transform shape by block rotation
    CollisionShape transformed(const Rotation& rotation) const;

    // Precompute all 24 rotations
    static std::array<CollisionShape, 24> computeRotations(const CollisionShape& base);

    // Standard shapes
    static const CollisionShape NONE;
    static const CollisionShape FULL_BLOCK;
    static const CollisionShape HALF_SLAB_BOTTOM;
    static const CollisionShape HALF_SLAB_TOP;
    static const CollisionShape FENCE_POST;
    static const CollisionShape THIN_FLOOR;

private:
    std::vector<AABB> boxes_;
};

}  // namespace finevox
```

---

## 8.3 Collision Box vs Hit Box

Blocks and entities have two distinct bounding volumes that serve different purposes:

| Type | Purpose | Examples |
|------|---------|----------|
| **Collision Box** | Physics - prevents entities from passing through | Walking on ground, bumping into walls |
| **Hit Box** | Interaction - raycasting for selection/attack | Clicking on a block, hitting an entity |

These are often the same, but not always:

| Block/Entity | Collision | Hit Box | Behavior |
|--------------|-----------|---------|----------|
| Solid block | Full cube | Full cube | Normal solid block |
| Tall grass | None | Full cube | Walk through, but can click to break |
| Torch | None | Small box | Walk through, clickable |
| Ladder | None | Full cube | Walk through, climb when inside |
| Painting | None | Large rectangle | Walk through, can click to remove |
| Ghost entity | None | Entity bounds | Walk through, can attack |
| Pressure plate | Thin slab | Full cube | Step on triggers, easy to click |

### Raycast Modes (Implemented)

```cpp
namespace finevox {

enum class RaycastMode {
    Collision,   // Check collision boxes (for physics queries)
    Interaction, // Check hit boxes (for player clicks/attacks)
    Both         // Check either (for general queries)
};

struct RaycastResult {
    bool hit = false;
    BlockPos blockPos;        // Block that was hit
    Face face = Face::PosY;   // Face of the block that was hit
    glm::vec3 hitPoint;       // Exact hit point in world coordinates
    float distance = 0.0f;    // Distance from origin to hit point
};

}  // namespace finevox
```

---

## 8.4 Entity Position Persistence (Avoiding Wall Glitching)

### The Problem

A famous Minecraft bug: entities would glitch through walls after save/load. The root cause:

1. Entity AABBs have precise floating-point boundaries
2. Only the bottom-center point was saved to disk
3. On load, AABB was reconstructed from center point
4. Floating-point rounding could extend AABB into neighboring blocks
5. Entity would "pop" through the wall

### The Solution: Collision Margin

Enforce a **minimum margin** between entities and blocks at all times:

```cpp
constexpr float COLLISION_MARGIN = 0.001f;  // ~1mm in game units
// Much larger than floating-point epsilon (~1e-7)
// Much smaller than any visible gap

// When resolving collisions, ensure margin is maintained
float resolveAxisCollision(const AABB& entity, const AABB& block, int axis, float movement) {
    // After resolution, entity edge must be at least COLLISION_MARGIN away from block edge
    // ...
}
```

### Entity Serialization

Save the full AABB bounds (not just center), or save center with explicit half-extents:

```yaml
entity:
  position: [10.5, 65.001, 20.5]  # Note the margin above y=65 block
  half_extents: [0.3, 0.9, 0.3]   # Or save dimensions explicitly
```

---

## 8.5 Soft vs Hard Collisions

### Hard Collision (Default)

Entities cannot overlap blocks or each other. Standard collision resolution pushes entities apart.

### Soft Collision (Entity Crowding)

Some entities allow overlap, generating a repulsion force:

```cpp
struct CollisionResponse {
    enum Type {
        Hard,           // Cannot overlap, resolve immediately
        Soft,           // Overlap generates force
        None            // No collision (pass-through)
    };

    Type type = Hard;
    float repulsionStrength = 0.0f;  // For soft collisions
};

class Entity {
public:
    // How this entity responds to collisions with blocks
    virtual CollisionResponse blockCollisionResponse() const {
        return {CollisionResponse::Hard, 0.0f};
    }

    // How this entity responds to collisions with other entities
    virtual CollisionResponse entityCollisionResponse(const Entity& other) const {
        return {CollisionResponse::Hard, 0.0f};
    }
};
```

**Use cases:**
- Packed mob farms (soft entity-entity collision)
- Slime blocks (bouncy collision)
- Water/lava (velocity modification instead of hard stop)

**Even hard collisions must yield** when nothing can move out of the way (e.g., piston crushing). This prevents entities getting stuck in blocks.

---

## 8.6 Entity Physics

```cpp
namespace finevox {

class PhysicsSystem {
public:
    explicit PhysicsSystem(World& world);

    // Move entity, handling collisions
    // Returns actual movement vector after collision resolution
    glm::vec3 moveEntity(Entity& entity, glm::vec3 desiredMovement, float deltaTime);

    // Apply gravity to entity velocity
    void applyGravity(Entity& entity, float deltaTime);

    // Ground detection
    bool isOnGround(const Entity& entity) const;

    // Raycast through world
    RaycastResult raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance,
                          RaycastMode mode = RaycastMode::Interaction) const;

    // Configuration
    void setGravity(float g) { gravity_ = g; }
    float gravity() const { return gravity_; }

private:
    World& world_;
    float gravity_ = 20.0f;  // Blocks per second squared

    // Collect all collision AABBs in a region
    std::vector<AABB> collectColliders(const AABB& region) const;

    // Resolve collision along one axis (maintains COLLISION_MARGIN)
    float resolveAxisCollision(
        const AABB& entityBox,
        const std::vector<AABB>& colliders,
        int axis,
        float movement
    );

    // Step-climbing logic (allows walking up stairs)
    bool tryStepUp(Entity& entity, glm::vec3& movement, const std::vector<AABB>& colliders);
};

}  // namespace finevox
```

---

## 8.7 Step-Climbing Algorithm

Borrowed from EigenVoxel, improved:

```cpp
glm::vec3 PhysicsSystem::moveEntity(Entity& entity, glm::vec3 desiredMovement, float dt) {
    AABB entityBox = entity.getBoundingBox();
    glm::vec3 finalMovement = desiredMovement;

    // Collect nearby colliders
    AABB searchRegion = entityBox.expanded(glm::abs(desiredMovement) + glm::vec3(1.0f));
    auto colliders = collectColliders(searchRegion);

    // Try step-climbing for horizontal movement
    const float MAX_STEP_HEIGHT = 0.625f;  // Slightly over half a block

    if (entity.isOnGround() && (desiredMovement.x != 0 || desiredMovement.z != 0)) {
        // Try different step heights
        for (float stepHeight = 0.0f; stepHeight <= MAX_STEP_HEIGHT; stepHeight += 0.0625f) {
            AABB steppedBox = entityBox.translated(glm::vec3(0, stepHeight, 0));

            // Check if we can move horizontally from this height
            float moveX = resolveAxisCollision(steppedBox, colliders, 0, desiredMovement.x);
            steppedBox = steppedBox.translated(glm::vec3(moveX, 0, 0));

            float moveZ = resolveAxisCollision(steppedBox, colliders, 2, desiredMovement.z);
            steppedBox = steppedBox.translated(glm::vec3(0, 0, moveZ));

            // Check if we can step down (or stay at this height)
            float moveY = resolveAxisCollision(steppedBox, colliders, 1, -stepHeight + desiredMovement.y);

            glm::vec3 totalMove(moveX, moveY + stepHeight, moveZ);

            // Keep best result (most horizontal movement)
            float horizDist = moveX * moveX + moveZ * moveZ;
            float bestHorizDist = finalMovement.x * finalMovement.x + finalMovement.z * finalMovement.z;

            if (horizDist > bestHorizDist + 0.001f) {
                finalMovement = totalMove;
            }
        }
    } else {
        // Standard XYZ collision resolution
        finalMovement.y = resolveAxisCollision(entityBox, colliders, 1, desiredMovement.y);
        entityBox = entityBox.translated(glm::vec3(0, finalMovement.y, 0));

        finalMovement.x = resolveAxisCollision(entityBox, colliders, 0, desiredMovement.x);
        entityBox = entityBox.translated(glm::vec3(finalMovement.x, 0, 0));

        finalMovement.z = resolveAxisCollision(entityBox, colliders, 2, desiredMovement.z);
    }

    // Apply movement
    entity.setPosition(entity.position() + finalMovement);

    // Update ground state
    entity.setOnGround(finalMovement.y > desiredMovement.y && desiredMovement.y < 0);

    return finalMovement;
}
```

---

## See Also

- [19 - Block Models](19-block-models.md) - Data-driven model format for loading collision/hit shapes from files

---

[Next: Lighting System](09-lighting.md)
