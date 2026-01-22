# 21. Clipboard and Schematic System

[Back to Index](INDEX.md) | [Previous: Large World Coordinates](20-large-world-coordinates.md)

---

## 21.1 Overview

The clipboard/schematic system provides infrastructure for world editing operations:
- **Copy/Paste**: Extract blocks from one location, place them elsewhere
- **Cut/Paste**: Same as copy, but removes original blocks
- **Schematics**: Save block structures to files for later use or sharing

This system operates at the **block data level**, preserving:
- Block type (by name, for portability)
- Rotation/orientation
- Displacement offset
- Per-block extra data (tile entity data, custom properties)

---

## 21.2 Design Goals

1. **Portability**: Schematics should work across game versions and mods
2. **Efficiency**: In-memory operations should be fast for real-time editing
3. **Completeness**: Preserve all block information, not just type
4. **Two Formats**: Optimized in-memory format + portable serialized format

---

## 21.3 Block Snapshot Structure

A **BlockSnapshot** captures complete block state for clipboard operations:

```cpp
namespace finevox {

/// Complete snapshot of a block's state (portable format)
struct BlockSnapshot {
    std::string typeName;           // Block type name (e.g., "blockgame:oak_stairs")
    Rotation rotation;              // 24-state rotation
    glm::vec3 displacement{0.0f};   // Sub-block offset (usually zero)
    std::optional<DataContainer> extraData;  // Tile entity data, custom properties

    // Convenience constructors
    BlockSnapshot() = default;
    explicit BlockSnapshot(std::string_view type) : typeName(type) {}

    // Check if this is an air block
    bool isAir() const { return typeName.empty() || typeName == "air"; }

    // Check if block has any non-default properties
    bool hasMetadata() const {
        return rotation != Rotation::IDENTITY ||
               displacement != glm::vec3(0.0f) ||
               extraData.has_value();
    }
};

}  // namespace finevox
```

**Why store type name as string?**
- Schematics may be loaded in different game sessions where BlockTypeId values differ
- String names are stable across mod additions/removals
- BlockTypeId lookup happens once when pasting, not during storage

---

## 21.4 Schematic Structure

A **Schematic** is a 3D region of block snapshots:

```cpp
namespace finevox {

/// 3D region of block snapshots
class Schematic {
public:
    // Create empty schematic with given dimensions
    Schematic(int32_t sizeX, int32_t sizeY, int32_t sizeZ);

    // Dimensions
    int32_t sizeX() const { return sizeX_; }
    int32_t sizeY() const { return sizeY_; }
    int32_t sizeZ() const { return sizeZ_; }
    glm::ivec3 size() const { return {sizeX_, sizeY_, sizeZ_}; }
    int64_t volume() const { return int64_t(sizeX_) * sizeY_ * sizeZ_; }

    // Block access (coordinates relative to schematic origin)
    BlockSnapshot& at(int32_t x, int32_t y, int32_t z);
    const BlockSnapshot& at(int32_t x, int32_t y, int32_t z) const;
    BlockSnapshot& at(glm::ivec3 pos) { return at(pos.x, pos.y, pos.z); }
    const BlockSnapshot& at(glm::ivec3 pos) const { return at(pos.x, pos.y, pos.z); }

    // Bounds checking
    bool contains(int32_t x, int32_t y, int32_t z) const;
    bool contains(glm::ivec3 pos) const { return contains(pos.x, pos.y, pos.z); }

    // Iteration (for all non-air blocks)
    template<typename Func>
    void forEachBlock(Func&& func) const;

    // Statistics
    size_t nonAirBlockCount() const;
    std::unordered_set<std::string> uniqueBlockTypes() const;

    // Metadata
    void setName(std::string_view name) { name_ = name; }
    std::string_view name() const { return name_; }
    void setAuthor(std::string_view author) { author_ = author; }
    std::string_view author() const { return author_; }

private:
    int32_t sizeX_, sizeY_, sizeZ_;
    std::vector<BlockSnapshot> blocks_;  // Stored in YZX order for cache-friendly vertical iteration
    std::string name_;
    std::string author_;

    size_t index(int32_t x, int32_t y, int32_t z) const {
        return y + sizeY_ * (z + sizeZ_ * x);
    }
};

}  // namespace finevox
```

### Storage Order

Blocks are stored in **YZX order** (Y innermost) because:
- Vertical slices are common (floor plans, wall sections)
- Pasting often iterates bottom-to-top
- Matches SubChunk internal storage for efficient extraction

---

## 21.5 Extraction and Placement

### Extracting from World

```cpp
namespace finevox {

/// Extract a region from the world into a schematic
Schematic extractRegion(
    World& world,
    BlockPos min,
    BlockPos max
);

/// Extract with block filter (e.g., skip air, only certain types)
Schematic extractRegion(
    World& world,
    BlockPos min,
    BlockPos max,
    std::function<bool(BlockTypeId)> filter
);

}  // namespace finevox
```

**Implementation sketch:**

