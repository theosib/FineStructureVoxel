# 4. Core Data Structures

[Back to Index](INDEX.md) | [Previous: Architecture](03-architecture.md)

---

## 4.1 Position Types

```cpp
namespace finevox {

// Block position in world coordinates
// Packed: X(26 bits) | Z(26 bits) | Y(12 bits) = 64 bits
// Range: +/-33M blocks in X/Z, +/-2K blocks in Y
struct BlockPos {
    int32_t x, y, z;

    uint64_t pack() const;
    static BlockPos unpack(uint64_t packed);

    ChunkPos toChunkPos() const;
    int toLocalIndex() const;  // Index within subchunk (0-4095)

    BlockPos neighbor(Face face) const;

    // Operators
    bool operator==(const BlockPos& other) const;
    BlockPos operator+(const BlockPos& offset) const;
};

// SubChunk position (BlockPos >> 4 for all axes)
struct ChunkPos {
    int32_t x, y, z;

    uint64_t pack() const;
    static ChunkPos unpack(uint64_t packed);

    BlockPos cornerBlockPos() const;  // Block at (0,0,0) of subchunk
    ColumnPos toColumnPos() const { return {x, z}; }

    bool operator==(const ChunkPos& other) const;
};

// Column position (just X, Z - no Y component)
struct ColumnPos {
    int32_t x, z;

    uint64_t pack() const;
    static ColumnPos unpack(uint64_t packed);

    static ColumnPos fromBlockPos(BlockPos pos) {
        return {pos.x >> 4, pos.z >> 4};
    }

    bool operator==(const ColumnPos& other) const;
};

// Face/direction enum with utilities
enum class Face : uint8_t {
    NegX = 0, PosX = 1,
    NegY = 2, PosY = 3,
    NegZ = 4, PosZ = 5
};

Face opposite(Face f);
glm::ivec3 faceNormal(Face f);
glm::ivec3 faceOffset(Face f);

}  // namespace finevox
```

---

## 4.2 Pointer Usage Strategy

Before diving into the data structures, here's the strategy for pointer types:

| Resource | Pointer Type | Rationale |
|----------|--------------|-----------|
| `ChunkColumn` | `shared_ptr` | Multiple owners: active map, cache, render thread |
| `SubChunk` in `Block` | `shared_ptr` | Keeps subchunk alive during block operations |
| `ChunkColumn*` in `SubChunk` | raw pointer | Parent always outlives child |
| `BlockType*` | raw pointer | Global registry outlives all blocks |
| `World*` | raw pointer | Singleton-ish, outlives everything |

**Guideline:** Use `shared_ptr` when multiple owners or when the pointer holder needs to extend lifetime. Use raw pointers for "borrowed" references where the referent is guaranteed to outlive the user.

---

## 4.3 String Interning for Block Type Names

Block types are identified by name (e.g., `"minecraft:stone"`), but comparing strings everywhere is expensive. We use **string interning** to get the best of both worlds:

```cpp
namespace finevox {

// Interned string ID - cheap to copy and compare
// Globally unique for the lifetime of the program
using InternedId = uint32_t;

class StringInterner {
public:
    static StringInterner& instance();

    // Intern a string, returning its ID (thread-safe)
    // Same string always returns same ID
    InternedId intern(std::string_view str);

    // Get string back from ID (fast, O(1))
    std::string_view lookup(InternedId id) const;

    // Check if string is already interned
    std::optional<InternedId> find(std::string_view str) const;

private:
    mutable std::shared_mutex mutex_;
    std::vector<std::string> strings_;           // ID -> string
    std::unordered_map<std::string_view, InternedId> stringToId_;
};

// Convenience wrapper
struct BlockTypeId {
    InternedId id;

    BlockTypeId() : id(0) {}  // 0 = air
    explicit BlockTypeId(InternedId i) : id(i) {}
    static BlockTypeId fromName(std::string_view name) {
        return BlockTypeId(StringInterner::instance().intern(name));
    }

    std::string_view name() const {
        return StringInterner::instance().lookup(id);
    }

    bool operator==(BlockTypeId other) const { return id == other.id; }
    bool operator!=(BlockTypeId other) const { return id != other.id; }
};

}  // namespace finevox
```

