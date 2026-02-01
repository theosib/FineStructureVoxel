# 26. Network Protocol Design

**Status:** Draft
**Related:** [25-entity-system.md](25-entity-system.md), [24-event-system.md](24-event-system.md), [11-persistence.md](11-persistence.md)

---

## 26.1 Design Philosophy

### 26.1.1 Smart Client Model

We target a **smart client** architecture - thin on game logic, rich on graphics:

| Aspect | Minecraft Client | FineVox Client |
|--------|------------------|----------------|
| World storage | Full block data | Voxel appearance data |
| Block logic | Duplicate handlers | None - server authority |
| Physics | Full simulation | Prediction + collision shapes |
| Meshing | Client generates | Client generates (voxel native) |
| LOD | Client handles | Client handles |
| Hidden face removal | Client | Client |
| Frustum culling | Client | Client |
| Assets | Pre-installed | Cached from server |
| UI definitions | Hardcoded | Server-defined |
| Lighting computation | Client | Server (client can estimate) |

**Key insight:** The client doesn't need to know *what* a block is (stone, furnace, etc.) - it needs to know:
- What it **looks like** (voxel geometry, textures)
- How to **interact** with it (collision shape, hit shape, has-UI flag)
- What **physics rules** apply (solid, liquid, slippery, conveyor velocity)

### 26.1.2 Protocol Principles

1. **Formally specified** - Clear enough for independent implementation
2. **Extensible** - Variable-length message types, graceful unknown handling
3. **Latency-aware** - Design for 50-200ms RTT scenarios
4. **Bandwidth-conscious** - Incremental caching, delta encoding, compression
5. **Serialization-compatible** - CBOR-based, matches persistence format
6. **Voxel-native** - No raw triangles, leverage voxel structure

### 26.1.3 Caching Philosophy

Heavy client-side caching minimizes connection time:

```
First connection:
  Server: "Here are 500 block definitions"
  Client: Downloads and caches all

Second connection:
  Server: "Block definitions checksum: 0xABCD1234"
  Client: "I have that cached, ready to go"
  Server: "Great, here's just the 3 new blocks since last time"
```

**Cache invalidation:**
- Checksums at multiple granularities (full set, per-category)
- Server can push invalidations for specific items
- Version numbers for incremental updates

---

## 26.2 Protocol Framing

### 26.2.1 Variable-Length Message Types

Instead of fixed 16-bit type IDs, use **prefix coding** (like UTF-8):

```
Single byte (0x00-0x7F):    0xxxxxxx           = types 0-127
Two bytes (0x80-0xBF):      10xxxxxx yyyyyyyy  = types 128-16,511
Three bytes (0xC0-0xDF):    110xxxxx yyyy zzzz = types 16,512-2,113,663
Four bytes (0xE0-0xEF):     1110xxxx ...       = extended range
Reserved (0xF0-0xFF):       Reserved for future encoding changes
```

**Benefits:**
- Common messages (position updates) use 1 byte
- Extensible without breaking existing parsers
- Unknown message types can be skipped (length prefix)

### 26.2.2 Message Structure

```
+----------------+------------------+------------------+
| Length (varint)| Type (prefix)    | Payload (CBOR)   |
+----------------+------------------+------------------+
```

- **Length**: Variable-length integer, total payload size
- **Type**: Variable-length prefix code
- **Payload**: CBOR-encoded message body

### 26.2.3 Version Negotiation

```
ClientHello:
  protocol_version: u32          // Protocol version client supports
  client_version: string         // Client software version
  cache_manifest: CacheManifest  // What client already has cached
  supported_extensions: [string]

ServerHello:
  protocol_version: u32
  server_version: string
  enabled_extensions: [string]
  session_id: u64
  cache_status: CacheValidation  // What client can reuse

CacheManifest:
  block_defs_hash: bytes         // Hash of cached block definitions
  item_defs_hash: bytes          // Hash of cached item definitions
  entity_defs_hash: bytes        // Hash of cached entity models
  texture_hashes: {id: bytes}    // Per-atlas hashes
  ui_defs_hash: bytes            // Hash of cached UI definitions
  rules_hash: bytes              // Hash of cached client rules
  audio_hashes: {id: bytes}      // Per-sample hashes
```

