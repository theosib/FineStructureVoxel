# 11. Persistence and Serialization

[Back to Index](INDEX.md) | [Previous: Input and Player Control](10-input.md)

---

## 11.1 Data Format: CBOR

Instead of inventing a custom NBT-like format, we use **CBOR (Concise Binary Object Representation)**, an IETF standard (RFC 8949). CBOR provides:

- **Self-describing format** with named fields (like NBT, unlike Protobuf)
- **Compact binary encoding** (more efficient than JSON, similar to MessagePack)
- **IETF standardized** with good C++ library support
- **Schema-optional** - can parse without prior schema knowledge

**Why not Protobuf?** Protobuf uses numeric field tags, not names. The serialized data isn't self-describing - you need the .proto schema to interpret it. For game save data that may evolve over time and need to be debuggable, self-describing formats are preferable.

**Why not MessagePack?** CBOR is very similar to MessagePack but is IETF standardized and has slightly cleaner semantics around bytes vs strings.

**Implementation:** Custom CBOR encoder/decoder in `data_container.cpp` (no external library dependency).

### In-Memory vs Serialization

**In-memory storage** (DataContainer) uses native C++ types:
- Interned uint32_t keys for fast lookup
- std::variant for type-safe values
- Optimized for frequent access during gameplay

**Serialization** uses CBOR:
- Keys serialized as strings (human-readable in hex dumps)
- Loaded back into DataContainer with key re-interning
- CBOR is NOT used for in-memory storage (would require parsing on every access)

```cpp
// Serialization flow:
DataContainer container;
container.set("text", "Hello World");     // In-memory: interned key + variant

std::vector<uint8_t> bytes = container.toCBOR();
// CBOR bytes: { "text": "Hello World" }  // String keys for portability

auto loaded = DataContainer::fromCBOR(bytes);
// Re-interns "text" key, stores in native format
```

See [04-core-data-structures.md](04-core-data-structures.md) Section 4.8 for DataContainer implementation details.

---

## 11.2 SubChunk Serialization

SubChunks are serialized as CBOR maps with the following structure:

```cpp
// SubChunk CBOR format:
{
  "y": <int>,                              // Y-level of subchunk (in chunk coordinates)
  "palette": ["air", "stone", "dirt", ...], // Block type names (index = local ID)
  "blocks": <byte string>,                  // 8-bit or 16-bit indices (4096 values)
  "rotations": <byte string>,               // 1 byte per block (optional, omit if all zero)
  "data": {                                 // Per-block extra data (sparse map)
    <index>: { ... DataContainer ... },
    ...
  }
}
```

### Block Storage Simplifications

**Palette**: Array of block type name strings. Index in array = local block ID. Air is always index 0.

**Block indices**:
- 8-bit (1 byte each) if palette size ≤ 256
- 16-bit (2 bytes each, little-endian) if palette size > 256
- Let zlib/LZ4 handle compression - no custom bit-packing needed

**Rotations**: 1 byte per block. Value 0 = default rotation. Since most blocks use default rotation, this compresses extremely well with zlib. Omit entirely if all blocks have rotation 0.

**Per-block data**: Sparse map keyed by block index (0-4095). Includes:
- Tile entity data (furnaces, chests, signs)
- Block displacements (rare - stored as `"displacement": [dx, dy, dz]`)
- Any other block-specific metadata

Most subchunks have empty data maps.

---

## 11.3 ChunkColumn Serialization

```cpp
// ChunkColumn CBOR format:
{
  "x": <int>,                    // Column X coordinate
  "z": <int>,                    // Column Z coordinate
  "subchunks": [ ... ],          // Array of serialized subchunks
  "heightmap": <byte string>,    // 256 int16 values (16x16) for lighting
  "biomes": <byte string>,       // Biome data (format TBD)
  "data": { ... }                // Column-level extra data
}
```

Only non-empty subchunks are stored in the array.

---

## 11.4 Region File Format

Region files use a **journal-style table of contents** for crash safety and efficient updates.

### File Structure

```
worlds/<world>/regions/
  r.0.0.dat       # Region data file (chunk data, append-mostly)
  r.0.0.toc       # Table of contents (journal-style)
  r.0.-1.dat
  r.0.-1.toc
  ...
```

Region coordinates: `r.{rx}.{rz}` where `rx = floor(columnX / 32)`, `rz = floor(columnZ / 32)`.

### Data File (.dat)

Chunks are written sequentially. Each chunk entry:

```
[4 bytes] Magic (0x56584348 = "VXCH")
[4 bytes] Compressed size
[N bytes] LZ4-compressed CBOR (ChunkColumn)
```

Chunks may be rewritten in-place if new size fits, or appended if not.

### Table of Contents (.toc)

Journal-style append-only log of chunk locations:

```
[4 bytes] Magic (0x56585443 = "VXTC")
[4 bytes] Version
[Entries...]
  [2 bytes] Local X (0-31)
  [2 bytes] Local Z (0-31)
  [8 bytes] Offset in .dat file
  [4 bytes] Compressed size
  [8 bytes] Timestamp (for conflict resolution)
```

**Journal semantics:**
- New entries appended when chunks are saved
- Latest entry for each (x,z) is authoritative
- Periodic compaction removes obsolete entries
- On load, scan ToC and build in-memory index