This allows:
- **Unlimited block types globally** (up to 4 billion with uint32_t)
- **Cheap comparison** (just compare integers)
- **Human-readable names** preserved for debugging/serialization

---

## 4.4 Per-SubChunk Block Type Registry

While `BlockTypeId` uses 32 bits globally, most subchunks contain far fewer than 4096 unique block types. We use a **per-subchunk palette** to compress storage:

```cpp
namespace finevox {

class SubChunkPalette {
public:
    // Add a block type to palette, return local index
    // Returns existing index if already present
    uint16_t addType(BlockTypeId globalId);

    // Get global ID from local index
    BlockTypeId getGlobalId(uint16_t localIndex) const;

    // Get local index from global ID (returns nullopt if not in palette)
    std::optional<uint16_t> getLocalIndex(BlockTypeId globalId) const;

    // Number of unique types in this subchunk
    size_t size() const { return palette_.size(); }

    // Bits needed to store a local index (for compression)
    int bitsPerBlock() const;

    // Serialization
    void serialize(std::ostream& out) const;
    static SubChunkPalette deserialize(std::istream& in);

private:
    std::vector<BlockTypeId> palette_;                    // local index -> global ID
    std::unordered_map<InternedId, uint16_t> idToLocal_;  // global ID -> local index
};

}  // namespace finevox
```

**Storage optimization:**
- SubChunk with 1 block type: 0 bits per block (constant)
- SubChunk with 2-16 types: 4 bits per block
- SubChunk with 17-256 types: 8 bits per block
- SubChunk with 257-4096 types: 12 bits per block
- SubChunk with >4096 types: 16 bits per block (theoretical max)

**Bit-packing strategy (for serialization):**

In-memory we use unpacked uint16_t per block for fast access. Bit-packing only applies during serialization to disk.

Two packing approaches:
1. **Word-straddling:** Pack indices tightly, allowing them to cross 64-bit word boundaries
2. **Word-aligned:** Only pack indices within 64-bit words, padding remainder bits

We choose **word-aligned packing** for serialization:
- Avoids branch-heavy boundary detection code
- Simpler bit manipulation (no cross-word masking)
- Better for zlib/lz4 compression: straddling creates high-entropy bit patterns that compress poorly
- Minor space overhead (few wasted bits per word) is acceptable for disk storage
- Lesson from Minecraft: word-straddling was a performance bottleneck and didn't help compression

Example for 5-bit indices (17-32 palette entries):
```
Word-aligned: 12 indices per 64-bit word (60 bits used, 4 bits padding)
              [idx0|idx1|idx2|...|idx11|pad]

Word-straddling: ~12.8 indices per word (complex boundary logic)
                 Worse compression, more CPU overhead
```

---

## 4.5 Block System

> **Implementation Note (Current State):**
> The current implementation uses a simplified approach:
> - `SubChunk::getBlock(x,y,z)` returns `BlockTypeId` directly
> - `World::getBlock()` returns `BlockTypeId` directly
> - No `Block` wrapper class is implemented yet
>
> The full `Block` wrapper design below will be needed when we implement:
> - BlockDisplacement (off-grid blocks)
> - Per-block custom data (DataContainer)
> - Block rotation state
>
> When implemented, the `Block` wrapper will exist on the caller's stack with
> low overhead, providing a convenient container that points to data rather
> than copying it.

