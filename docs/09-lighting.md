# 9. Lighting System

[Back to Index](INDEX.md) | [Previous: Physics and Collision](08-physics.md)

---

## 9.1 Goals

1. **Block light** - Light emitted from torches, lava, etc. (0-15 levels)
2. **Sky light** - Sunlight propagating from sky (0-15 levels)
3. **Ambient occlusion** - Soft shadows in corners
4. **Smooth lighting** - Interpolated light values for smooth appearance

---

## 9.2 Light Propagation

```cpp
namespace finevox {

class LightingSystem {
public:
    explicit LightingSystem(World& world);

    // Get light level at a block (combined sky + block light)
    int getLightLevel(BlockPos pos) const;
    int getSkyLight(BlockPos pos) const;
    int getBlockLight(BlockPos pos) const;

    // Update lighting after block change
    void onBlockChange(BlockPos pos);

    // Queue-based light propagation
    void processLightQueue();

    // Get ambient occlusion for a vertex
    float getAmbientOcclusion(BlockPos block, Face face, int cornerU, int cornerV) const;

private:
    World& world_;

    // Light storage (separate from block data for efficiency)
    struct ChunkLighting {
        std::array<uint8_t, 4096> skyLight;    // Packed 4-bit per block
        std::array<uint8_t, 4096> blockLight;  // Packed 4-bit per block
    };
    std::unordered_map<uint64_t, ChunkLighting> chunkLighting_;

    // BFS queues for light propagation
    std::queue<BlockPos> lightIncreaseQueue_;
    std::queue<BlockPos> lightDecreaseQueue_;

    void propagateSkyLight(ChunkPos chunkPos);
    void propagateBlockLight(BlockPos source, int level);
    void removeLightSource(BlockPos pos);
};

}  // namespace finevox
```

---

## 9.3 Ambient Occlusion

Per-vertex AO based on adjacent block occupancy:

```cpp
float LightingSystem::getAmbientOcclusion(BlockPos block, Face face, int cornerU, int cornerV) const {
    // Get positions of 3 neighbors that affect this corner
    // (side1, side2, and corner)
    BlockPos side1Pos = /* ... based on face and cornerU */;
    BlockPos side2Pos = /* ... based on face and cornerV */;
    BlockPos cornerPos = /* ... diagonal */;

    bool side1 = world_.getBlock(side1Pos).isSolid();
    bool side2 = world_.getBlock(side2Pos).isSolid();
    bool corner = world_.getBlock(cornerPos).isSolid();

    // Calculate AO level (0-3 neighbors blocking)
    int ao;
    if (side1 && side2) {
        ao = 0;  // Full occlusion
    } else {
        ao = 3 - (side1 + side2 + corner);
    }

    // Convert to 0-1 range
    static const float aoValues[] = { 0.2f, 0.45f, 0.7f, 1.0f };
    return aoValues[ao];
}
```

---

[Next: Input and Player Control](10-input.md)
