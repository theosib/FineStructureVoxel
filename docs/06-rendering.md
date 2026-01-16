# 6. Rendering System

[Back to Index](INDEX.md) | [Previous: World Management](05-world-management.md)

---

## 6.1 Architecture

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

Block changes trigger a multi-stage pipeline to update GPU-renderable data:

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 1: Block Change Detection                                         │
│                                                                         │
│   SubChunk.setBlock() ──► meshDirty_ = true                             │
│                      └──► notify adjacent subchunks (boundary faces)    │
│                                                                         │
│   Cheap: O(1) flag set + O(1) neighbor notification                     │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 2: Priority Queue                                                  │
│                                                                         │
│   MeshRebuildQueue (CoalescingQueue with priority)                      │
│   - Key: ChunkPos                                                        │
│   - Priority: f(distance to camera, in-frustum, time since dirty)       │
│   - Coalescing: multiple changes to same subchunk = one rebuild         │
│                                                                         │
│   Priority calculation (lower = higher priority):                       │
│     in_frustum ? 0 : 1000                                               │
│     + distance_to_camera * 10                                           │
│     + time_since_dirty * -1  // older = higher priority                 │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 3: Mesh Worker Thread(s)                                          │
│                                                                         │
│   while (running) {                                                     │
│       ChunkPos pos = queue.popHighestPriority();                        │
│       SubChunk* chunk = world.getSubChunk(pos);                         │
│       if (!chunk || !chunk->meshDirty_) continue;                       │
│                                                                         │
│       // Generate mesh data (CPU-side vertex/index arrays)              │
│       MeshData data = buildMesh(*chunk, world);                         │
│                                                                         │
│       // Queue for GPU upload                                           │
│       gpuUploadQueue.push({pos, std::move(data)});                      │
│   }                                                                     │
│                                                                         │
│   Note: Multiple worker threads can process in parallel                 │
│   Output: CPU-side vertex/index buffers ready for GPU upload            │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Stage 4: GPU Upload Queue (Main Thread)                                  │
│                                                                         │
│   GPUUploadQueue (FIFO, coalescing by ChunkPos)                         │
│   - FIFO is fine: priority already handled in Stage 2                   │
│   - Coalescing: if same chunk queued multiple times, keep latest        │
│                                                                         │
│   Main thread processes N uploads per frame:                            │
│     while (uploads_this_frame < MAX_UPLOADS && !queue.empty()) {        │
│         auto [pos, data] = queue.pop();                                 │
│         SubChunkView* view = getView(pos);                              │
│         view->uploadToGPU(data);  // Buffer copy to GPU                 │
│         ++uploads_this_frame;                                           │
│     }                                                                   │
│                                                                         │
│   Throttling prevents frame stalls from too many uploads                │
└─────────────────────────────────────────────────────────────────────────┘
```

### Key Design Points

**Stage 1 - Minimal overhead on block changes:**
- Setting `meshDirty_` is O(1)
- Adjacent subchunk notification only touches immediate neighbors (up to 6)
- Does NOT compute mesh or touch GPU

**Stage 2 - Priority queue with coalescing:**
- Same subchunk changed 100 times = 1 rebuild
- Camera-near and in-frustum chunks rebuild first
- Stale dirty chunks eventually get processed even if distant

**Stage 3 - Parallel mesh generation:**
- Pure CPU work, can use thread pool
- Generates vertex/index arrays (not GPU buffers)
- Could batch-generate for chunks in same column

**Stage 4 - GPU uploads on main thread:**
- Vulkan buffer uploads typically require main thread (or careful synchronization)
- Throttled per frame to avoid stalls
- FIFO is fine since priority already sorted in Stage 2
- Coalescing prevents redundant uploads if mesh worker is faster than GPU upload

### Alternative: Combined Stage 3+4

If mesh building and GPU data preparation can share work:

```cpp
// In worker thread:
MeshData data = buildMesh(*chunk, world);

// Prepare staging buffer (CPU-visible GPU memory) in worker thread
StagingBuffer staging = prepareStagingBuffer(data);

// Queue only the copy command for main thread
copyQueue.push({pos, staging, targetBuffer});

// Main thread just issues copy commands:
vkCmdCopyBuffer(cmd, staging.buffer, target.buffer, ...);
```

This reduces main-thread work to just issuing copy commands.

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

**Vertex Shader (block_vertex.glsl):**
```glsl
#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in float inAO;

layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 viewProj;
    vec3 viewCenter;  // For view-relative to world conversion
    vec3 lightDir;
    vec3 lightColor;
    vec3 ambientColor;
    float time;
};

layout(push_constant) uniform PushConstants {
    vec3 chunkOffset;  // View-relative chunk position
};

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;
layout(location = 3) out float fragAO;

void main() {
    vec3 worldPos = inPosition + chunkOffset;
    gl_Position = viewProj * vec4(worldPos, 1.0);

    fragPosition = worldPos;
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