```cpp
namespace finevox {

// Forward declarations
class SubChunk;
class BlockType;

// Ephemeral block descriptor
// Holds shared_ptr to SubChunk to keep it alive during use
// Cheap to create for short-lived operations
struct Block {
    std::shared_ptr<SubChunk> subchunk;
    BlockPos pos;
    uint16_t localIndex;  // Index in subchunk's block storage (0-4095)

    // Convenience accessors
    BlockTypeId typeId() const;
    BlockType* type() const;       // Raw pointer - registry outlives blocks
    uint8_t rotation() const;
    void setRotation(uint8_t rot);

    // Sub-block displacement for off-grid rendering (see BlockDisplacement)
    BlockDisplacement getDisplacement() const;
    void setDisplacement(BlockDisplacement disp);
    bool hasDisplacement() const;

    // Data access (returns nullptr if no custom data)
    DataContainer* getData();
    DataContainer* getOrCreateData();

    // Neighbor queries (may return Block with null subchunk if unloaded)
    Block neighbor(Face face) const;
    bool isAir() const;
    bool isSolid() const;
    bool isTranslucent() const;
    bool isValid() const { return subchunk != nullptr; }
};

// Block type definition (singleton per type, owned by global registry)
//
// **Implementation Note (Current State):**
// The current BlockType in block_type.hpp uses a data-driven approach:
// - Builder pattern: setCollisionShape(), setOpaque(), setHardness(), etc.
// - No virtual methods - all properties are data members
// - Precomputed 24-rotation collision/hit shapes for O(1) lookup
//
// The virtual method design below will be needed when we implement:
// - Custom block behaviors (events: onPlace, onBreak, onTick)
// - Per-block custom meshes
// - Scripted block types
//
class BlockType {
public:
    virtual ~BlockType() = default;

    // Identity
    virtual std::string_view name() const = 0;
    BlockTypeId id() const { return id_; }  // Set by registry on registration

    // Properties
    virtual bool isSolid() const { return true; }
    virtual bool isTranslucent() const { return false; }
    virtual bool hasCollision() const { return true; }
    virtual float hardness() const { return 1.0f; }

    // Mesh and rendering
    virtual const Mesh* getDefaultMesh() const = 0;
    virtual TextureId getTextureId(Face face) const = 0;

    // Collision and interaction shapes (see Physics doc for details)
    virtual const CollisionShape* getCollisionShape() const = 0;  // Physics
    virtual const CollisionShape* getHitBox() const {             // Raycasting
        return getCollisionShape();  // Default: same as collision
    }
    virtual bool preventsPlacement() const { return hasCollision(); }

    // Per-face solidity for rotation-aware culling
    virtual bool isFaceSolid(Face localFace) const { return isSolid(); }

    // Events (called by world)
    virtual void onPlace(Block& block) {}
    virtual void onBreak(Block& block) {}
    virtual void onNeighborChange(Block& block, Face face) {}
    virtual void onTick(Block& block) {}
    virtual void onInteract(Block& block, Entity& entity) {}

    // Custom mesh support (for blocks with per-instance meshes)
    virtual bool hasCustomMesh() const { return false; }
    virtual Mesh* createCustomMesh(const Block& block) { return nullptr; }

    // Text rendering support (for signs, labels, etc.)
    virtual bool hasTextRendering() const { return false; }
    virtual TextRenderConfig getTextConfig() const { return {}; }

private:
    friend class BlockRegistry;
    BlockTypeId id_;  // Set by registry
};

// Configuration for block text rendering
struct TextRenderConfig {
    Face face = Face::PosZ;           // Which face the text appears on
    glm::vec2 offset{0.0f, 0.0f};     // Offset from face center
    glm::vec2 size{0.8f, 0.4f};       // Size of text area (in block units)
    float scale = 0.05f;              // Text scale
    glm::vec4 color{0, 0, 0, 1};      // Text color (RGBA)
    bool centered = true;             // Center text in area
    int maxLines = 4;                 // Maximum number of lines
    std::string fontName = "default"; // Font to use
};

// Optional sub-block displacement for off-grid block placement
// Blocks with displacement are rendered offset from their grid position.
// Values are clamped to [-1.0, 1.0] per axis (one block unit in each direction).
//
// Face elision/culling rules for displaced blocks:
// - Full faces can only be elided against neighbors with IDENTICAL displacement
// - Blocks with different displacements (including zero vs non-zero) always render all faces
// - This produces correct hollow-shell meshing for aligned displaced blocks
//   while preventing visual artifacts from misaligned blocks
//
// **Implementation Status:** NOT YET IMPLEMENTED
// This struct is designed but not yet integrated into SubChunk storage or MeshBuilder.
// When implemented:
// - SubChunk will have sparse storage: std::unordered_map<uint16_t, BlockDisplacement>
// - MeshBuilder::canElideFaceWithNeighbor() will check displacement matching
// - Block wrapper class will provide getDisplacement()/setDisplacement()
struct BlockDisplacement {
    float dx = 0.0f;  // X-axis displacement [-1.0, 1.0]
    float dy = 0.0f;  // Y-axis displacement [-1.0, 1.0]
    float dz = 0.0f;  // Z-axis displacement [-1.0, 1.0]

    bool isZero() const {
        return dx == 0.0f && dy == 0.0f && dz == 0.0f;
    }

    bool operator==(const BlockDisplacement& other) const {
        return dx == other.dx && dy == other.dy && dz == other.dz;
    }

    // Clamp values to valid range
    void clamp() {
        dx = std::clamp(dx, -1.0f, 1.0f);
        dy = std::clamp(dy, -1.0f, 1.0f);
        dz = std::clamp(dz, -1.0f, 1.0f);
    }
};

// Global block type registry
class BlockRegistry {
public:
    static BlockRegistry& instance();

    // Register a block type (takes ownership)
    // Automatically interns name and assigns ID
    void registerType(std::unique_ptr<BlockType> type);

    // Lookup by ID (fast, O(1))
    BlockType* getType(BlockTypeId id) const;

    // Lookup by name (interns string, then O(1))
    BlockType* getType(std::string_view name) const;

    // Get ID for name (interns if needed)
    BlockTypeId getId(std::string_view name) const;

    // Special types
    BlockType* air() const { return airType_; }

private:
    std::unordered_map<InternedId, std::unique_ptr<BlockType>> types_;
    BlockType* airType_ = nullptr;  // Cached for fast access
};

}  // namespace finevox
```