```cpp
Schematic extractRegion(World& world, BlockPos min, BlockPos max) {
    auto size = max - min + BlockPos(1, 1, 1);
    Schematic result(size.x, size.y, size.z);

    for (int32_t x = min.x; x <= max.x; ++x) {
        for (int32_t z = min.z; z <= max.z; ++z) {
            for (int32_t y = min.y; y <= max.y; ++y) {
                BlockPos worldPos(x, y, z);
                glm::ivec3 localPos = worldPos - min;

                BlockSnapshot& snap = result.at(localPos);

                // Get block type name (not ID - for portability)
                BlockTypeId typeId = world.getBlock(worldPos);
                snap.typeName = StringInterner::instance().lookup(typeId.id);

                // Get rotation if available
                snap.rotation = world.getBlockRotation(worldPos);

                // Get displacement if available
                snap.displacement = world.getBlockDisplacement(worldPos);

                // Get extra data if any
                if (auto* data = world.getBlockData(worldPos)) {
                    snap.extraData = *data;
                }
            }
        }
    }

    return result;
}
```

### Placing into World

```cpp
namespace finevox {

/// Options for schematic placement
struct PlaceOptions {
    bool replaceNonAir = true;      // Replace existing non-air blocks
    bool replaceAir = true;         // Place into air positions
    bool skipUnknownTypes = true;   // Skip blocks with unrecognized type names
    Rotation rotation = Rotation::IDENTITY;  // Rotate entire schematic
    bool mirror = false;            // Mirror horizontally (X axis)
};

/// Place a schematic into the world using BatchBuilder
/// Returns number of blocks placed
int32_t placeSchematic(
    World& world,
    const Schematic& schematic,
    BlockPos origin,
    const PlaceOptions& options = {}
);

/// Place with custom block transformer (e.g., for block substitution)
int32_t placeSchematic(
    World& world,
    const Schematic& schematic,
    BlockPos origin,
    const PlaceOptions& options,
    std::function<BlockSnapshot(const BlockSnapshot&)> transformer
);

}  // namespace finevox
```

**Implementation uses BatchBuilder for efficiency:**

```cpp
int32_t placeSchematic(World& world, const Schematic& schematic,
                       BlockPos origin, const PlaceOptions& options) {
    BatchBuilder batch(world);
    int32_t placed = 0;

    schematic.forEachBlock([&](glm::ivec3 localPos, const BlockSnapshot& snap) {
        if (snap.isAir() && !options.replaceAir) return;

        // Apply rotation/mirror to position
        glm::ivec3 transformedPos = transformPosition(localPos, schematic.size(),
                                                       options.rotation, options.mirror);
        BlockPos worldPos = origin + BlockPos(transformedPos.x, transformedPos.y, transformedPos.z);

        // Check if we should replace existing block
        if (!options.replaceNonAir && world.getBlock(worldPos) != AIR_BLOCK_TYPE) {
            return;
        }

        // Resolve block type name to ID
        auto typeIdOpt = StringInterner::instance().find(snap.typeName);
        if (!typeIdOpt && options.skipUnknownTypes) return;
        BlockTypeId typeId = typeIdOpt ? BlockTypeId(*typeIdOpt)
                                       : BlockTypeId::fromName(snap.typeName);

        // Apply schematic rotation to block rotation
        Rotation finalRotation = snap.rotation.compose(options.rotation);

        // Queue the block change
        batch.setBlock(worldPos, typeId.id, finalRotation.index());

        if (snap.displacement != glm::vec3(0.0f)) {
            // Transform and set displacement
            glm::vec3 transformedDisp = transformDisplacement(snap.displacement,
                                                               options.rotation, options.mirror);
            batch.setBlockDisplacement(worldPos, transformedDisp);
        }

        if (snap.extraData) {
            batch.setBlockData(worldPos, *snap.extraData);
        }

        ++placed;
    });

    batch.execute();
    return placed;
}
```

---

## 21.6 Serialization Format

Schematics serialize to CBOR for file storage:

```cpp
// Schematic CBOR format:
{
    "version": 1,                        // Format version
    "name": "My Structure",              // Optional name
    "author": "Player",                  // Optional author
    "size": [sizeX, sizeY, sizeZ],       // Dimensions

    "palette": [                         // Block type palette
        "air",
        "blockgame:stone",
        "blockgame:oak_planks",
        ...
    ],

    "blocks": <byte string>,             // Palette indices (8 or 16 bit)

    "metadata": {                        // Sparse metadata for non-default blocks
        <linear_index>: {
            "rotation": <uint8>,         // Rotation index (0-23), omit if 0
            "displacement": [dx, dy, dz], // Sub-block offset, omit if zero
            "data": { ... }              // Extra data, omit if none
        },
        ...
    }
}
```

### Serialization API

```cpp
namespace finevox {

/// Serialize schematic to CBOR bytes
std::vector<uint8_t> serializeSchematic(const Schematic& schematic);

/// Deserialize schematic from CBOR bytes
Schematic deserializeSchematic(std::span<const uint8_t> data);

/// Save schematic to file (with LZ4 compression)
void saveSchematic(const Schematic& schematic, const std::filesystem::path& path);

/// Load schematic from file
Schematic loadSchematic(const std::filesystem::path& path);

}  // namespace finevox
```

