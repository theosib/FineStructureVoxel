# 8. Physics and Collision

[Back to Index](INDEX.md) | [Previous: Level of Detail](07-lod.md)

---

## 8.1 Collision Shapes

```cpp
namespace finevox {

// Axis-aligned bounding box
struct AABB {
    glm::vec3 min;
    glm::vec3 max;

    bool intersects(const AABB& other) const;
    bool contains(glm::vec3 point) const;

    // Swept collision (returns time of impact, 0-1, or >1 if no collision)
    float sweepCollision(const AABB& other, glm::vec3 velocity) const;

    // Get collision normal for swept collision
    glm::vec3 getCollisionNormal(const AABB& other, glm::vec3 velocity) const;

    AABB expanded(glm::vec3 amount) const;
    AABB translated(glm::vec3 offset) const;
};

// Collection of AABBs for complex block shapes
class CollisionShape {
public:
    void addBox(const AABB& box);

    const std::vector<AABB>& boxes() const { return boxes_; }

    // Transform shape by block rotation
    CollisionShape transformed(uint8_t rotation) const;

    // Precompute all 24 rotations
    static std::array<CollisionShape, 24> computeRotations(const CollisionShape& base);

    // Empty shape (no collision)
    static const CollisionShape NONE;

private:
    std::vector<AABB> boxes_;
};

// Common shapes
namespace Shapes {
    extern const CollisionShape FULL_BLOCK;
    extern const CollisionShape HALF_SLAB;
    extern const CollisionShape STAIRS;
    extern const CollisionShape FENCE;
}

}  // namespace finevox
```

---

## 8.2 Collision Box vs Hit Box

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

```cpp
namespace finevox {

// BlockType provides both shapes
class BlockType {
public:
    // Physics collision (can be NONE for pass-through blocks)
    virtual const CollisionShape* getCollisionShape() const = 0;

    // Raycast/interaction hit box (can differ from collision)
    // Defaults to collision shape if not overridden
    virtual const CollisionShape* getHitBox() const {
        return getCollisionShape();
    }

    // Some blocks (like tall grass) have no collision but are still "solid"
    // for purposes like preventing block placement inside them
    virtual bool preventsPlacement() const { return hasCollision(); }
};

// Entity provides both as well
class Entity {
public:
    // Physics bounding box
    virtual AABB getCollisionBox() const = 0;

    // Interaction/combat hit box (may be larger or differently shaped)
    virtual AABB getHitBox() const {
        return getCollisionBox();  // Default: same as collision
    }

    // Some entities (like spectators) have no collision but can still be seen
    virtual bool hasCollision() const { return true; }
};

}  // namespace finevox
```

### Raycast Modes

The raycast system needs to know which boxes to check:

```cpp
namespace finevox {

enum class RaycastMode {
    Collision,   // Check collision boxes (for physics queries)
    Interaction, // Check hit boxes (for player clicks/attacks)
    Both         // Check either (for general queries)
};

class PhysicsSystem {
public:
    // Raycast with mode selection
    RaycastResult raycast(
        glm::vec3 origin,
        glm::vec3 direction,
        float maxDistance,
        RaycastMode mode = RaycastMode::Interaction  // Default for player interaction
    ) const;
};

}  // namespace finevox
```

---

## 8.3 Entity Physics

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
    World::RaycastResult raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance) const;

    // Configuration
    void setGravity(float g) { gravity_ = g; }
    float gravity() const { return gravity_; }

private:
    World& world_;
    float gravity_ = 20.0f;  // Blocks per second squared

    // Collect all collision AABBs in a region
    std::vector<AABB> collectColliders(const AABB& region) const;

    // Resolve collision along one axis
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

## 8.4 Step-Climbing Algorithm

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

[Next: Lighting System](09-lighting.md)