---

## 26.3 The Quantization Problem

### 26.3.1 Minecraft's Bug

Minecraft quantizes entity positions to 12 fractional bits (1/4096 block precision):
```
Server: entity at Y = 5.00001 (standing on block Y=4..5)
Quantized: Y = 5.0 (rounds down)
Client: entity at Y = 5.0 exactly
Client physics: "touching block surface, might fall"
Result: Entity visually bobs up and down
```

### 26.3.2 Our Solution: Semantic Quantization

**Principle:** Send the position that produces the correct client-side interpretation, not the mathematically closest approximation.

```cpp
struct EntityPositionUpdate {
    EntityId entity;

    // Quantized position (20.12 fixed point)
    int32_t x, y, z;

    // State flags that inform interpretation
    uint8_t flags;
    // bit 0: is_grounded
    // bit 1: is_swimming
    // bit 2: is_climbing
    // bit 3: is_flying
    // bit 4: is_on_conveyor

    // For grounded entities: authoritative ground reference
    int32_t ground_y;  // Block Y coordinate (integer)
};
```

**Quantization rules:**

1. **Grounded entities**: Round Y **up** by at least 1 LSB above surface
2. **Projectiles**: For landed arrows, send landing anchor (block + face + offset)
3. **Vehicles**: Send anchor block for wheeled/railed entities

### 26.3.3 Anchor-Based Positions

```cpp
struct AnchoredPosition {
    enum AnchorType : uint8_t {
        Absolute,         // Raw position (flying, falling)
        OnBlock,          // Standing on block surface
        InBlock,          // Swimming in water
        OnWall,           // Climbing
        AttachedToEntity, // Riding
        OnConveyor,       // Moving with conveyor belt
        OnRail,           // Minecart on rail
    };

    AnchorType type;

    // Anchor-specific data
    union {
        struct { int32_t x, y, z; } absolute;
        struct { BlockPos block; uint8_t face; uint16_t offset; } surface;
        struct { EntityId parent; uint8_t seat; } attached;
    };
};
```

---

## 26.4 World Data Protocol

### 26.4.1 Voxel Data Model

The client stores **appearance data**, not game data. Server sends voxel information in a format optimized for client-side meshing:

```cpp
struct ChunkVoxelData {
    ChunkPos pos;

    // Palette-compressed voxel data (like our SubChunk format)
    // But palette entries are VoxelAppearance, not BlockTypeId
    uint16_t palette_size;
    std::vector<VoxelAppearance> palette;
    bytes voxel_indices;  // Bit-packed indices into palette

    // Light data (server-computed)
    bytes block_light;    // 4 bits per voxel
    bytes sky_light;      // 4 bits per voxel
};

struct VoxelAppearance {
    uint16_t appearance_id;  // References cached BlockAppearance
    uint8_t rotation;        // Block rotation (0-23)
    uint8_t variant;         // Random variant index
};
```

### 26.4.2 Block Appearance Definitions

Sent once (and cached), defines how blocks look and behave:

```cpp
struct BlockAppearance {
    uint16_t appearance_id;       // Compact session ID
    string resource_id;           // Canonical name "finevox:stone"

    // Display info (for tooltips, UI)
    string display_name;          // "Stone" (localized by server)
    optional<string> description; // Tooltip text

    // Geometry (voxel-native, not triangles)
    VoxelGeometry geometry;  // Could be full cube, slab, stairs, custom

    // Textures (per-face UVs into cached atlas)
    uint16_t texture_atlas_id;
    FaceUVs face_uvs[6];

    // Animation (optional)
    optional<TextureAnimation> animation;

    // Physics/interaction shapes
    ShapeDefinition collision_shape;
    ShapeDefinition hit_shape;

    // Physics properties
    PhysicsProperties physics;

    // Interaction
    uint8_t interaction_flags;
    // bit 0: can_break
    // bit 1: can_interact (right-click opens UI)
    // bit 2: can_place_on

    // Client-side rules (see §26.16)
    optional<uint16_t> break_rule_id;  // Particle/sound on break
    optional<uint16_t> step_rule_id;   // Sound when walked on
};

struct PhysicsProperties {
    enum SurfaceType : uint8_t {
        Solid,
        Liquid,
        Slippery,    // Ice
        Bouncy,      // Slime
        Climbable,   // Ladder, vine
        Conveyor,    // Moving surface
    } surface;

    // For liquids
    float viscosity;
    float buoyancy;

    // For conveyors
    Vec3 velocity;  // Movement imparted to entities

    // For slippery
    float friction;
};
```