### Free Space Management

Track free spans from deleted/overwritten chunks:

```cpp
struct FreeSpan {
    uint64_t offset;
    uint64_t size;
};

// In-memory while region is open
std::set<FreeSpan, BestFitComparator> freeSpans_;
```

On write:
1. Compress chunk, determine size
2. Find best-fit free span (or append to end)
3. Write chunk data
4. Append ToC entry
5. Update free span tracking

### Benefits Over Fixed Sectors

- Variable-size chunks without wasted space
- No fragmentation from size changes
- Crash-safe (ToC is append-only, old data preserved until compaction)
- Simple recovery: scan ToC, use latest entries

---

## 11.5 Compression

**Primary compression:** LZ4 for chunk data
- Very fast decompression (important for chunk loading)
- Good compression ratio for voxel data
- Block data with many zeros/repeated values compresses well

**Why not zlib?** LZ4 decompresses ~10x faster. Compression ratio difference is small for typical chunk data. Load speed matters more than file size.

**Optional:** zstd for archival/network (better ratio than LZ4, slower)

---

## 11.6 Resource Locator

The **ResourceLocator** provides a unified path resolution service for all engine resources. It understands the hierarchy of scopes and maps logical paths to physical filesystem paths.

### Scope Hierarchy

```
engine/              - Engine defaults (shipped with library)
game/                - Game assets (textures, shaders, etc.) - root provided by game
user/                - User-level settings (~/.config/finevox or platform equivalent)
  config.cbor        - Global ConfigManager data
world/<name>/        - Per-world data
  world.cbor         - WorldConfig metadata
  regions/           - Region files for overworld
  dim/<name>/        - Named dimensions within world
    regions/         - Region files for this dimension
```

### Design Principles

1. **Separation of concerns**: ResourceLocator knows *where* things live, not *what* they are
2. **Scope registration**: Roots are registered dynamically, not hardcoded
3. **Logical paths**: Consumers use logical paths like `"user/config.cbor"`, not filesystem paths
4. **Platform abstraction**: Handles platform-specific user directories internally

### API Overview

```cpp
class ResourceLocator {
public:
    static ResourceLocator& instance();

    // Set root paths for built-in scopes
    void setEngineRoot(const std::filesystem::path& path);
    void setGameRoot(const std::filesystem::path& path);
    void setUserRoot(const std::filesystem::path& path);

    // World/dimension management
    void registerWorld(const std::string& name, const std::filesystem::path& path);
    void unregisterWorld(const std::string& name);
    void registerDimension(const std::string& world, const std::string& dim,
                          const std::string& subpath = "");

    // Path resolution
    std::filesystem::path resolve(const std::string& logicalPath) const;
    bool exists(const std::string& logicalPath) const;

    // Convenience methods
    std::filesystem::path worldPath(const std::string& name) const;
    std::filesystem::path dimensionPath(const std::string& world, const std::string& dim) const;
    std::filesystem::path regionPath(const std::string& world, const std::string& dim = "overworld") const;
};
```

### Usage Examples

```cpp
// Game initialization
ResourceLocator::instance().setEngineRoot("/usr/share/finevox");
ResourceLocator::instance().setGameRoot("/path/to/game/assets");
ResourceLocator::instance().setUserRoot("~/.config/finevox");  // Expands ~ automatically

// World management
ResourceLocator::instance().registerWorld("MyWorld", "/path/to/saves/MyWorld");
ResourceLocator::instance().registerDimension("MyWorld", "nether");
ResourceLocator::instance().registerDimension("MyWorld", "the_end");

// Path resolution
auto configPath = ResourceLocator::instance().resolve("user/config.cbor");
// → /home/user/.config/finevox/config.cbor

auto worldCfg = ResourceLocator::instance().resolve("world/MyWorld/world.cbor");
// → /path/to/saves/MyWorld/world.cbor

auto regions = ResourceLocator::instance().regionPath("MyWorld", "nether");
// → /path/to/saves/MyWorld/dim/nether/regions
```

### Integration Points

- **ConfigManager**: Uses `resolve("user/config.cbor")` instead of hardcoded paths
- **WorldConfig**: Uses `resolve("world/<name>/world.cbor")`
- **IOManager**: Uses `regionPath(world, dimension)` for region file directories
- **Game assets**: Game layer sets game root, engine uses `resolve("game/textures/...")`

---

## 11.7 Save/Load Threading

```cpp
class WorldPersistence {
public:
    // Queue column for async save
    void queueSave(ColumnPos pos, std::unique_ptr<ChunkColumn> column);

    // Request async load (returns future)
    std::future<std::unique_ptr<ChunkColumn>> queueLoad(ColumnPos pos);

    // Process pending I/O (call from I/O thread)
    void processSaveQueue();
    void processLoadQueue();

private:
    // One region file cache per open region
    LRUCache<RegionPos, RegionFile> regionCache_;

    // Thread-safe queues
    CoalescingQueueTS<ColumnPos, std::unique_ptr<ChunkColumn>> saveQueue_;
    CoalescingQueueTS<ColumnPos, std::promise<...>> loadQueue_;
};
```

---

[Next: Scripting and Command Language](12-scripting.md)
