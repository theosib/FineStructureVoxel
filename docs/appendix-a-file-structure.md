# Appendix A: File Structure

[Back to Index](INDEX.md)

---

```
FineStructureVoxel/
├── CMakeLists.txt
├── README.md
├── docs/                            # Design documentation
│   ├── INDEX.md                     # Master index
│   ├── AI-NOTES.md                  # AI session context
│   ├── SOURCE-DOC-MAPPING.md        # Source ↔ doc cross-reference
│   ├── REVIEW-TODO.md               # Design review tracking
│   ├── 01-executive-summary.md      # Goals, non-goals
│   ├── 02-prior-art.md              # Lessons from prior implementations
│   ├── 03-architecture.md           # Architecture overview
│   ├── 04-core-data-structures.md   # BlockPos, SubChunk, Palette, etc.
│   ├── 05-world-management.md       # ChunkColumn, World, ColumnManager
│   ├── 06-rendering.md              # Mesh generation, view-relative rendering
│   ├── 07-lod.md                    # Level of detail
│   ├── 08-physics.md                # AABB, collision, raycasting
│   ├── 09-lighting.md               # Block light, sky light, AO
│   ├── 10-input.md                  # Input system (design only)
│   ├── 11-persistence.md            # CBOR, region files
│   ├── 12-scripting.md              # Command language (design only)
│   ├── 13-batch-operations.md       # BatchBuilder, coalescing
│   ├── 14-threading.md              # Thread model
│   ├── 15-finestructurevk-integration.md
│   ├── 16-finestructurevk-critique.md
│   ├── 17-implementation-phases.md  # Phase checklist (authoritative)
│   ├── 18-open-questions.md         # Settled and open decisions
│   ├── 19-block-models.md           # Block model spec format
│   ├── 20-large-world-coordinates.md
│   ├── 20-finestructurevk-recommendations.md  # (shares number 20)
│   ├── 21-clipboard-schematic.md    # Design only
│   ├── 22-phase6-lod-design.md
│   ├── 23-distance-and-loading.md
│   ├── 24-event-system.md
│   ├── 25-entity-system.md
│   ├── 26-network-protocol.md
│   ├── finegui-design.md            # GUI toolkit design
│   ├── PLAN-mesh-architecture-improvements.md
│   ├── appendix-a-file-structure.md # This file
│   └── appendix-b-differences.md
├── include/finevox/                 # Public headers (flat layout)
│   ├── position.hpp                 # BlockPos, ChunkPos, ColumnPos, Face
│   ├── subchunk.hpp                 # 16³ block storage with palette
│   ├── palette.hpp                  # SubChunkPalette
│   ├── string_interner.hpp          # String→ID interning
│   ├── chunk_column.hpp             # Full-height column of subchunks
│   ├── world.hpp                    # World container
│   ├── column_manager.hpp           # Column lifecycle state machine
│   ├── block_type.hpp               # BlockType, BlockTypeId, BlockRegistry
│   ├── block_model.hpp              # FaceGeometry, BlockGeometry, BlockModel
│   ├── block_model_loader.hpp       # .model/.geom/.collision parser
│   ├── rotation.hpp                 # 24 cube rotations
│   ├── mesh.hpp                     # MeshBuilder, MeshData, ChunkVertex
│   ├── mesh_worker_pool.hpp         # Parallel mesh generation
│   ├── mesh_rebuild_queue.hpp       # Mesh rebuild scheduling
│   ├── world_renderer.hpp           # Render coordination (VK-dependent)
│   ├── subchunk_view.hpp            # GPU mesh handle
│   ├── lod.hpp                      # LOD generation and selection
│   ├── physics.hpp                  # AABB, CollisionShape, PhysicsSystem
│   ├── light_data.hpp               # Per-block light storage
│   ├── light_engine.hpp             # BFS light propagation
│   ├── block_event.hpp              # BlockEvent types
│   ├── block_handler.hpp            # BlockContext, BlockHandler
│   ├── event_queue.hpp              # UpdateScheduler, EventOutbox
│   ├── entity.hpp                   # Entity base
│   ├── entity_manager.hpp           # Entity lifecycle
│   ├── entity_registry.hpp          # Entity type registration
│   ├── item_registry.hpp            # Item registration
│   ├── graphics_event_queue.hpp     # Game↔graphics messaging
│   ├── module.hpp                   # ModuleLoader, GameModule
│   ├── config.hpp                   # ConfigManager, WorldConfig
│   ├── config_file.hpp              # Config file parsing
│   ├── config_parser.hpp            # Key-value parser (used by model loader)
│   ├── distances.hpp                # Distance zone calculations
│   ├── resource_locator.hpp         # Asset path resolution
│   ├── texture_manager.hpp          # Texture atlas management
│   ├── block_atlas.hpp              # Block UV coordinate mapping
│   ├── serialization.hpp            # SubChunk/Column CBOR serialization
│   ├── cbor.hpp                     # CBOR encoder/decoder
│   ├── region_file.hpp              # 32×32 region file I/O
│   ├── io_manager.hpp               # Async save/load
│   ├── data_container.hpp           # Key-value extra data storage
│   ├── batch_builder.hpp            # Block operation batching
│   ├── block_data_helpers.hpp       # BlockTypeId storage helpers
│   ├── lru_cache.hpp                # Generic LRU cache
│   ├── queue.hpp                    # Base queue interface
│   ├── simple_queue.hpp             # Simple FIFO queue
│   ├── coalescing_queue.hpp         # Key-deduplicating queue
│   ├── keyed_queue.hpp              # Key-associated data queue
│   ├── alarm_queue.hpp              # Timer-based queue
│   ├── blocking_queue.hpp           # Legacy thread-safe queue
│   └── wake_signal.hpp              # Thread wakeup primitive
├── src/                             # Implementation files (mirrors headers)
│   ├── position.cpp
│   ├── subchunk.cpp
│   ├── palette.cpp
│   ├── string_interner.cpp
│   ├── chunk_column.cpp
│   ├── world.cpp
│   ├── column_manager.cpp
│   ├── block_type.cpp
│   ├── block_model.cpp
│   ├── block_model_loader.cpp
│   ├── rotation.cpp
│   ├── mesh.cpp
│   ├── mesh_worker_pool.cpp
│   ├── world_renderer.cpp           # VK-dependent
│   ├── subchunk_view.cpp            # VK-dependent
│   ├── lod.cpp
│   ├── physics.cpp
│   ├── light_data.cpp
│   ├── light_engine.cpp
│   ├── block_event.cpp
│   ├── block_handler.cpp
│   ├── event_queue.cpp
│   ├── entity.cpp
│   ├── entity_manager.cpp
│   ├── entity_registry.cpp
│   ├── item_registry.cpp
│   ├── graphics_event_queue.cpp
│   ├── module.cpp
│   ├── config.cpp
│   ├── config_file.cpp
│   ├── config_parser.cpp
│   ├── resource_locator.cpp
│   ├── texture_manager.cpp          # VK-dependent
│   ├── block_atlas.cpp              # VK-dependent
│   ├── serialization.cpp
│   ├── region_file.cpp
│   ├── io_manager.cpp
│   ├── data_container.cpp
│   └── batch_builder.cpp
├── tests/                           # GoogleTest unit tests
│   ├── test_position.cpp
│   ├── test_string_interner.cpp
│   ├── test_palette.cpp
│   ├── test_subchunk.cpp
│   ├── test_chunk_column.cpp
│   ├── test_rotation.cpp
│   ├── test_world.cpp
│   ├── test_column_manager.cpp
│   ├── test_batch_builder.cpp
│   ├── test_lru_cache.cpp
│   ├── test_data_container.cpp
│   ├── test_serialization.cpp
│   ├── test_region_file.cpp
│   ├── test_io_manager.cpp
│   ├── test_config.cpp
│   ├── test_config_parser.cpp
│   ├── test_config_file.cpp
│   ├── test_resource_locator.cpp
│   ├── test_physics.cpp
│   ├── test_mesh.cpp
│   ├── test_block_type.cpp
│   ├── test_block_model.cpp
│   ├── test_lod.cpp
│   ├── test_module.cpp
│   ├── test_lighting.cpp
│   ├── test_event_system.cpp
│   ├── test_blocking_queue.cpp
│   ├── test_queue_primitives.cpp
│   └── test_mesh_worker_pool.cpp
├── examples/
│   └── render_demo.cpp              # Interactive demo (VK + finegui)
├── shaders/                         # GLSL shaders
│   ├── chunk.vert                   # Block vertex shader
│   ├── chunk.frag                   # Block fragment shader
│   ├── overlay.vert                 # Overlay2D vertex shader
│   └── overlay.frag                 # Overlay2D fragment shader
└── resources/                       # Data-driven block definitions
    ├── blocks/                      # Block model files
    │   ├── base/                    # Base models (inherited by variants)
    │   │   ├── solid_cube.model
    │   │   ├── slab.model
    │   │   ├── stairs.model
    │   │   └── wedge.model
    │   ├── stone.model
    │   ├── dirt.model
    │   ├── grass.model
    │   ├── cobble.model
    │   ├── glowstone.model
    │   ├── slab.model
    │   ├── stone_slab.model
    │   ├── stairs.model
    │   └── wedge.model
    ├── shapes/                      # Geometry and collision definitions
    │   ├── solid_cube.geom
    │   ├── slab_faces.geom
    │   ├── slab_collision.collision
    │   ├── stairs_faces.geom
    │   ├── stairs_collision.collision
    │   ├── wedge_faces.geom
    │   └── wedge_collision.collision
    └── textures/                    # Block textures
        ├── stone.png
        ├── cobble.png
        ├── wood.png
        ├── steel.png
        └── junk.png
```

---

[Back to Index](INDEX.md)
