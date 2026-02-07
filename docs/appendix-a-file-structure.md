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
│   ├── STYLE_GUIDE.md               # Coding conventions & philosophy
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
│   ├── 27-world-generation.md       # World generation design
│   ├── finegui-design.md            # GUI toolkit design
│   ├── PLAN-mesh-architecture-improvements.md
│   ├── appendix-a-file-structure.md # This file
│   └── appendix-b-differences.md
├── include/finevox/
│   ├── core/                        # Core engine (namespace finevox)
│   │   ├── position.hpp             # BlockPos, ChunkPos, ColumnPos, Face
│   │   ├── subchunk.hpp             # 16³ block storage with palette
│   │   ├── palette.hpp              # SubChunkPalette
│   │   ├── string_interner.hpp      # String→ID interning
│   │   ├── chunk_column.hpp         # Full-height column of subchunks
│   │   ├── world.hpp                # World container
│   │   ├── column_manager.hpp       # Column lifecycle state machine
│   │   ├── block_type.hpp           # BlockType, BlockTypeId, BlockRegistry
│   │   ├── block_model.hpp          # FaceGeometry, BlockGeometry, BlockModel
│   │   ├── block_model_loader.hpp   # .model/.geom/.collision parser
│   │   ├── rotation.hpp             # 24 cube rotations
│   │   ├── mesh.hpp                 # MeshBuilder, MeshData, ChunkVertex
│   │   ├── mesh_worker_pool.hpp     # Parallel mesh generation
│   │   ├── mesh_rebuild_queue.hpp   # Mesh rebuild scheduling
│   │   ├── lod.hpp                  # LOD generation and selection
│   │   ├── physics.hpp              # AABB, CollisionShape, PhysicsSystem
│   │   ├── light_data.hpp           # Per-block light storage
│   │   ├── light_engine.hpp         # BFS light propagation
│   │   ├── block_event.hpp          # BlockEvent types
│   │   ├── block_handler.hpp        # BlockContext, BlockHandler
│   │   ├── event_queue.hpp          # UpdateScheduler, EventOutbox
│   │   ├── entity.hpp               # Entity base
│   │   ├── entity_manager.hpp       # Entity lifecycle
│   │   ├── entity_registry.hpp      # Entity type registration
│   │   ├── item_registry.hpp        # Item registration
│   │   ├── graphics_event_queue.hpp # Game↔graphics messaging
│   │   ├── module.hpp               # ModuleLoader, GameModule
│   │   ├── config.hpp               # ConfigManager, WorldConfig
│   │   ├── config_file.hpp          # Config file parsing
│   │   ├── config_parser.hpp        # Key-value parser
│   │   ├── distances.hpp            # Distance zone calculations
│   │   ├── resource_locator.hpp     # Asset path resolution
│   │   ├── serialization.hpp        # SubChunk/Column CBOR serialization
│   │   ├── cbor.hpp                 # CBOR encoder/decoder
│   │   ├── region_file.hpp          # 32×32 region file I/O
│   │   ├── io_manager.hpp           # Async save/load
│   │   ├── data_container.hpp       # Key-value extra data storage
│   │   ├── batch_builder.hpp        # Block operation batching
│   │   ├── block_data_helpers.hpp   # BlockTypeId storage helpers
│   │   ├── lru_cache.hpp            # Generic LRU cache
│   │   ├── queue.hpp                # Base queue interface
│   │   ├── simple_queue.hpp         # Simple FIFO queue
│   │   ├── coalescing_queue.hpp     # Key-deduplicating queue
│   │   ├── keyed_queue.hpp          # Key-associated data queue
│   │   ├── alarm_queue.hpp          # Timer-based queue
│   │   ├── wake_signal.hpp          # Thread wakeup primitive
│   │   └── deprecated/
│   │       └── blocking_queue.hpp   # Legacy thread-safe queue
│   ├── worldgen/                    # World generation (namespace finevox::worldgen)
│   │   ├── noise.hpp                # Noise2D/3D interfaces, Perlin, OpenSimplex
│   │   ├── noise_ops.hpp            # FBM, ridged, billow, domain warp, NoiseFactory
│   │   ├── noise_voronoi.hpp        # Voronoi tessellation noise
│   │   ├── biome.hpp                # BiomeId, BiomeProperties, BiomeRegistry
│   │   ├── biome_map.hpp            # Spatial biome assignment
│   │   ├── biome_loader.hpp         # .biome file loader
│   │   ├── feature.hpp              # Feature interface, FeaturePlacementContext
│   │   ├── feature_tree.hpp         # TreeFeature, TreeConfig
│   │   ├── feature_ore.hpp          # OreFeature, OreConfig
│   │   ├── feature_schematic.hpp    # SchematicFeature
│   │   ├── feature_registry.hpp     # FeatureRegistry, FeaturePlacement
│   │   ├── feature_loader.hpp       # .feature/.ore file loader
│   │   ├── world_generator.hpp      # GenerationPipeline, GenerationContext
│   │   ├── generation_passes.hpp    # TerrainPass, SurfacePass, CavePass, etc.
│   │   ├── schematic.hpp            # BlockSnapshot, Schematic
│   │   ├── schematic_io.hpp         # CBOR schematic serialization
│   │   └── clipboard_manager.hpp    # Runtime copy/paste
│   └── render/                      # Vulkan rendering (namespace finevox::render)
│       ├── world_renderer.hpp       # Render coordination
│       ├── subchunk_view.hpp        # GPU mesh handle
│       ├── block_atlas.hpp          # Block UV coordinate mapping
│       └── texture_manager.hpp      # Texture atlas management
├── src/
│   ├── core/                        # Core implementations
│   │   ├── position.cpp             ... (mirrors core headers)
│   │   └── ...
│   ├── worldgen/                    # World generation implementations
│   │   ├── noise_perlin.cpp         ... (mirrors worldgen headers)
│   │   └── ...
│   └── render/                      # Vulkan render implementations
│       ├── world_renderer.cpp       ... (mirrors render headers)
│       └── ...
├── tests/                           # GoogleTest unit tests
│   ├── test_position.cpp            # Core tests
│   ├── ...
│   ├── test_noise.cpp               # Worldgen tests
│   ├── test_schematic.cpp
│   ├── test_biome.cpp
│   ├── test_feature.cpp
│   └── test_generation.cpp
├── examples/
│   └── render_demo.cpp              # Interactive demo (VK + finegui)
├── shaders/                         # GLSL shaders
│   ├── chunk.vert
│   ├── chunk.frag
│   ├── overlay.vert
│   └── overlay.frag
└── resources/                       # Data-driven definitions
    ├── blocks/                      # Block model files (.model)
    │   ├── base/                    # Base models (inherited by variants)
    │   └── ...
    ├── shapes/                      # Geometry (.geom) and collision (.collision)
    ├── biomes/                      # Biome definitions (.biome)
    │   ├── plains.biome
    │   ├── forest.biome
    │   ├── desert.biome
    │   └── mountains.biome
    ├── features/                    # Feature definitions (.feature, .ore)
    │   ├── oak_tree.feature
    │   ├── iron_ore.ore
    │   └── coal_ore.ore
    └── textures/                    # Block textures (.png)
```

---

[Back to Index](INDEX.md)
