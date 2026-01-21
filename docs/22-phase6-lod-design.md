# 22. Phase 6 LOD System - Detailed Design

[Back to Index](INDEX.md) | [Previous: Clipboard and Schematic System](21-clipboard-schematic.md)

---

## Overview

Phase 6 implements a Level of Detail (LOD) system for rendering distant terrain efficiently. The goals are:

1. **Reduce vertex/triangle count** for distant chunks
2. **Maintain visual consistency** - no jarring pops or seams
3. **Minimize memory overhead** - avoid storing many mesh versions
4. **Support large view distances** (512+ chunks)

This document details two complementary approaches that work together:
- **POP Buffers** for implicit mesh LOD (no separate storage per LOD)
- **Octree Downsampling** for block data LOD (simplified block representation)

---

## 1. LOD Levels and Distances

```cpp
enum class LODLevel {
    LOD0 = 0,   // Full detail: 1×1×1 blocks (0-32 blocks distance)
    LOD1 = 1,   // 2×2×2 grouping (32-64 blocks)
    LOD2 = 2,   // 4×4×4 grouping (64-128 blocks)
    LOD3 = 3,   // 8×8×8 grouping (128-256 blocks)
    LOD4 = 4,   // 16×16×16 grouping (256+ blocks) - entire subchunk = 1 block
};

struct LODConfig {
    std::array<float, 5> distances = {32.0f, 64.0f, 128.0f, 256.0f, 512.0f};
    float hysteresis = 8.0f;  // Buffer zone to prevent thrashing

    LODLevel getLevelForDistance(float distance) const;
};
```

**Hysteresis**: When transitioning between LOD levels, use buffer zones. If currently at LOD1 and moving closer, don't switch to LOD0 until distance < 32 - hysteresis. This prevents rapid switching at boundaries.

---

## 2. Approach A: POP Buffers (Mesh-Side LOD)

### 2.1 Concept

POP (Progressively Ordered Primitives) buffers provide **implicit LOD** without storing separate meshes. The key insight:

1. **Quantize vertices** to power-of-two grid positions
2. **Sort primitives** by the LOD level at which they collapse
3. **Render fewer primitives** for lower LOD by simply reducing index count

