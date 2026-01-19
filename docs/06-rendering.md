# 6. Rendering System

[Back to Index](INDEX.md) | [Previous: World Management](05-world-management.md)

---

## 6.1 Architecture

> **Implementation Note (Current State):**
> The current implementation has a simpler flat architecture:
> ```
> WorldRenderer (renders all subchunks)
>     +-- SubChunkView (one per loaded subchunk, wraps finevk::RawMesh)
> ```
> The full architecture below is the target design for future phases.

```
WorldRenderer
    |
    +-- ChunkRenderer (one per loaded chunk)
    |       +-- Opaque MeshRenderer (one per texture)
    |       +-- Translucent MeshRenderer (one per block, sorted)
    |
    +-- EntityRenderer
    |
    +-- SkyRenderer
    |
    +-- UIRenderer
```

---

## 6.2 Mesh Generation

**Greedy Meshing** combines adjacent coplanar faces with the same texture to reduce vertex count:

```cpp
namespace finevox {

class MeshBuilder {
public:
    // Generate mesh for a chunk
    // Returns meshes grouped by texture ID
    std::unordered_map<TextureId, Mesh> buildChunkMesh(Chunk& chunk, World& world);

private:
    // Greedy mesh one face (axis-aligned plane)
    void greedyMeshFace(
        Chunk& chunk,
        Face face,
        std::unordered_map<TextureId, std::vector<Vertex>>& output
    );

    // Check if face should be rendered (not occluded by solid neighbor)
    // For displaced blocks, face elision only occurs when:
    // 1. Both blocks have full solid faces
    // 2. Both blocks have IDENTICAL displacement values
    // Blocks with different displacements always render all visible faces.
    bool shouldRenderFace(Block& block, Face face, World& world);

    // Check displacement match for face elision
    bool canElideFaceWithNeighbor(Block& block, Block& neighbor);

    // Ambient occlusion calculation
    float calculateAO(Block& block, Face face, int cornerX, int cornerZ);
};

// Vertex format for chunk meshes
struct ChunkVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 texCoord;
    float ao;  // Ambient occlusion (0-1)

    static constexpr auto getAttributeFlags() {
        return finevk::VertexAttribute::Position
             | finevk::VertexAttribute::Normal
             | finevk::VertexAttribute::TexCoord;
        // AO handled as additional custom attribute
    }
};

}  // namespace finevox
```

### 6.2.1 Off-Grid Block Displacement

Blocks can optionally be placed with a sub-block displacement, rendering them offset from their grid position. This is useful for:
- Decorative elements that don't align to the grid
- Structural variety without additional block types
- Natural formations (rocks, debris, etc.)

**Displacement constraints:**
- Each axis (X, Y, Z) is clamped to [-1.0, 1.0] (one block unit in each direction)
- Displacement is stored sparsely (only for blocks with non-zero displacement)

**Face elision rules for displaced blocks:**
```cpp
bool MeshBuilder::canElideFaceWithNeighbor(Block& block, Block& neighbor) {
    // Both blocks must be solid and have full faces
    if (!block.type()->isSolid() || !neighbor.type()->isSolid()) {
        return false;
    }

    // Critical: Only elide faces if displacements are IDENTICAL
    // This prevents visual artifacts from misaligned blocks
    BlockDisplacement d1 = block.getDisplacement();
    BlockDisplacement d2 = neighbor.getDisplacement();

    return d1 == d2;  // Both zero, or exactly matching non-zero values
}
```

This approach ensures:
- Aligned displaced blocks (e.g., a row of offset blocks) mesh correctly as hollow shells
- Misaligned blocks render all faces, preventing holes or z-fighting
- Zero-displacement blocks (normal grid blocks) only elide faces with other zero-displacement blocks

---

## 6.3 View-Relative Rendering

To prevent float precision issues at large coordinates, all rendering is done relative to the camera's block position:

```cpp
namespace finevox {

class WorldRenderer {
public:
    void render(finevk::CommandBuffer& cmd, const Camera& camera, World& world);

private:
    // Current view center (camera's block position)
    BlockPos viewCenter_;

    // Convert world position to view-relative position
    glm::vec3 toViewRelative(BlockPos pos) const {
        return glm::vec3(
            pos.x - viewCenter_.x,
            pos.y - viewCenter_.y,
            pos.z - viewCenter_.z
        );
    }

    glm::vec3 toViewRelative(glm::vec3 worldPos) const {
        return worldPos - glm::vec3(viewCenter_.x, viewCenter_.y, viewCenter_.z);
    }
};

}  // namespace finevox
```

