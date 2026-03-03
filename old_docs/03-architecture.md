# 3. Architecture Overview

[Back to Index](INDEX.md) | [Previous: Prior Art](02-prior-art.md)

---

## 3.1 High-Level Components

```
+---------------------------------------------------------------------+
|                        Application Layer                             |
|  (Game logic, UI, mod system, game-specific features)               |
+---------------------------------------------------------------------+
|                        Engine Layer                                  |
|  +----------+ +----------+ +----------+ +----------+                |
|  |  World   | | Renderer | | Physics  | |  Input   |                |
|  | Manager  | |  System  | |  System  | |  System  |                |
|  +----+-----+ +----+-----+ +----+-----+ +----+-----+                |
|       |            |            |            |                       |
|  +----+------------+------------+------------+-----+                 |
|  |              Core Systems                        |                 |
|  |  (Threading, Memory, Serialization, Events)      |                 |
|  +--------------------------------------------------+                 |
+---------------------------------------------------------------------+
|                     FineStructureVK                                  |
|  (Window, GameLoop, RenderAgent, Vulkan abstraction)                |
+---------------------------------------------------------------------+
```

## 3.2 Module Breakdown

| Module | Responsibility |
|--------|----------------|
| `voxel::world` | World, Chunk, Block, BlockType management |
| `voxel::render` | Mesh generation, chunk rendering, materials |
| `voxel::physics` | Collision detection, entity movement, raycasting |
| `voxel::terrain` | Procedural generation, biomes, structures |
| `voxel::entity` | Player, NPCs, items, entity component system |
| `voxel::persist` | Save/load, serialization, region files |
| `voxel::ui` | HUD, menus, inventory (uses FineStructureVK) |

## 3.2.1 Core Services

| Service | Responsibility |
|---------|----------------|
| `ResourceLocator` | Logical path → physical path resolution for all resources |
| `ConfigManager` | Global engine settings (singleton, uses ResourceLocator) |
| `WorldConfig` | Per-world settings with global fallback |
| `IOManager` | Async save/load with background threads |

### ResourceLocator Scope Hierarchy

```
engine/     → Engine defaults (shipped with library)
game/       → Game assets (root provided by game layer)
user/       → User settings (~/.config/finevox or platform equivalent)
world/<n>/  → Per-world data (registered dynamically)
  dim/<d>/  → Dimensions within world
```

See [11-persistence.md](11-persistence.md) Section 11.6 for full ResourceLocator documentation.

## 3.3 Dependency Graph

```
terrain ----> world <---- persist
                |
                v
             render <---- entity <---- physics
                |            |
                v            v
           FineStructureVK (GameLoop, RenderAgent, etc.)
```

---

[Next: Core Data Structures](04-core-data-structures.md)