Reference: [0 FPS - A level of detail method for blocky voxels](https://0fps.net/2018/03/03/a-level-of-detail-method-for-blocky-voxels/)

### 2.2 Algorithm

For each quad/triangle, compute the LOD level at which it collapses (all vertices round to same point):

```cpp
// Quantize vertex to LOD level i grid
glm::ivec3 quantize(glm::vec3 v, int lodLevel) {
    int scale = 1 << lodLevel;  // 1, 2, 4, 8, 16...
    return glm::ivec3(
        static_cast<int>(std::floor(v.x / scale)) * scale,
        static_cast<int>(std::floor(v.y / scale)) * scale,
        static_cast<int>(std::floor(v.z / scale)) * scale
    );
}

// Find LOD level where quad collapses (all 4 corners same)
int computeQuadLOD(const glm::vec3 corners[4]) {
    for (int lod = 0; lod < MAX_LOD; ++lod) {
        glm::ivec3 q0 = quantize(corners[0], lod);
        glm::ivec3 q1 = quantize(corners[1], lod);
        glm::ivec3 q2 = quantize(corners[2], lod);
        glm::ivec3 q3 = quantize(corners[3], lod);

        // If all corners quantize to same point, quad collapses at this LOD
        if (q0 == q1 && q1 == q2 && q2 == q3) {
            return lod;
        }
    }
    return MAX_LOD;
}
```

### 2.3 Sorting and Rendering

During mesh generation, sort quads by their LOD value:

```cpp
struct POPMeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<uint32_t> indices;

    // lodOffsets[i] = first index for primitives surviving at LOD i
    // Render indices [0, lodOffsets[targetLOD]) for that LOD
    std::array<uint32_t, MAX_LOD + 1> lodOffsets;
};

void buildPOPMesh(const SubChunk& subchunk, POPMeshData& out) {
    // 1. Generate all quads with their LOD values
    struct QuadWithLOD {
        std::array<ChunkVertex, 4> verts;
        std::array<uint32_t, 6> localIndices;
        int lodValue;
    };
    std::vector<QuadWithLOD> quads;

    // ... generate quads as normal, compute LOD for each ...

    // 2. Sort by LOD (highest LOD first = survives longest)
    std::stable_sort(quads.begin(), quads.end(),
        [](const auto& a, const auto& b) { return a.lodValue > b.lodValue; });

    // 3. Build sorted index buffer and compute offsets
    uint32_t currentIndex = 0;
    for (int lod = MAX_LOD; lod >= 0; --lod) {
        out.lodOffsets[lod] = currentIndex;
        for (const auto& quad : quads) {
            if (quad.lodValue >= lod) {
                // This quad survives at this LOD
                // ... add to index buffer ...
            }
        }
    }
}

// Rendering: just change index count
void renderAtLOD(POPMeshData& mesh, int lodLevel, CommandBuffer& cmd) {
    uint32_t indexCount = mesh.lodOffsets[lodLevel];
    cmd.drawIndexed(indexCount, 1, 0, 0, 0);
}
```

### 2.4 Geomorphing (Smooth Transitions)

To avoid popping when switching LOD, use **geomorphing** in the vertex shader:

```glsl
uniform float lodBlend;      // 0.0 = current LOD, 1.0 = next coarser LOD
uniform int currentLOD;

vec3 quantizeVertex(vec3 pos, int lod) {
    float scale = float(1 << lod);
    return floor(pos / scale) * scale;
}

void main() {
    vec3 posLOD0 = quantizeVertex(inPosition, currentLOD);
    vec3 posLOD1 = quantizeVertex(inPosition, currentLOD + 1);
    vec3 morphedPos = mix(posLOD0, posLOD1, lodBlend);

    gl_Position = viewProjection * vec4(morphedPos, 1.0);
}
```

### 2.5 Advantages of POP Buffers

- **Single mesh storage** - no separate meshes per LOD
- **Continuous LOD** - can blend between any two levels
- **Cache-friendly** - single vertex/index buffer
- **Works with greedy meshing** - merged quads get correct LOD values

---

## 3. Approach B: Octree Downsampling (Block-Side LOD)

### 3.1 Concept

For distant terrain, we don't need full block resolution. Downsample the block data itself:

- **LOD1**: Each 2×2×2 region → 1 representative block
- **LOD2**: Each 4×4×4 region → 1 representative block
- **LOD3**: Each 8×8×8 region → 1 representative block

This reduces the input to mesh generation, dramatically cutting vertex counts.

### 3.2 Block Selection Strategy

When merging 8 blocks (2×2×2) into 1, choose the representative:

```cpp
enum class LODBlockSelection {
    MostCommon,     // Mode: most frequent block type wins
    MostVisible,    // Prioritize opaque over transparent over air
    Weighted,       // Consider visibility + frequency
};

BlockTypeId selectRepresentative(
    const std::array<BlockTypeId, 8>& blocks,
    LODBlockSelection strategy
) {
    switch (strategy) {
        case LODBlockSelection::MostVisible: {
            // Priority: opaque > transparent > air
            BlockTypeId best = BlockTypeId::air();
            int bestScore = 0;

            for (BlockTypeId id : blocks) {
                int score = 0;
                if (id != BlockTypeId::air()) {
                    score = isOpaque(id) ? 2 : 1;
                }
                if (score > bestScore) {
                    bestScore = score;
                    best = id;
                }
            }
            return best;
        }

        case LODBlockSelection::MostCommon: {
            // Simple frequency count
            std::unordered_map<uint32_t, int> counts;
            for (BlockTypeId id : blocks) {
                counts[id.id]++;
            }
            // ... return most frequent ...
        }

        // ... other strategies ...
    }
}
```

### 3.3 LOD SubChunk Structure

Store downsampled block data for LOD levels:

```cpp
class LODSubChunk {
public:
    // LOD1: 8×8×8 blocks (2× downsampled)
    // LOD2: 4×4×4 blocks (4× downsampled)
    // LOD3: 2×2×2 blocks (8× downsampled)
    // LOD4: 1×1×1 block  (entire subchunk)

    BlockTypeId getBlock(int x, int y, int z, LODLevel level) const;
    void rebuild(const SubChunk& source, LODLevel level);

private:
    // Compact storage for each LOD level
    std::array<BlockTypeId, 512> lod1_;  // 8×8×8
    std::array<BlockTypeId, 64> lod2_;   // 4×4×4
    std::array<BlockTypeId, 8> lod3_;    // 2×2×2
    BlockTypeId lod4_;                    // 1×1×1

    std::array<bool, 4> dirty_ = {true, true, true, true};
};
```

### 3.4 Mesh Generation for LOD Blocks

Mesh generation at LOD levels uses scaled block positions:

```cpp
MeshData buildLODMesh(
    const LODSubChunk& lodChunk,
    LODLevel level,
    ChunkPos pos,
    const NeighborProvider& neighbors,
    const BlockTextureProvider& textures
) {
    int scale = 1 << static_cast<int>(level);  // 2, 4, 8, 16
    int size = 16 / scale;  // 8, 4, 2, 1

    MeshBuilder builder;
    builder.setBlockScale(static_cast<float>(scale));  // Scale vertex positions

    for (int y = 0; y < size; ++y) {
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                BlockTypeId block = lodChunk.getBlock(x, y, z, level);
                if (block == BlockTypeId::air()) continue;

                // Check neighbors (also at LOD resolution)
                // ... face culling ...

                // Add faces with scaled positions
                // Position = (x, y, z) * scale
            }
        }
    }

    return builder.finish();
}
```

### 3.5 Simplifications at Distance

At LOD levels, apply visual simplifications:

```cpp
struct LODSimplifications {
    bool cubesOnly = false;         // All blocks become cubes (no stairs, slabs)
    bool disableTransparency = false;  // Glass/water become opaque
    bool skipVegetation = false;    // Don't render flowers, grass, etc.
    bool skipSmallBlocks = false;   // Skip fences, torches, etc.
    bool simplifyColors = false;    // Use average color instead of texture
};

LODSimplifications getSimplifications(LODLevel level) {
    switch (level) {
        case LODLevel::LOD0: return {};  // Full detail
        case LODLevel::LOD1: return {.cubesOnly = true};
        case LODLevel::LOD2: return {.cubesOnly = true, .skipVegetation = true};
        case LODLevel::LOD3: return {.cubesOnly = true, .skipVegetation = true,
                                      .disableTransparency = true};
        case LODLevel::LOD4: return {.cubesOnly = true, .skipVegetation = true,
                                      .disableTransparency = true, .simplifyColors = true};
    }
}
```

---

## 4. Boundary Stitching

### 4.1 The Problem

When adjacent chunks are at different LOD levels, there can be visible seams or gaps at boundaries.

### 4.2 Skirt-Based Solution

Separate the mesh into **main body** and **skirt** (boundary faces):

```cpp
struct LODChunkMesh {
    MeshData mainMesh;   // Interior faces (stable)
    MeshData skirtMesh;  // Boundary faces (may need regeneration)

    LODLevel level;
    std::array<LODLevel, 6> neighborLevels;  // LOD of each neighbor
};
```

The skirt adapts to neighbor LOD levels:
- If neighbor is same LOD: normal boundary faces
- If neighbor is coarser: extend faces to cover gaps
- If neighbor is finer: let the finer side handle stitching

### 4.3 Conservative Approach (Recommended Initially)

For initial implementation, use a simpler approach:

1. **Always render boundary faces** at the finer LOD level
2. **Slight overlap** at boundaries (1 block of overlap)
3. **Z-fighting prevention** via polygon offset or depth bias

This is not perfect but avoids complex stitching logic.

---

## 5. Memory and Update Strategy

### 5.1 Lazy LOD Generation

Don't precompute all LOD levels. Generate on demand:

```cpp
class LODManager {
public:
    // Request LOD mesh for chunk at given level
    // Returns cached mesh or triggers async generation
    std::shared_ptr<LODChunkMesh> requestLOD(ChunkPos pos, LODLevel level);

    // Process pending LOD generations (time-budgeted)
    void processPendingWork(std::chrono::milliseconds budget);

private:
    // Cache of LOD meshes
    std::unordered_map<uint64_t, std::shared_ptr<LODChunkMesh>> cache_;

    // Pending generation queue (prioritized by distance)
    std::priority_queue<LODRequest> pendingQueue_;

    // Worker pool for LOD generation
    MeshWorkerPool& workers_;
};
```

### 5.2 Update Delays

Distant LOD levels don't need immediate updates:

| LOD Level | Update Delay | Rationale |
|-----------|--------------|-----------|
| LOD0      | Immediate    | Close chunks need instant response |
| LOD1      | 500ms        | Visible but not critical |
| LOD2      | 2s           | Distant, changes barely visible |
| LOD3      | 5s           | Very distant, batch changes |
| LOD4      | 10s          | Extreme distance, minimal updates |

### 5.3 GPU Memory Budget

Limit total GPU memory for LOD meshes:

```cpp
class LODMemoryManager {
    size_t maxMemoryBytes_ = 256 * 1024 * 1024;  // 256 MB budget
    size_t currentUsage_ = 0;

    void evictIfNeeded(size_t requiredBytes) {
        while (currentUsage_ + requiredBytes > maxMemoryBytes_) {
            // Evict least recently used LOD mesh
            evictLRU();
        }
    }
};
```

---

## 6. Integration with Existing Systems

### 6.1 WorldRenderer Changes

```cpp
class WorldRenderer {
public:
    // Existing
    void render(CommandBuffer& cmd, const Camera& camera);

    // New for LOD
    void setLODConfig(const LODConfig& config);
    LODLevel getLODLevelForChunk(ChunkPos pos) const;

private:
    LODManager lodManager_;
    LODConfig lodConfig_;

    void renderChunk(ChunkPos pos, CommandBuffer& cmd) {
        float distance = computeDistance(pos, cameraPos_);
        LODLevel level = lodConfig_.getLevelForDistance(distance);

        if (level == LODLevel::LOD0) {
            // Use existing SubChunkView
            renderFullDetail(pos, cmd);
        } else {
            // Use LOD mesh
            auto lodMesh = lodManager_.requestLOD(pos, level);
            if (lodMesh) {
                renderLODMesh(*lodMesh, cmd);
            }
        }
    }
};
```

### 6.2 MeshBuilder Integration

For POP buffer support, extend MeshBuilder:

```cpp
class MeshBuilder {
public:
    // Existing
    MeshData buildSubChunkMesh(...);

    // New: build with POP buffer LOD info
    POPMeshData buildSubChunkMeshPOP(...);

    // New: build at reduced resolution
    MeshData buildLODMesh(const LODSubChunk& lodChunk, LODLevel level, ...);
};
```

---

## 7. Implementation Plan

### Phase 6.1: Basic Distance-Based LOD
- [ ] LODLevel enum and LODConfig
- [ ] Distance calculation with hysteresis
- [ ] LODManager skeleton
- [ ] Render LOD0 vs "distant placeholder" (solid color cube)

### Phase 6.2: Octree Downsampling
- [ ] LODSubChunk with downsampled block storage
- [ ] Block selection strategies (MostVisible, MostCommon)
- [ ] Mesh generation for LOD blocks
- [ ] LOD simplifications (cubes only, skip vegetation)

### Phase 6.3: POP Buffer Support
- [ ] Quad LOD value computation
- [ ] Sorted index buffer generation
- [ ] lodOffsets array for variable index count
- [ ] Render path for POP meshes

### Phase 6.4: Geomorphing
- [ ] Vertex shader modifications for morphing
- [ ] LOD blend factor calculation
- [ ] Smooth transitions between levels

### Phase 6.5: Boundary Handling
- [ ] Skirt mesh separation (if needed)
- [ ] Conservative overlap approach
- [ ] Seam-free rendering verification

### Phase 6.6: Memory and Performance
- [ ] GPU memory budgeting
- [ ] LRU eviction for LOD cache
- [ ] Update delay tuning
- [ ] Performance profiling and optimization

---

## 8. Debug Features

### 8.1 LOD Bias / Force LOD

For testing and debugging, allow shifting the LOD calculation or forcing a specific level:

```cpp
class LODConfig {
public:
    // ...existing config...

    // Debug: shift all LOD distances by this factor
    // lodBias = 0: normal behavior
    // lodBias = 1: everything renders at LOD1 (as if 2x farther)
    // lodBias = 2: everything renders at LOD2 (as if 4x farther)
    // lodBias = -1: everything renders at LOD0 (full detail always)
    int lodBias = 0;

    // Debug: force specific LOD level (-1 = use distance-based)
    int forceLOD = -1;

    LODLevel getLevelForDistance(float distance) const {
        if (forceLOD >= 0 && forceLOD <= static_cast<int>(LODLevel::LOD4)) {
            return static_cast<LODLevel>(forceLOD);
        }

        // Apply bias by shifting effective distance
        float effectiveDistance = distance;
        if (lodBias > 0) {
            effectiveDistance *= static_cast<float>(1 << lodBias);
        } else if (lodBias < 0) {
            effectiveDistance /= static_cast<float>(1 << (-lodBias));
        }

        // Normal distance-based selection with hysteresis...
        for (int i = 0; i < 5; ++i) {
            if (effectiveDistance < distances[i]) {
                return static_cast<LODLevel>(i);
            }
        }
        return LODLevel::LOD4;
    }
};
```

### 8.2 Demo Controls

Add keyboard controls to the render demo:

| Key | Action |
|-----|--------|
| `L` | Cycle through LOD levels (force LOD 0 → 1 → 2 → 3 → 4 → auto) |
| `[` | Decrease LOD bias (show finer detail at distance) |
| `]` | Increase LOD bias (show coarser detail up close) |
| `P` | Print current LOD stats (chunks per LOD level, vertex counts) |

```cpp
// In render_demo.cpp key handler:
if (key == GLFW_KEY_L && action == finevk::Action::Press) {
    static int forceLOD = -1;
    forceLOD = (forceLOD + 2) % 7 - 1;  // -1, 0, 1, 2, 3, 4, -1, ...
    worldRenderer.setForceLOD(forceLOD);
    if (forceLOD < 0) {
        std::cout << "LOD: Auto (distance-based)\n";
    } else {
        std::cout << "LOD: Forced to level " << forceLOD << "\n";
    }
}

if (key == GLFW_KEY_LEFT_BRACKET && action == finevk::Action::Press) {
    int bias = worldRenderer.lodBias() - 1;
    worldRenderer.setLODBias(bias);
    std::cout << "LOD bias: " << bias << "\n";
}

if (key == GLFW_KEY_RIGHT_BRACKET && action == finevk::Action::Press) {
    int bias = worldRenderer.lodBias() + 1;
    worldRenderer.setLODBias(bias);
    std::cout << "LOD bias: " << bias << "\n";
}
```

### 8.3 Visual LOD Indicators

Optional debug visualization modes:

```cpp
enum class LODDebugMode {
    None,           // Normal rendering
    ColorByLOD,     // Tint chunks by LOD level (red=0, orange=1, yellow=2, green=3, blue=4)
    WireframeByLOD, // Wireframe for non-LOD0 chunks
    ShowBoundaries, // Highlight LOD transition boundaries
};
```

In the fragment shader:

```glsl
#ifdef DEBUG_LOD_COLORS
uniform int debugLODLevel;

void main() {
    // ... normal rendering ...

    // Tint by LOD level
    vec3 lodColors[5] = vec3[](
        vec3(1.0, 0.2, 0.2),  // LOD0: red
        vec3(1.0, 0.6, 0.2),  // LOD1: orange
        vec3(1.0, 1.0, 0.2),  // LOD2: yellow
        vec3(0.2, 1.0, 0.2),  // LOD3: green
        vec3(0.2, 0.6, 1.0)   // LOD4: blue
    );

    finalColor = mix(finalColor, lodColors[debugLODLevel], 0.3);
}
#endif
```

---

## 9. Testing Strategy

### Unit Tests
- [ ] LODLevel distance calculations with hysteresis
- [ ] Block selection strategies (mode, most visible)
- [ ] LODSubChunk downsampling correctness
- [ ] POP buffer LOD value computation
- [ ] Index buffer sorting verification

### Visual Tests
- [ ] No visible seams between LOD levels
- [ ] Smooth geomorphing transitions
- [ ] Correct block representation at distance
- [ ] No Z-fighting at boundaries

### Performance Tests
- [ ] Vertex count reduction at each LOD level
- [ ] Memory usage stays within budget
- [ ] Frame time impact of LOD switching
- [ ] Large view distance (512+ chunks) stability

---

## 9. References

- [0 FPS - A level of detail method for blocky voxels](https://0fps.net/2018/03/03/a-level-of-detail-method-for-blocky-voxels/) - POP buffers
- [Parker Lawrence - Voxel Level of Detail](https://legalian.github.io/voxel/2018/03/12/voxel-level-of-detail.html) - Skirt-based boundaries
- [Transvoxel Algorithm](https://transvoxel.org/) - Smooth terrain stitching (reference only, not for blocky voxels)
- [Distant Horizons Mod](https://www.curseforge.com/minecraft/mc-mods/distant-horizons) - Minecraft LOD implementation

---

[Next: TBD]