---

## 6.4 Chunk View and Double Buffering

> **Implementation Note (Current State - Phase 4):**
> The current `SubChunkView` class is simpler:
> - Single buffer (no double-buffering)
> - Wraps `finevk::RawMesh` for GPU storage
> - Synchronous mesh updates on main thread
>
> Double-buffering and async mesh updates are **Phase 5** features.

```cpp
namespace finevox {

class ChunkView {
public:
    explicit ChunkView(Chunk& chunk);

    // Update mesh from chunk data (called from worker thread)
    void rebuildMesh(World& world);

    // Swap front/back buffers (called when rebuild complete)
    void swapBuffers();

    // Render (called from main thread)
    void render(
        finevk::CommandBuffer& cmd,
        const glm::mat4& viewProj,
        BlockPos viewCenter,
        finevk::Material& material
    );

    // Check if chunk is in frustum
    bool isInFrustum(const Frustum& frustum, BlockPos viewCenter) const;

    // State
    bool isReady() const { return frontBuffer_.ready; }
    bool isRebuilding() const { return rebuilding_.load(); }

private:
    Chunk& chunk_;

    struct MeshBuffer {
        std::unordered_map<TextureId, finevk::MeshRef> opaqueMeshes;
        std::vector<std::pair<float, finevk::MeshRef>> translucentMeshes;  // Sorted by distance
        bool ready = false;
    };

    MeshBuffer frontBuffer_;  // Used for rendering
    MeshBuffer backBuffer_;   // Used for rebuilding
    std::atomic<bool> rebuilding_{false};

    // Bounding box for frustum culling
    glm::vec3 boundsMin_, boundsMax_;
};

}  // namespace finevox
```

---

## 6.5 Mesh Update Pipeline

> **Implementation Note (Phase 5 - Current):**
> The pipeline has been simplified to a pull-based version model:
> - SubChunks have atomic `blockVersion_` counter (incremented on changes)
> - Render loop iterates visible chunks near-to-far, providing natural priority
> - FIFO queue with deduplication replaces priority queue
> - Workers write results directly; no separate GPU upload queue needed

Block changes trigger a streamlined pipeline:

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 1: Block Change Detection                                         │
│                                                                         │
│   SubChunk.setBlock() ──► blockVersion_.fetch_add(1)                    │
│                                                                         │
│   Cheap: O(1) atomic increment                                          │
│   No push notifications - version comparison happens at read time       │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 2: Render Loop Scheduling                                          │
│                                                                         │
│   for each visible subchunk (near-to-far order):                        │
│       if (cachedVersion != subchunk.blockVersion()):                    │
│           meshRebuildQueue.push(pos);  // Deduplicates automatically    │
│                                                                         │
│   FIFO order from near-to-far iteration provides natural priority       │
│   No explicit priority calculation needed                               │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 3: Mesh Worker Thread(s)                                          │
│                                                                         │
│   while (running) {                                                     │
│       ChunkPos pos = queue.popWait();  // Blocking with condition_var   │
│       SubChunk* chunk = world.getSubChunk(pos);                         │
│       if (!chunk) continue;                                             │
│                                                                         │
│       // Generate mesh (reads block data without lock - see note below) │
│       MeshData data = buildMesh(*chunk, world);                         │
│                                                                         │
│       // Push result with version for staleness check                   │
│       resultQueue.push({pos, data, chunk->blockVersion()});             │
│   }                                                                     │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 4: Result Processing (Main Thread)                                 │
│                                                                         │
│   while (auto result = workerPool.popResult()) {                        │
│       if (result.version < currentVersion(result.pos)):                 │
│           continue;  // Stale mesh, skip upload                         │
│       uploadToGPU(result);                                              │
│   }                                                                     │
│                                                                         │
│   Staleness check prevents uploading outdated meshes                    │
└─────────────────────────────────────────────────────────────────────────┘
```

### Concurrency and Consistency Notes

**Reading chunk data during mesh generation:**

Mesh workers read SubChunk data without holding locks. This is safe because:

1. **Palette indices are stable** - New block types are appended to the palette;
   existing indices never change meaning (as long as compaction is offline-only)

2. **Block type lookup is atomic** - Reading `blocks_[index]` and `palette_[paletteIdx]`
   are both simple array lookups

3. **Inconsistent reads are tolerable** - If a block changes mid-mesh-build:
   - The mesh may show old or new state for that block
   - The version check ensures a rebuild gets scheduled
   - Visual glitches are transient (typically one frame)

4. **No crashes possible** - Array bounds are fixed, palette entries are valid

**For blocks with extra data affecting appearance (stairs, sloped dirt, etc.):**

- Custom meshes stored as `std::shared_ptr` can be swapped atomically
- Use `std::atomic<std::shared_ptr<Mesh>>` (C++20) or `atomic_store`/`atomic_load`
- Version bump ensures rebuild when mesh-affecting data changes

**Batch block operations:**

For bulk `setBlock` calls (e.g., structure generation), increment version once at
the end of the batch rather than per-block. This avoids redundant mesh rebuilds.

### Key Design Benefits

- **No push notifications** - Simpler code, no callback management
- **Natural priority** - Render loop order provides near-to-far scheduling
- **Deduplication** - Queue coalesces multiple changes to same chunk
- **Version-based staleness** - Stale meshes detected and skipped
- **Lock-free reads** - Workers don't block game logic

---

## 6.6 Texture Atlas

Block textures are packed into an atlas to minimize texture binding:

```cpp
namespace finevox {

class TextureAtlas {
public:
    explicit TextureAtlas(finevk::LogicalDevice& device);