### File Format

Schematic files use the same structure as region chunk data:

```
[4 bytes] Magic (0x56584348 = "VXSC" for "VoXel SChematic")
[4 bytes] Compressed size
[N bytes] LZ4-compressed CBOR data
```

---

## 21.7 Clipboard Manager

For runtime copy/paste operations, a **ClipboardManager** provides session-scoped storage:

```cpp
namespace finevox {

/// Session clipboard for copy/paste operations
class ClipboardManager {
public:
    static ClipboardManager& instance();

    // Primary clipboard
    void setClipboard(Schematic schematic);
    const Schematic* clipboard() const;  // nullptr if empty
    void clearClipboard();

    // Named clipboards (for advanced editing)
    void setNamed(std::string_view name, Schematic schematic);
    const Schematic* getNamed(std::string_view name) const;
    void clearNamed(std::string_view name);
    void clearAll();

    // Clipboard history (optional, for undo support)
    void pushHistory(Schematic schematic);
    const Schematic* historyAt(size_t index) const;
    size_t historySize() const;
    void clearHistory();
    void setMaxHistorySize(size_t max);

private:
    ClipboardManager();

    std::unique_ptr<Schematic> clipboard_;
    std::unordered_map<std::string, Schematic> namedClipboards_;
    std::deque<Schematic> history_;
    size_t maxHistorySize_ = 10;
};

}  // namespace finevox
```

---

## 21.8 Transformation Utilities

```cpp
namespace finevox {

/// Rotate schematic by 90-degree increments
Schematic rotateSchematic(const Schematic& schematic, Rotation rotation);

/// Mirror schematic along an axis
Schematic mirrorSchematic(const Schematic& schematic, Axis axis);

/// Flip schematic vertically
Schematic flipSchematic(const Schematic& schematic);

/// Crop schematic to smallest bounding box containing non-air blocks
Schematic cropSchematic(const Schematic& schematic);

/// Replace block types in schematic
Schematic replaceBlocks(
    const Schematic& schematic,
    const std::unordered_map<std::string, std::string>& replacements
);

}  // namespace finevox
```

---

## 21.9 Integration Points

### With BatchBuilder (doc 13)

Schematic placement uses `BatchBuilder` for efficient multi-block operations:
- Single lock acquisition per affected chunk
- Batched mesh invalidation
- Coalesced neighbor updates

### With Persistence (doc 11)

Schematic serialization uses the same infrastructure:
- CBOR encoding (DataContainer compatible)
- LZ4 compression
- ResourceLocator for file paths (`"user/schematics/<name>.vxsc"`)

### With World Management (doc 05)

Extraction uses World's block access APIs:
- `getBlock()` for type
- `getBlockRotation()` for orientation
- `getBlockDisplacement()` for offset
- `getBlockData()` for extra data

### With Block Registry

- Block type names resolved via `StringInterner`
- Unknown block types handled gracefully (skip or substitute)

---

## 21.10 Command Integration (Future)

When the scripting system (doc 12) is implemented:

```
{copy min max}           # Copy region to clipboard
{cut min max}            # Cut region to clipboard
{paste origin}           # Paste clipboard at origin
{paste origin rotated 90}  # Paste with rotation
{save-schematic name}    # Save clipboard to file
{load-schematic name}    # Load file to clipboard
```

---

## 21.11 Implementation Phases

This system should be implemented after Phase 5 (Mesh Optimization) and can be done incrementally:

### Phase A: Core Structures
- [ ] `BlockSnapshot` struct
- [ ] `Schematic` class with basic operations
- [ ] `extractRegion()` function
- [ ] Unit tests for Schematic

### Phase B: Placement
- [ ] `placeSchematic()` with BatchBuilder integration
- [ ] `PlaceOptions` for rotation/mirror
- [ ] Unit tests for round-trip (extract → place)

### Phase C: Serialization
- [ ] CBOR serialization/deserialization
- [ ] LZ4 compressed file format
- [ ] File load/save functions
- [ ] Unit tests for serialization round-trip

### Phase D: Runtime Support
- [ ] `ClipboardManager` singleton
- [ ] Transformation utilities (rotate, mirror, crop)
- [ ] Integration with game UI (future)

---

## 21.12 Compatibility Considerations

### Forward Compatibility

- Version field in CBOR allows format evolution
- Unknown fields are ignored when loading older versions
- Palette approach handles new block types gracefully

### Cross-Game Compatibility

- Block type names should follow namespaced convention (`"game:block"`)
- Schematics from different games can be loaded; unknown blocks become air or substituted
- Metadata is preserved even if not understood (pass-through)

### Size Limits

- Practical limit: ~1 million blocks per schematic (memory)
- Recommended max: 256×256×256 for UI responsiveness
- Larger structures should use multiple schematics or streaming

---

[Next: (Future document)]