---

## 4.6 SubChunk Structure

```cpp
namespace finevox {

constexpr int SUBCHUNK_SIZE = 16;
constexpr int SUBCHUNK_VOLUME = SUBCHUNK_SIZE * SUBCHUNK_SIZE * SUBCHUNK_SIZE;  // 4096

class SubChunk : public std::enable_shared_from_this<SubChunk> {
public:
    explicit SubChunk(ChunkPos pos, ChunkColumn* parent);
    ~SubChunk();

    // Position
    ChunkPos pos() const { return pos_; }
    BlockPos cornerBlockPos() const;

    // Parent column (raw pointer - column outlives subchunks)
    ChunkColumn* column() const { return parent_; }

    // Block access using palette
    BlockTypeId getBlockType(int localIndex) const;
    BlockTypeId getBlockType(int x, int y, int z) const;
    void setBlockType(int localIndex, BlockTypeId type);

    uint8_t getRotation(int localIndex) const;
    void setRotation(int localIndex, uint8_t rot);

    // Sub-block displacement (sparse, only for off-grid blocks)
    BlockDisplacement getDisplacement(int localIndex) const;
    void setDisplacement(int localIndex, BlockDisplacement disp);
    bool hasDisplacement(int localIndex) const;
    void clearDisplacement(int localIndex);

    // Get Block descriptor (holds shared_ptr to this)
    Block getBlock(int localIndex);
    Block getBlock(int x, int y, int z);

    // Data containers (sparse, only for blocks that need custom data)
    DataContainer* getBlockData(int localIndex);
    DataContainer* getOrCreateBlockData(int localIndex);
    void removeBlockData(int localIndex);

    // Custom meshes (sparse, only for blocks with per-instance meshes)
    const Mesh* getCustomMesh(int localIndex) const;
    void setCustomMesh(int localIndex, std::unique_ptr<Mesh> mesh);

    // Rendering
    SubChunkView* getView();  // Lazy creation
    void markDirty();         // Schedule mesh rebuild
    bool isDirty() const;

    // Palette access (for serialization/debugging)
    const SubChunkPalette& palette() const { return palette_; }

    // Serialization
    void serialize(std::ostream& out) const;
    static std::shared_ptr<SubChunk> deserialize(std::istream& in, ChunkColumn* parent);

private:
    ChunkPos pos_;
    ChunkColumn* parent_;  // Raw pointer - parent outlives us

    // Block type palette (local index <-> global BlockTypeId)
    SubChunkPalette palette_;

    // Block storage (4096 entries, using local palette indices)
    // Actual bits per entry depends on palette size
    std::vector<uint16_t> blockTypes_;  // Could be optimized to variable bit-width
    std::array<uint8_t, SUBCHUNK_VOLUME> rotations_{};

    // Sparse storage for blocks with custom data/properties
    std::unordered_map<uint16_t, std::unique_ptr<DataContainer>> blockData_;
    std::unordered_map<uint16_t, std::unique_ptr<Mesh>> customMeshes_;
    std::unordered_map<uint16_t, BlockDisplacement> displacements_;  // Off-grid blocks

    // View (created lazily, may be null for server-side chunks)
    std::unique_ptr<SubChunkView> view_;
    std::atomic<bool> dirty_{true};
};

}  // namespace finevox
```