    // Add texture to atlas, returns texture ID and UV coordinates
    struct TextureRegion {
        TextureId id;
        glm::vec2 uvMin;
        glm::vec2 uvMax;
    };
    TextureRegion addTexture(const std::string& path);
    TextureRegion addTexture(const uint8_t* data, int width, int height);

    // Finalize atlas (uploads to GPU)
    void finalize();

    // Get GPU texture for binding
    finevk::TextureRef getTexture() const { return atlasTexture_; }

    // Get UV coordinates for a texture ID
    glm::vec4 getUVBounds(TextureId id) const;  // (minU, minV, maxU, maxV)

private:
    finevk::LogicalDevice& device_;
    finevk::TextureRef atlasTexture_;
    std::vector<glm::vec4> uvBounds_;

    // Packing algorithm
    int atlasWidth_ = 0, atlasHeight_ = 0;
    std::vector<uint8_t> atlasData_;
};

}  // namespace finevox
```

---

## 6.6 Shader Design

> **Implementation Note:**
> The actual shaders in `shaders/chunk.vert` and `shaders/chunk.frag` may differ slightly
> from these examples. The key concept is view-relative rendering where `chunkOffset`
> is computed on CPU with double precision, keeping GPU math in the small-number range.

**Vertex Shader (chunk.vert) - View-Relative Rendering:**
```glsl
#version 450

layout(location = 0) in vec3 inPosition;   // Local position within subchunk (0-16)
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inAO;

// Camera uniform - view matrix is centered at origin (rotation only)
layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    vec3 cameraPos;
    float nearPlane;
    float farPlane;
} camera;

// Per-chunk push constants
layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;  // View-relative position: (chunkWorldOrigin - cameraPos)
    float padding;
} chunk;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragAO;

void main() {
    // View-relative rendering for precision at large coordinates:
    // chunkOffset = (chunkWorldOrigin - cameraPos) computed on CPU with doubles
    // viewRelativePos = chunkOffset + localPos = position relative to camera
    vec3 viewRelativePos = chunk.chunkOffset + inPosition;

    // Transform using view-relative position
    // View matrix treats camera as at origin (rotation only, no translation)
    gl_Position = camera.projection * camera.view * vec4(viewRelativePos, 1.0);

    // Reconstruct approximate world pos for lighting if needed
    fragWorldPos = viewRelativePos + camera.cameraPos;
    fragNormal = inNormal;
    fragTexCoord = inTexCoord;
    fragAO = inAO;
}
```

**Fragment Shader (block_fragment.glsl):**
```glsl
#version 450

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in float fragAO;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 viewProj;
    vec3 viewCenter;
    vec3 lightDir;
    vec3 lightColor;
    vec3 ambientColor;
    float time;
};

layout(set = 1, binding = 0) uniform sampler2D texAtlas;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texAtlas, fragTexCoord);

    // Discard transparent pixels
    if (texColor.a < 0.1) {
        discard;
    }

    // Lighting
    float NdotL = max(dot(fragNormal, lightDir), 0.0);
    vec3 diffuse = lightColor * NdotL;

    // Apply ambient occlusion
    vec3 ambient = ambientColor * fragAO;

    vec3 finalColor = texColor.rgb * (ambient + diffuse);
    outColor = vec4(finalColor, texColor.a);
}
```

---

[Next: Level of Detail (LOD)](07-lod.md)
