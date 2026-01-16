# Appendix A: File Structure

[Back to Index](INDEX.md)

---

```
FineStructureVoxel/
├── CMakeLists.txt
├── DESIGN.md                    # Brief overview with links to docs/
├── README.md
├── docs/                        # Detailed documentation (this directory)
│   ├── INDEX.md
│   ├── 01-executive-summary.md
│   ├── ...
│   └── appendix-b-differences.md
├── assets/
│   ├── textures/
│   │   ├── blocks/
│   │   └── entities/
│   └── shaders/
│       ├── block_vertex.glsl
│       ├── block_fragment.glsl
│       └── ...
├── include/
│   └── voxel/
│       ├── core/
│       │   ├── Position.hpp
│       │   ├── Block.hpp
│       │   ├── BlockType.hpp
│       │   ├── Chunk.hpp
│       │   └── World.hpp
│       ├── render/
│       │   ├── ChunkView.hpp
│       │   ├── WorldRenderer.hpp
│       │   ├── TextureAtlas.hpp
│       │   └── MeshBuilder.hpp
│       ├── physics/
│       │   ├── AABB.hpp
│       │   ├── CollisionShape.hpp
│       │   ├── PhysicsSystem.hpp
│       │   └── Entity.hpp
│       ├── terrain/
│       │   ├── Noise.hpp
│       │   ├── TerrainGenerator.hpp
│       │   └── Biome.hpp
│       ├── persist/
│       │   ├── ChunkSerializer.hpp
│       │   └── RegionFile.hpp
│       └── game/
│           ├── InputManager.hpp
│           ├── PlayerController.hpp
│           └── VoxelGame.hpp
├── src/
│   └── (mirrors include structure)
├── tests/
│   └── ...
└── third_party/
    └── (FineStructureVK as submodule or external)
```

---

[Back to Index](INDEX.md)
