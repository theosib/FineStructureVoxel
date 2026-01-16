# 2. Prior Art Analysis

[Back to Index](INDEX.md) | [Previous: Executive Summary](01-executive-summary.md)

---

## 2.1 VoxelGame2 (C++/OpenGL)

**Architecture Summary:**
- 16x16x16 chunks with uint16_t block IDs
- Per-chunk block type registry (name->ID mapping)
- 24-rotation system for block orientation
- Event-driven block updates (place/break/update/repaint/tick)
- DataContainer system for arbitrary per-block metadata
- Dual-threaded mesh generation with double-buffering

**Strengths to Preserve:**
1. **Ephemeral Block descriptors** - Cheap temporary objects pointing into chunk storage
2. **Per-chunk type registry** - Memory-efficient, allows 65K block types per chunk
3. **Event system** - Clean separation of placement, update, and rendering events
4. **DataContainer** - Flexible NBT-like metadata for blocks that need it
5. **Rotation encoding** - Compact uint8_t for 24 orientations

**Weaknesses to Fix:**
1. **Trivial world generation** - Only flat dirt layer
2. **Lock contention** - Chunk generation holds world lock
3. **No LOD system** - All chunks render at full detail
4. **Hardcoded lighting** - Fixed light position in shader
5. **No frustum culling** - Infrastructure exists but disabled
6. **Endianness assumptions** - Binary format not portable
7. **Thread lifecycle** - Worker threads not properly joined on shutdown

**Key Bugs Identified:**
- Race condition in block update queue processing
- Missing null checks in mesh loading path
- Chunk unload race condition with render threads
- **Rotation face culling bug** - When a block has non-zero rotation, the wrong faces are culled from the combined geometry. The face visibility check compares against neighbor blocks, but doesn't account for how the rotation transforms which face is actually facing each direction. This causes visual artifacts where faces that should be visible are culled, and vice versa.

---

## 2.2 EigenVoxel (Scala + Java/LWJGL)

**Architecture Summary:**
- Same 16x16x16 chunk system, Scala implementation with Java graphics infrastructure
- View-relative rendering (positions relative to camera block)
- ConcurrentHashMap for thread-safe chunk storage
- RenderAgent pattern for integrating non-render code into frame timing

**Java Infrastructure (27 files across 7 packages):**

The Java portion provides a mature graphics and input framework:

| Package | Components |
|---------|------------|
| `GraphicsEngine` | Mesh, Face (Triangle/Quad/NoFace), MeshRenderer, Shader, Texture, StringRenderer, FontAtlas |
| `Adaptors` | Window, GLWindow, RenderAgent, InputReceiver, Disposable interfaces |
| `Events` | InputEvent hierarchy (Key, Button, Scroll, Motion, Timer events) |
| `Utils` | PriorityList, FileLocator, IOUtil, dimension/cursor helpers |

**Notable Java Design Patterns:**
1. **Null Object Pattern** - `Face.NoFace` singleton prevents null checks in face arrays
2. **Polymorphic Faces** - Triangle and Quad implement Face interface, Quad auto-converts to 6 triangle vertices
3. **Lazy GPU Loading** - Shaders and textures compile/upload on first bind
4. **Priority-Based Ordering** - PriorityList for flexible render agent and input receiver composition
5. **Event Consumption** - Input events return true to consume, preventing further delivery
6. **Direct Memory Management** - LWJGL MemoryUtil for performance-critical buffers

**Mesh Configuration Format:**
The Java `Mesh.loadMesh()` parses block configuration files defining:
- Face geometry (vertices, texture coordinates)
- Per-face texture references
- Collision box definitions
- Solid face flags for culling

**Improvements Over VoxelGame2:**
1. **View-relative rendering** - Solves float precision at large coordinates
2. **Better concurrency** - ConcurrentHashMap vs manual locking
3. **Frustum culling** - Actually implemented and working
4. **Step-climbing physics** - More realistic collision with stairs/ledges
5. **Mature graphics abstraction** - Well-designed Java rendering pipeline
6. **Font/text rendering** - Complete TrueType atlas with kerning support

**Issues Found:**
- Bug in BlockPos.fromVector() - uses `x` instead of `z` for Z coordinate
- Perlin noise array initialization bug
- Entity frustum check always returns true (disabled)
- No chunk unloading - memory grows unbounded

---

## 2.3 Design Principles from Prior Art

| Principle | Source | Rationale |
|-----------|--------|-----------|
| 16x16x16 chunks | Both | Good balance of granularity and overhead |
| Per-chunk block registry | VG2 | Memory efficient, 65K types per chunk |
| View-relative rendering | EV | Essential for large world coordinates |
| Ephemeral block descriptors | VG2 | Reduces allocation, cheap queries |
| Double-buffered meshes | Both | Smooth concurrent updates |
| Event-driven updates | VG2 | Clean separation of concerns |

---

[Next: Architecture Overview](03-architecture.md)