### 26.4.3 Client-Side Meshing

The client generates meshes from voxel data:

1. **Hidden face removal** - Client-side, comparing adjacent voxels
2. **Greedy meshing** - Client optimization
3. **LOD generation** - Client simplifies for distance
4. **AO calculation** - Client computes from voxel adjacency

Server sends:
- Voxel data (what's in each cell)
- Light values (pre-computed, updated incrementally)
- Appearance definitions (cached)

Client generates:
- Triangle meshes
- LOD variants
- Draw commands

**Chunk boundary handling:**

At chunk edges, client needs neighbor voxel data for hidden face removal:

```cpp
// Client uses best available info for boundary faces
Face getBoundaryBehavior(ChunkPos neighbor_chunk, LocalPos edge_pos) {
    if (!hasChunkData(neighbor_chunk)) {
        // No neighbor data yet - show the face
        // This prevents x-ray cheats and handles loading edges
        return Face::Visible;
    }

    // Have neighbor data - normal hidden face check
    auto neighbor_voxel = getVoxel(neighbor_chunk, opposite_edge(edge_pos));
    return isOccluding(neighbor_voxel) ? Face::Hidden : Face::Visible;
}
```

**Rationale:** Missing neighbor = visible faces prevents:
- X-ray exploits (hiding faces the server hasn't sent data for)
- Visual holes at world edges / loading boundaries
- Client needs to re-mesh boundary when neighbor arrives

### 26.4.4 Lighting

Server computes lighting (heavyweight, persistent):
- Block light propagation
- Sky light propagation
- Updates on block changes

Server sends light values with chunk data. Client can:
- Use server values directly (authoritative)
- Estimate before server responds (for instant break/place feedback)
- Smooth transitions when server values arrive

```cpp
struct LightUpdate {
    ChunkPos chunk;
    std::vector<LightChange> changes;
};

struct LightChange {
    uint16_t local_index;  // Position within chunk
    uint8_t block_light;   // 0-15
    uint8_t sky_light;     // 0-15
};
```

---

## 26.5 Block Interaction Protocol

### 26.5.1 Instant Break/Place

For responsive gameplay, client predicts break/place results:

```
Client                              Server
   |                                   |
   |-- BreakBlock(pos) -------------->|
   |   (immediately show broken)      |
   |   (update local collision cache) |
   |   (estimate new lighting)        |
   |                                  | (validate)
   |<----------- Confirm -------------|  (OK, here's official light)
   |   OR                             |
   |<----------- Reject --------------|  (undo, restore block)
```

**Client needs for prediction:**
- Collision shapes (what can player walk through?)
- Which blocks are breakable
- What placing creates (selected item → appearance)

### 26.5.2 Interaction Data

For blocks near the player, server sends interaction metadata:

```cpp
struct NearbyBlockInfo {
    // Sparse map: only blocks with non-default interaction
    std::map<BlockPos, BlockInteraction> blocks;
};

struct BlockInteraction {
    uint16_t appearance_id;      // For rendering
    ShapeRef collision_shape;    // For movement
    ShapeRef hit_shape;          // For targeting
    uint8_t flags;               // Breakable, interactable, etc.
    optional<uint16_t> ui_id;    // UI to open on interact (cached)
};
```

Client maintains a sparse cache of interaction data within ~5-10 blocks.

---

## 26.6 Motion and Animation

### 26.6.1 Motion Interpolation

Entity motion includes **explicit start and end points**:

```cpp
struct EntityMotion {
    EntityId entity;

    // Motion segment
    Vec3 start_pos;
    Vec3 end_pos;
    uint64_t start_tick;
    uint64_t end_tick;

    // Easing/curve type
    enum Curve : uint8_t {
        Linear,
        EaseIn,
        EaseOut,
        EaseInOut,
        Ballistic,  // Gravity-affected arc
    } curve;

    // Optional: explicit control points for complex paths
    optional<std::vector<Vec3>> path_points;
};
```

**Benefits:**
- No extrapolation needed - client knows endpoint
- Network latency doesn't cause overshoot
- Server can update motion mid-flight

### 26.6.2 Motion Updates

When motion changes (entity turns, stops, etc.):

```cpp
struct EntityMotionUpdate {
    EntityId entity;

    // New motion, seamlessly blended from current position
    EntityMotion new_motion;

    // Blend duration (for smooth transition)
    uint8_t blend_ticks;  // 0 = instant, N = blend over N ticks
};
```

**Overlapping animations:**
```cpp
class EntityAnimator {
    // Current motion being rendered
    EntityMotion current_;

    // Pending motion (blending toward)
    optional<EntityMotion> pending_;
    float blend_progress_;

    Vec3 getCurrentPosition(uint64_t tick) {
        Vec3 pos = current_.evaluate(tick);
        if (pending_) {
            Vec3 target = pending_->evaluate(tick);
            pos = lerp(pos, target, blend_progress_);
        }
        return pos;
    }
};
```

---

## 26.7 Asset Streaming and Caching

### 26.7.1 Incremental Asset Loading

Assets are requested **just-in-time** and cached persistently:

```cpp
// Client requests assets as needed
struct AssetRequest {
    enum Type : uint8_t {
        BlockAppearance,
        EntityModel,
        TextureAtlas,
        UIDefinition,
        AudioSample,
    } type;

    uint16_t id;
};

// Server responds with asset or "use cache"
struct AssetResponse {
    enum Status : uint8_t {
        Data,       // Here's the asset
        UseCache,   // Your cached version is valid
        NotFound,   // Unknown asset ID
    } status;

    bytes data;  // CBOR-encoded asset if Status::Data
};
```

### 26.7.2 Bulk Cache Validation

On connection, validate entire cache categories:

```cpp
struct CacheValidation {
    // Per-category: hash of all definitions
    bytes block_appearances_hash;
    bytes item_appearances_hash;
    bytes entity_models_hash;
    bytes ui_definitions_hash;
    bytes client_rules_hash;
    bytes particle_types_hash;

    // Incremental: list of changed IDs since version N
    uint32_t block_version;
    std::vector<uint16_t> changed_block_ids;
    uint32_t item_version;
    std::vector<uint16_t> changed_item_ids;

    // Invalidated: these cache entries are stale
    std::vector<uint16_t> invalid_block_ids;
    std::vector<uint16_t> invalid_item_ids;
};
```

### 26.7.3 Texture Atlases

```cpp
struct TextureAtlas {
    uint16_t atlas_id;
    bytes content_hash;  // For cache validation

    uint16_t width, height;
    bytes image_data;  // Compressed (PNG, BC7, etc.)

    // Named texture regions
    std::map<string, UVRect> regions;
};
```

### 26.7.4 Audio Samples

```cpp
struct AudioSample {
    uint16_t sample_id;
    bytes content_hash;

    string name;  // "block.stone.break"
    bytes audio_data;  // Compressed audio (opus, vorbis)

    AudioProperties props;
};

struct AudioProperties {
    float base_volume;
    float pitch_variance;  // Random pitch variation
    float attenuation;     // Distance falloff
    bool positional;       // 3D positioned or ambient
};
```

### 26.7.5 Item Appearances

Items (for inventory display) have their own appearance definitions:

```cpp
struct ItemAppearance {
    uint16_t appearance_id;
    string resource_id;       // "finevox:diamond_pickaxe"

    // Display
    string display_name;      // "Diamond Pickaxe"
    optional<string> description;  // Tooltip lines
    uint8_t max_stack;        // 64, 16, or 1

    // Icon (2D representation)
    uint16_t icon_atlas_id;
    UVRect icon_uv;

    // 3D model for held/dropped items
    optional<uint16_t> model_id;

    // Durability bar
    bool has_durability;

    // Use action hint (for client UI feedback)
    enum UseType : uint8_t {
        None,
        PlaceBlock,    // Shows ghost placement
        Usable,        // Right-click action
        Consumable,    // Eat/drink
        Weapon,        // Attack arc
        Tool,          // Mining indicator
    } use_type;

    // Associated block (for block items)
    optional<uint16_t> places_block_appearance;
};
```

---

## 26.8 UI Protocol

### 26.8.1 Declarative UI Definitions

UIs are defined declaratively and cached:

```cpp
struct UIDefinition {
    uint16_t ui_id;
    string name;  // "chest_9x3", "furnace", etc.

    // Layout tree
    UIElement root;

    // Slot definitions (for inventory UIs)
    std::vector<SlotDef> slots;

    // Data bindings (what server state maps to what elements)
    std::vector<DataBinding> bindings;
};

struct UIElement {
    enum Type : uint8_t {
        Container,      // Layout container (HBox, VBox, Grid)
        Slot,           // Interactive inventory slot
        ItemDisplay,    // Non-interactive item icon + quantity
        ProgressBar,    // Furnace progress, etc.
        Label,          // Text
        Button,         // Clickable
        Image,          // Static image
        Tooltip,        // Hover tooltip container
    } type;

    string element_id;  // For data bindings
    LayoutSpec layout;
    variant<...> props;
    std::vector<UIElement> children;
};
```

### 26.8.2 Generic Inventory Slot Element

The `Slot` element is a **reusable component** for any inventory display:

```cpp
struct SlotProps {
    uint8_t slot_index;      // Which slot in the UI's slot array
    bool can_insert;         // Player can put items here
    bool can_extract;        // Player can take items out
    optional<uint16_t> filter;  // Only accept certain item types
};
```

Client renders slots with:
- Item icon (from cached item appearance)
- Stack count
- Durability bar (if applicable)
- Highlight on hover
- Drag/drop handling

### 26.8.3 Item Display Element

For displaying items without interaction (recipe outputs, quest rewards, etc.):

```cpp
struct ItemDisplayProps {
    // Static item to display (from UI definition)
    optional<ItemRef> static_item;

    // Or dynamic binding (from server state)
    optional<string> binding_id;  // e.g., "recipe_output"

    // Display options
    bool show_quantity;    // Show stack count
    bool show_tooltip;     // Show item name on hover
    uint8_t icon_size;     // Pixels (16, 32, 48)
};

struct ItemRef {
    uint16_t appearance_id;
    uint8_t count;
};
```

**Client-side rendering:**
- Icon from item appearance's texture
- Quantity badge (bottom-right)
- Tooltip on hover (display_name from appearance)

### 26.8.4 Opening UI

```cpp
// Server opens a UI
struct OpenUI {
    uint16_t ui_id;           // Which UI definition
    uint16_t instance_id;     // Unique instance (for multiple chests)

    // Initial state
    std::vector<SlotContents> slots;
    std::map<string, variant> properties;  // Progress bars, labels, etc.
};

struct SlotContents {
    uint16_t item_appearance_id;  // 0 = empty
    uint8_t count;
    bytes item_data;  // Custom item properties
};
```

### 26.8.4 UI Updates and Actions

```cpp
// Server updates UI state
struct UIUpdate {
    uint16_t instance_id;
    std::vector<SlotChange> slot_changes;
    std::map<string, variant> property_changes;
};

// Client sends interaction
struct UIAction {
    uint16_t instance_id;

    enum Action : uint8_t {
        SlotClick,
        SlotShiftClick,
        SlotDrag,
        ButtonClick,
        Close,
    } action;

    union {
        struct { uint8_t slot; uint8_t button; } click;
        struct { uint8_t from; uint8_t to; uint8_t count; } drag;
        uint8_t button_id;
    };
};
```

**Server validates all actions** - client shows immediate visual feedback, server confirms or rejects.

---

## 26.9 Player Input Protocol

### 26.9.1 Movement Input

```cpp
struct PlayerInput {
    uint32_t sequence;
    uint32_t tick;

    uint8_t movement_flags;
    // bits: forward, back, left, right, jump, sneak, sprint

    int16_t yaw, pitch;  // Quantized look direction

    // Predicted position (for reconciliation)
    int32_t pred_x, pred_y, pred_z;  // 20.12 fixed
};
```

### 26.9.2 Physics Prediction

Client predicts movement using physics properties from block appearances:

```cpp
class ClientPhysics {
    // Player collision shape
    AABB player_bounds_;

    // Cached interaction data for nearby blocks
    InteractionCache cache_;

    void predictMovement(PlayerInput input, float dt) {
        // Check ground type
        auto ground = cache_.getBlockAt(below(player_pos_));

        switch (ground.physics.surface) {
            case Solid:
                applyNormalMovement(input, dt);
                break;
            case Slippery:
                applySlipperyMovement(input, dt, ground.physics.friction);
                break;
            case Liquid:
                applySwimmingMovement(input, dt, ground.physics);
                break;
            case Conveyor:
                applyConveyorMovement(input, dt, ground.physics.velocity);
                break;
        }
    }
};
```

### 26.9.3 Riding Entities

```cpp
struct MountInput {
    EntityId mount;
    uint8_t control_flags;  // Forward, turn left/right, jump
};
```

When riding, player input controls the mount. Client predicts mount movement, server validates.

---

## 26.10 Latency Mitigation

### 26.10.1 Prediction and Reconciliation

```
Client                              Server
   |                                   |
   |-- Input(seq=1, pred_pos=A) ----->|
   |   (move to A locally)            |
   |-- Input(seq=2, pred_pos=B) ----->|
   |   (move to B locally)            |
   |                                  | (process seq=1 → actual pos A')
   |<--------- Ack(seq=1, pos=A') ----|
   |   (if A ≈ A': good)              |
   |   (if A ≠ A': reconcile)         |
```

### 26.10.2 Optimistic Block Updates

```cpp
class OptimisticWorld {
    // Server-confirmed state
    std::map<ChunkPos, ChunkVoxelData> confirmed_;

    // Pending local changes (awaiting server confirmation)
    std::map<BlockPos, PendingChange> pending_;

    VoxelAppearance getVoxel(BlockPos pos) {
        if (auto it = pending_.find(pos); it != pending_.end()) {
            return it->second.optimistic_appearance;
        }
        return confirmed_[chunkOf(pos)].getVoxel(localPos(pos));
    }

    void applyConfirmation(BlockPos pos, VoxelAppearance confirmed) {
        pending_.erase(pos);
        confirmed_[chunkOf(pos)].setVoxel(localPos(pos), confirmed);
    }

    void applyRejection(BlockPos pos) {
        // Revert to confirmed state
        pending_.erase(pos);
        // Client will re-mesh with original voxel
    }
};
```

### 26.10.3 Light Estimation

Before server confirms lighting:

```cpp
uint8_t estimateBlockLight(BlockPos pos, VoxelAppearance voxel) {
    // Simple heuristic: if block emits light, use that
    // Otherwise, average neighbors' light
    if (voxel.emission > 0) return voxel.emission;

    uint8_t sum = 0;
    for (auto neighbor : neighbors(pos)) {
        sum += getConfirmedLight(neighbor);
    }
    return std::max(0, (sum / 6) - 1);
}
```

---

## 26.11 Bandwidth Optimization

### 26.11.1 Delta Encoding

```cpp
struct EntityDelta {
    EntityId id;
    uint8_t changed;  // Bitmask

    optional<int16_t> dx, dy, dz;  // Position delta (smaller range)
    optional<int8_t> d_yaw, d_pitch;  // Rotation delta
    optional<uint8_t> state;
};
```

### 26.11.2 Compression

- **Chunk data**: zstd compression (good for voxel patterns)
- **Batched updates**: Frame-level compression
- **Assets**: Format-appropriate (PNG/BC7 for textures, opus for audio)

### 26.11.3 Priority Throttling

```cpp
enum Priority : uint8_t {
    Critical,  // Player actions, collisions
    High,      // Nearby entities, block changes
    Normal,    // Distant updates
    Low,       // Cosmetic, can drop
};
```

Server drops Low priority during congestion.

---

## 26.12 Client Architecture Summary

### What the client DOES (graphics-heavy):

- **Meshing** - Generate triangles from voxel data
- **Hidden face removal** - Compare adjacent voxels
- **LOD generation** - Simplify distant geometry
- **Frustum culling** - Discard off-screen chunks
- **Depth sorting** - Transparency ordering
- **Physics prediction** - Player movement, collision
- **Animation/interpolation** - Smooth entity motion
- **UI rendering** - From server definitions
- **Audio playback** - Positioned sounds
- **Light blending** - Smooth transitions

### What the client DOES NOT (logic-heavy):

- **Game rules** - Server validates all actions
- **Entity AI** - Server simulates
- **World generation** - Server generates
- **Light propagation** - Server computes
- **Inventory logic** - Server validates

### Data the client caches:

1. **Block appearances** - How blocks look, shapes, physics
2. **Entity models** - Geometry, animation, textures
3. **Texture atlases** - All textures
4. **UI definitions** - Layout descriptions
5. **Audio samples** - Sound effects
6. **Client rules** - Particle/sound triggers
7. **Particle types** - Particle rendering definitions
8. **Item appearances** - Item icons, names, tooltips
9. **Chunk voxel data** - Current world state (transient)
10. **Entity state** - Positions, animations (transient)

---

## 26.13 Message Type Reference

Using prefix encoding, common messages get short codes:

### Frequent Messages (1 byte: 0x00-0x7F)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x01 | PlayerInput | C→S | Movement input |
| 0x02 | InputAck | S→C | Input acknowledgment |
| 0x03 | EntityPosition | S→C | Single entity update |
| 0x04 | EntityBatch | S→C | Multiple entity updates |
| 0x05 | BlockChange | S→C | Single block change |
| 0x06 | LightUpdate | S→C | Light value changes |
| 0x07 | KeepAlive | Both | Heartbeat |
| 0x08 | SoundEvent | S→C | Play positioned sound |
| 0x09 | EffectTrigger | S→C | Trigger client rule |
| 0x0A | ParticleSpawn | S→C | Spawn particles directly |

### Less Frequent (2 bytes: 0x80-0xBF + next byte)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0x80 0x01 | ChunkData | S→C | Full chunk voxel data |
| 0x80 0x02 | ChunkUnload | S→C | Discard chunk |
| 0x80 0x03 | PlayerAction | C→S | Break/place/interact |
| 0x80 0x04 | ActionResult | S→C | Confirm/reject action |
| 0x80 0x10 | OpenUI | S→C | Open UI screen |
| 0x80 0x11 | CloseUI | Both | Close UI |
| 0x80 0x12 | UIUpdate | S→C | UI state change |
| 0x80 0x13 | UIAction | C→S | UI interaction |

### Rare (3 bytes: 0xC0 + ...)

| Code | Name | Direction | Description |
|------|------|-----------|-------------|
| 0xC0 ... | ClientHello | C→S | Connection handshake |
| 0xC0 ... | ServerHello | S→C | Handshake response |
| 0xC0 ... | AssetRequest | C→S | Request missing asset |
| 0xC0 ... | AssetResponse | S→C | Asset data |
| 0xC0 ... | BlockAppearanceList | S→C | Block definitions |

---

## 26.14 Client Rules System

Analogous to UI definitions, **client rules** describe automatic client behaviors triggered by game events. This avoids round-trips for cosmetic effects while keeping the server authoritative over game state.

### 26.14.1 Rule Definitions

```cpp
struct ClientRule {
    uint16_t rule_id;
    string name;  // "stone_break", "water_splash"

    // Trigger condition
    RuleTrigger trigger;

    // Actions to perform
    std::vector<RuleAction> actions;
};

enum class TriggerType : uint8_t {
    BlockBreak,       // Block broken at position
    BlockPlace,       // Block placed at position
    EntityStep,       // Entity steps on block
    EntityLand,       // Entity lands on block
    ProjectileHit,    // Arrow/projectile impacts
    EntityDamage,     // Entity takes damage
    WeatherChange,    // Rain starts/stops at position
};

struct RuleTrigger {
    TriggerType type;
    // Conditions (optional filtering)
    optional<uint16_t> block_appearance;  // Only this block type
    optional<uint16_t> entity_model;      // Only this entity type
};

struct RuleAction {
    enum Type : uint8_t {
        SpawnParticles,
        PlaySound,
        ScreenShake,
        FlashTint,
    } type;

    variant<ParticleSpec, SoundSpec, ShakeSpec, TintSpec> params;
};
```

### 26.14.2 Particle Specifications

```cpp
struct ParticleSpec {
    uint16_t particle_type_id;  // Cached particle definition
    uint8_t count_min, count_max;
    Vec3 velocity_base;
    Vec3 velocity_variance;
    float lifetime_min, lifetime_max;
};

struct ParticleType {
    uint16_t type_id;
    uint16_t texture_atlas_id;
    UVRect texture_uv;

    // Physics
    float gravity;
    float drag;
    bool collides_with_blocks;

    // Rendering
    float size_start, size_end;
    Color color_start, color_end;
    float alpha_start, alpha_end;
};
```

### 26.14.3 Server-Triggered Effects

Server can also explicitly trigger effects:

```cpp
// Server → Client
struct EffectTrigger {
    uint16_t rule_id;     // Which rule to execute
    Vec3 position;        // Where
    optional<EntityId> entity;  // Context entity (if applicable)
};
```

This allows:
- **Client rules** for predictable effects (block break → particles)
- **Server triggers** for unpredictable effects (explosion at position X)

### 26.14.4 Rule Examples

```
Rule "stone_break":
  trigger: BlockBreak(appearance=stone)
  actions:
    - SpawnParticles(type=rock_fragment, count=5-8)
    - PlaySound(sample=block.stone.break, volume=1.0)

Rule "water_splash":
  trigger: EntityLand(block_appearance=water)
  actions:
    - SpawnParticles(type=water_droplet, count=10-20, velocity_up=3.0)
    - PlaySound(sample=entity.splash, volume_by_speed=true)

Rule "torch_ambient":
  trigger: Always(block_appearance=torch)  // Continuous
  actions:
    - SpawnParticles(type=flame, count=1, rate=0.5/sec)
    - SpawnParticles(type=smoke, count=1, rate=0.3/sec)
```

---

## 26.15 Resource Identification

### 26.15.1 Canonical Resource IDs

All assets use namespaced string identifiers:

```
namespace:path/to/resource

Examples:
  finevox:stone
  finevox:blocks/furnace
  mymod:custom_block
  finevox:ui/chest_3x9
  finevox:sounds/block/stone/break1
```

### 26.15.2 Session IDs

For wire efficiency, string IDs are mapped to compact integers per session:

```cpp
struct ResourceMapping {
    string resource_id;      // "finevox:stone"
    uint16_t session_id;     // 42 (compact, this session only)
    bytes content_hash;      // For cache validation
};
```

- **String IDs** used in: cache lookups, cross-server identification, logging
- **Session IDs** used in: wire protocol, voxel data palettes
- Mapping established at connection, stable for session duration

### 26.15.3 Cache Key Format

```
{server_identity}/{resource_type}/{content_hash}

Examples:
  example.com:25565/block/a1b2c3d4e5...
  example.com:25565/texture/f6g7h8i9...
```

Client cache is per-server, validated by content hash.

---

## 26.16 Open Questions

1. **Entity rendering data** - Enough in model defs, or need more?
2. **Weather/sky** - Server state or client rendering?
3. **Chat/commands** - String protocol details
4. **Command blocks** - How to handle server-side scripting display

---

## 26.17 Future Considerations

- **UDP channel** - Unreliable for position updates
- **WebSocket/WebRTC** - Browser client
- **Relay servers** - NAT traversal
- **Spectator mode** - Minimal state
- **Recording/replay** - Packet format
- **Modding hooks** - Client-side visual mods

---

[Previous: Entity System](25-entity-system.md) | [Next: TBD]
