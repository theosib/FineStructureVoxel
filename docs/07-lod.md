# 7. Level of Detail (LOD)

[Back to Index](INDEX.md) | [Previous: Rendering System](06-rendering.md)

---

## 7.1 Overview

Distant chunks don't need full detail - we can render simplified meshes to improve performance. The LOD system provides:

1. **Multiple detail levels** per subchunk (full, half, quarter resolution)
2. **Lazy LOD computation** - only update LOD meshes when necessary, with significant delay tolerance
3. **Aggressive batching** - combine multiple distant chunks into single draw calls
4. **Memory caching** - keep LOD meshes in memory, regenerate only on block changes

---

## 7.2 LOD Levels

```cpp
namespace finevox {

enum class LODLevel {
    Full = 0,      // 16x16x16, all faces, full detail (0-32 blocks)
    Half = 1,      // 8x8x8 effective, simplified mesh (32-64 blocks)
    Quarter = 2,   // 4x4x4 effective, very simplified (64-128 blocks)
    Impostor = 3   // Single quad per visible side (128+ blocks)
};

struct LODConfig {
    float fullDetailDistance = 32.0f;
    float halfDetailDistance = 64.0f;
    float quarterDetailDistance = 128.0f;
    float maxRenderDistance = 256.0f;

    LODLevel getLevelForDistance(float distance) const;
};

class LODMesh {
public:
    LODLevel level;
    finevk::MeshRef mesh;
    bool dirty = true;
    std::chrono::steady_clock::time_point lastModified;
};

}  // namespace finevox
```

---

## 7.3 Lazy LOD Updates

LOD meshes are updated lazily with significant delay tolerance. This minimizes computational cost when many blocks are changing:

```cpp
namespace finevox {

class LODManager {
public:
    explicit LODManager(World& world);

    // Mark a subchunk's LOD as needing rebuild
    void markDirty(ChunkPos pos, LODLevel level);

    // Process pending LOD updates (call each frame with time budget)
    // Returns number of LODs updated
    int processPendingUpdates(std::chrono::milliseconds budget);

    // Get LOD mesh for rendering (may return stale mesh if update pending)
    LODMesh* getLODMesh(ChunkPos pos, LODLevel level);

    // Configuration
    void setUpdateDelay(LODLevel level, std::chrono::milliseconds delay);

private:
    World& world_;

    // Per-subchunk LOD storage
    struct SubchunkLODs {
        std::array<LODMesh, 4> lods;  // One per LODLevel
    };
    std::unordered_map<uint64_t, SubchunkLODs> lodCache_;

    // Update queue with timestamps (don't update until delay passed)
    struct PendingUpdate {
        ChunkPos pos;
        LODLevel level;
        std::chrono::steady_clock::time_point requestTime;

        bool operator<(const PendingUpdate& other) const {
            return requestTime > other.requestTime;  // Oldest first
        }
    };
    std::priority_queue<PendingUpdate> updateQueue_;

    // Delay before LOD update (allows batching of changes)
    std::array<std::chrono::milliseconds, 4> updateDelays_ = {
        std::chrono::milliseconds(0),     // Full: immediate (handled by ChunkView)
        std::chrono::milliseconds(500),   // Half: 0.5 second delay
        std::chrono::milliseconds(2000),  // Quarter: 2 second delay
        std::chrono::milliseconds(5000)   // Impostor: 5 second delay
    };
};

}  // namespace finevox
```

---

## 7.4 LOD Mesh Generation

```cpp
namespace finevox {

class LODMeshBuilder {
public:
    // Build simplified mesh for a subchunk at given LOD level
    static finevk::MeshRef buildLODMesh(
        Chunk& chunk,
        World& world,
        LODLevel level,
        finevk::LogicalDevice& device
    );

private:
    // Half LOD: Sample every 2nd block, merge faces
    static void buildHalfLOD(Chunk& chunk, std::vector<ChunkVertex>& vertices);

    // Quarter LOD: Sample every 4th block, more aggressive merging
    static void buildQuarterLOD(Chunk& chunk, std::vector<ChunkVertex>& vertices);

    // Impostor: Just visible outer faces
    static void buildImpostor(Chunk& chunk, std::vector<ChunkVertex>& vertices);
};

}  // namespace finevox
```

---

## 7.5 Batched Distant Rendering

For very distant chunks, combine multiple into single draw calls:

```cpp
namespace finevox {

class DistantChunkBatcher {
public:
    // Collect distant chunks into batched meshes
    // Groups chunks by region (e.g., 4x4 chunk areas)
    void collectForBatching(
        const std::vector<std::shared_ptr<ChunkColumn>>& columns,
        BlockPos viewCenter,
        float minDistance
    );

    // Render all batched distant geometry
    void render(finevk::CommandBuffer& cmd);

private:
    // Batched meshes for distant regions
    struct RegionBatch {
        glm::ivec2 regionPos;  // Region coordinates (chunk coords / 4)
        finevk::MeshRef batchedMesh;
        bool dirty = true;
    };
    std::unordered_map<uint64_t, RegionBatch> regionBatches_;
};

}  // namespace finevox
```

---

[Next: Physics and Collision](08-physics.md)
