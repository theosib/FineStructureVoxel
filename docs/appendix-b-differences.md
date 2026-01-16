# Appendix B: Key Differences from Prior Implementations

[Back to Index](INDEX.md)

---

| Aspect | VoxelGame2 | EigenVoxel | FineStructureVoxel (New) |
|--------|-----------|------------|--------------------------|
| Graphics API | OpenGL 3.3 | OpenGL (LWJGL) | Vulkan (FineStructureVK) |
| Block Registry | Per-chunk | Per-chunk | Global |
| Mesh Generation | Simple face culling | Simple face culling | Greedy meshing |
| Lighting | Hardcoded | Hardcoded | Propagated block/sky light |
| Concurrency | Manual locks | ConcurrentHashMap | Thread pools + queues |
| Persistence | Custom binary | None | Region files |
| View-Relative | No | Yes | Yes |
| Frustum Culling | Skeleton only | Implemented | Implemented |
| AO | No | No | Yes |
| Step-Climbing | Basic | Advanced | Advanced |

---

[Back to Index](INDEX.md)