---

## 4.7 Rotation System

The 24 possible rotations of a cube form the rotation group of the cube. We use a lookup-table approach:

```cpp
namespace finevox {

// 24 rotations encoded as 0-23
// Rotation 0 = identity (no rotation)
// Each rotation can be decomposed into face-up (6 choices) x rotation-around-up (4 choices)
// But not all 24 are unique this way, so we use a lookup table

class Rotation {
public:
    static constexpr int COUNT = 24;

    // Get rotation index from face-up and around-up rotation
    static uint8_t fromFaceAndSpin(Face faceUp, int spin);

    // Transform a face through this rotation
    static Face transformFace(uint8_t rotation, Face face);

    // Inverse transform: given a world-space face direction, which local face is it?
    // This is critical for correct face culling with rotated blocks
    static Face inverseTransformFace(uint8_t rotation, Face worldFace);

    // Transform a local position (0-15) within a block
    static glm::ivec3 transformLocal(uint8_t rotation, glm::ivec3 local);

    // Compose rotations
    static uint8_t compose(uint8_t first, uint8_t second);
    static uint8_t inverse(uint8_t rotation);

    // Get rotation matrix for rendering
    static glm::mat3 toMatrix(uint8_t rotation);

private:
    static const std::array<std::array<Face, 6>, 24> faceTransformTable_;
    static const std::array<std::array<Face, 6>, 24> inverseFaceTransformTable_;
    static const std::array<glm::mat3, 24> rotationMatrices_;
};

}  // namespace finevox
```

### Rotation-Aware Face Culling (Fix for VoxelGame2 Bug)

The bug in VoxelGame2 was that face culling compared the block's *local* faces against neighbors without accounting for rotation. For example, if a block is rotated 90 deg around Y, its local "PosX" face now points in the world's "PosZ" direction.

The fix requires two considerations:

1. **When checking if a face should be rendered:** Transform the local face to world space, then check the neighbor in that world direction.

2. **When a neighbor has a rotation:** The neighbor's face pointing back at us is also rotated, so we need to check if *that* face (after inverse transform) is solid.

```cpp
bool MeshBuilder::shouldRenderFace(Block& block, Face localFace, World& world) {
    uint8_t rotation = block.rotation();

    // Transform local face to world direction
    Face worldFace = Rotation::transformFace(rotation, localFace);

    // Get neighbor in that world direction
    Block neighbor = block.neighbor(worldFace);
    if (!neighbor.isValid() || neighbor.isAir()) return true;
    if (!neighbor.type()->isSolid()) return true;

    // Check if neighbor's face pointing back at us is solid
    // The neighbor's local face that points at us is the inverse transform
    // of the opposite world face
    Face oppositeWorldFace = opposite(worldFace);
    Face neighborLocalFace = Rotation::inverseTransformFace(
        neighbor.rotation(), oppositeWorldFace);

    // Check if that face of the neighbor's mesh is solid (fully occludes)
    return !neighbor.type()->isFaceSolid(neighborLocalFace);
}
```

This ensures that:
- A rotated stair block correctly shows faces that aren't occluded
- Two adjacent rotated blocks correctly cull only truly hidden faces
- Non-full blocks (slabs, stairs) work correctly regardless of rotation

