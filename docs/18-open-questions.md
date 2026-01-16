# 18. Open Questions and Design Decisions

[Back to Index](INDEX.md) | [Previous: Implementation Phases](17-implementation-phases.md)

---

## Resolved Decisions

### 1. Block Registry: Per-SubChunk with Global String Names

**Decision:** Per-subchunk palette with global string interning.

- Globally, blocks have string names (e.g., `"mymod:custom_ore"`) that map to their models and implementations
- These are interned to `uint32_t` for cheap comparison
- Each subchunk has a local palette mapping global IDs to compact local indices
- This makes block types per-world **unlimited** while keeping subchunk storage **compressible**

See [04-core-data-structures.md](04-core-data-structures.md) sections 4.3-4.4.

### 2. Mesh Generation Threading: Worker Pool with Priority

**Decision:** Thread pool with distance/visibility-based prioritization.

When a block changes:
- Computing new meshes is **prioritized by distance to player and visibility**
- This keeps latency really low for changes performed by the user
- Changes by NPCs, pistons, etc. that are out of player's reach can be **lazier and more consolidated** - the user doesn't know when these occurred anyway

### 3. View Distance and LOD Architecture

**Decision:** Lazy visual representation with cached meshes.

Based on user position, a subchunk will be asked for the visual representation at the appropriate LOD scale:
1. SubChunk tells a worker thread to generate the representation
2. The latest version is **cached**
3. If a modification has been made, the request triggers an update to the worker thread
4. Meanwhile, an **old (stale) version is still rendered** - user sees previous state until worker completes

This applies to all visual representations - there's always a cached version being rendered while updates happen in the background.

### 4. GPU Memory Management

**Decision:** Buffer zones with lazy unloading.

- When a lower LOD is requested, higher LOD representation gets either:
  - **Immediately unloaded** from GPU (if cheap to do so), or
  - **Queued for lazy removal**
- There's a **buffer zone** where two LODs coexist if user position is near the LOD boundary
- If a block change occurs in a far subchunk, **only the LOD the user can see gets updated** on GPU
- If user moves and needs different LOD, they see **stale version briefly** while worker computes and GPU updates

### 5. Mod/Plugin Architecture

**Decision:** Shared object loading for everything, including core game elements.

- Full **MOD API** exposed via C++ shared object (`.so`/`.dll`) loading
- Not just for mods - **core game elements** are also loaded this way
- APIs for blocks, entities, and everything else are exposed to loaded modules
- This makes the engine truly game-agnostic; the "game" is just a set of loaded modules

```cpp
namespace finevox {

class ModuleLoader {
public:
    // Load a module (.so/.dll)
    bool loadModule(const std::filesystem::path& path);

    // Modules register their content via these registries
    BlockRegistry& blocks();
    EntityRegistry& entities();
    ItemRegistry& items();
    RecipeRegistry& recipes();
    CommandRegistry& commands();

    // Module lifecycle
    void initializeAll();   // Call after all modules loaded
    void shutdownAll();     // Call before exit
};

// Modules implement this interface
class GameModule {
public:
    virtual ~GameModule() = default;

    virtual std::string_view name() const = 0;
    virtual std::string_view version() const = 0;

    // Called to register content
    virtual void registerContent(ModuleLoader& loader) = 0;

    // Called after all modules registered
    virtual void initialize() {}

    // Called on shutdown
    virtual void shutdown() {}
};

// Module entry point (exported symbol)
extern "C" GameModule* createModule();

}  // namespace finevox
```

---

## Multiplayer Architecture (Needs Further Design)

This requires serious thought. Key principles:

### Client-Side Responsibilities

| System | Client Role | Server Role |
|--------|-------------|-------------|
| **Rendering** | Full ownership | None |
| **Player movement** | Client-side, async updates to server | Validates, can correct |
| **Block placement/removal** | Immediate local effect | Authoritative; responds with "what really happened" |
| **Entity physics (vehicles)** | Some entities client-side | Authoritative for most entities |
| **GUI dialogs** | Entirely client-side rendering/interaction | Sends specification, receives results |
| **Inventory/crafting UI** | Client renders, handles interactions | Authoritative for actual state changes |

### Client World Model

The client should have a **simplified world model**:
- Placing/removing a block has **immediate visual effect**
- But doesn't necessarily get all functionality instantly
- Server responds with confirmation of what really happened
- Almost always matches - but handles conflicts (two players affect same block)

### Synchronization Flow

```
Player places block
    ↓
Client: Immediately updates local world + mesh
Client: Sends "place block" to server
    ↓
Server: Validates, applies (or rejects/modifies)
Server: Sends "block result" to client
    ↓
Client: If different, update local world
         (momentary visual glitch is acceptable)
```

### Dynamic Updates

Some things change dynamically:
- Item removed from chest while player has it open
- Block broken by another player
- Entity state changes

Server pushes these changes; client updates its simplified model.

### Open Questions for Multiplayer

1. **Authority split** - Which entity types are client-authoritative vs server-authoritative?
2. **Prediction** - How much client-side prediction for player actions?
3. **Chunk ownership** - Does client "own" nearby chunks or just cache server state?
4. **Scripting execution** - Does client run any game logic scripts or just rendering?
5. **Bandwidth** - How to minimize block update traffic for distant changes?

---

## Remaining Open Questions

### Lighting Storage

- Option A: Store in SubChunk alongside blocks
- Option B: Separate LightingSystem with own storage
- **Leaning toward:** Separate system for cleaner architecture, but needs design

### Entity Component System

- Option A: Simple inheritance-based Entity class
- Option B: Full ECS (entt or custom)
- **Leaning toward:** Start simple; ECS adds complexity but may be needed for multiplayer/mods

### Greedy Meshing Granularity

- Mesh entire subchunk at once vs per-face-direction?
- Trade-off: Memory vs rebuild time
- **Leaning toward:** Whole subchunk, but profile to verify

### Target Games

- Minecraft-style survival? Roblox-style building? 7DTD-style destruction?
- Different games may need different physics/destruction models
- **Note:** With module-based architecture, the engine is game-agnostic - games define their own rules

---

[Back to Index](INDEX.md)