---

## 4.8 Data Container System

For blocks that need arbitrary metadata (signs with text, chests with inventory, redstone state):

### Design Principles

**In-memory storage** uses native C++ types for fast access:
- Keys are interned (uint32_t) for compact storage and O(1) lookup
- Values stored in a type-safe variant
- Optimized for frequent reads, occasional writes

**Serialization** uses CBOR for disk storage:
- Self-describing format (no schema needed to parse)
- Compact binary encoding
- String keys restored from interner during serialization

```cpp
namespace finevox {

// Key interning for DataContainer
// Uses the same StringInterner as BlockTypeId for simplicity
// Keys like "text", "inventory", "powered" get interned once
using DataKey = uint32_t;

inline DataKey internKey(std::string_view key) {
    return StringInterner::instance().intern(key);
}

inline std::string_view lookupKey(DataKey key) {
    return StringInterner::instance().lookup(key);
}

// Type-safe variant for data items
// Covers common block metadata needs:
// - Integers: redstone power levels, counters, IDs
// - Floats: progress bars, rotations
// - Strings: sign text, names
// - Bytes: binary blobs, custom formats
// - Nested: compound structures (chest inventory = list of item containers)
// - Arrays: lists of items, multiple text lines
using DataValue = std::variant<
    std::monostate,                    // null/empty
    int64_t,                           // all integers stored as int64
    double,                            // all floats stored as double
    std::string,                       // text data
    std::vector<uint8_t>,              // binary blob
    std::unique_ptr<DataContainer>,    // nested compound
    std::vector<int64_t>,              // integer array
    std::vector<double>,               // float array
    std::vector<std::string>           // string array
>;

class DataContainer {
public:
    DataContainer() = default;
    ~DataContainer() = default;

    // Move-only (contains unique_ptr in variant)
    DataContainer(DataContainer&&) = default;
    DataContainer& operator=(DataContainer&&) = default;
    DataContainer(const DataContainer&) = delete;
    DataContainer& operator=(const DataContainer&) = delete;

    // Deep copy
    [[nodiscard]] std::unique_ptr<DataContainer> clone() const;

    // Access by interned key (fast path)
    template<typename T>
    T get(DataKey key, T defaultValue = T{}) const;

    template<typename T>
    void set(DataKey key, T value);

    bool has(DataKey key) const;
    void remove(DataKey key);

    // Access by string (interns automatically)
    template<typename T>
    T get(std::string_view key, T defaultValue = T{}) const {
        return get<T>(internKey(key), defaultValue);
    }

    template<typename T>
    void set(std::string_view key, T value) {
        set<T>(internKey(key), std::move(value));
    }

    bool has(std::string_view key) const { return has(internKey(key)); }
    void remove(std::string_view key) { remove(internKey(key)); }

    // Iteration
    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }

    // Iterate with callback: void(DataKey key, const DataValue& value)
    template<typename Func>
    void forEach(Func&& func) const {
        for (const auto& [key, value] : data_) {
            func(key, value);
        }
    }

    // Serialization (uses CBOR - see persistence doc)
    // Keys are serialized as strings (looked up from interner)
    [[nodiscard]] std::vector<uint8_t> toCBOR() const;
    static std::unique_ptr<DataContainer> fromCBOR(std::span<const uint8_t> data);

private:
    std::unordered_map<DataKey, DataValue> data_;
};

}  // namespace finevox
```

### Memory Layout

With interned keys, a DataContainer with 5 entries uses approximately:
- Map overhead: ~64 bytes (unordered_map bookkeeping)
- Per entry: 4 bytes (key) + 40 bytes (variant) + ~16 bytes (bucket) = ~60 bytes
- Total: ~64 + 5*60 = ~364 bytes

Compare to string keys (average 10 chars):
- Per entry: 32 bytes (std::string) + 40 bytes (variant) + ~24 bytes (bucket) = ~96 bytes
- Total: ~64 + 5*96 = ~544 bytes

**~33% memory savings** with interned keys, plus faster lookups.

---

[Next: World Management](05-world-management.md)
