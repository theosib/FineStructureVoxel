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

**Recommended library:** [tinycbor](https://github.com/intel/tinycbor) or [QCBOR](https://github.com/laurencelundblade/QCBOR)

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

## 11.2 Region File Format

Inspired by Minecraft's region files, storing full columns:

```cpp
namespace finevox {

// Region = 32x32 column area (1024 columns per region file)
constexpr int REGION_SIZE = 32;
constexpr int COLUMNS_PER_REGION = REGION_SIZE * REGION_SIZE;

class RegionFile {
public:
    RegionFile(const std::filesystem::path& path);
    ~RegionFile();

    // Save/load column (full height)
    void saveColumn(const ChunkColumn& column);
    std::unique_ptr<ChunkColumn> loadColumn(int localX, int localZ);

    // Check if column exists
    bool hasColumn(int localX, int localZ) const;

    // Flush to disk
    void flush();

private:
    std::filesystem::path path_;
    std::fstream file_;

    // Header: offset table (4 bytes per column = 4KB header)
    std::array<uint32_t, COLUMNS_PER_REGION> offsets_;
    std::array<uint32_t, COLUMNS_PER_REGION> sizes_;

    void readHeader();
    void writeHeader();
};

// File layout:
// [0x0000 - 0x0FFF] Header: 1024 x 4-byte offset entries
// [0x1000 - 0x1FFF] Header: 1024 x 4-byte size entries
// [0x2000 - ...]    Column data (variable size, 4KB aligned sectors)

}  // namespace finevox
```

---

## 11.3 Column Serialization

```cpp
namespace finevox {

class ColumnSerializer {
public:
    // Serialize column to binary (CBOR + LZ4 compression)
    static std::vector<uint8_t> serialize(const ChunkColumn& column);

    // Deserialize column from binary
    static std::unique_ptr<ChunkColumn> deserialize(std::span<const uint8_t> data);

private:
    static std::vector<uint8_t> compress(std::span<const uint8_t> data);
    static std::vector<uint8_t> decompress(std::span<const uint8_t> data);
};

// Column binary format:
// [4 bytes] Magic number (0x56585843 = "VXCL")
// [4 bytes] Version
// [4 bytes] Uncompressed size
// [4 bytes] Compressed size
// [N bytes] LZ4-compressed CBOR data containing:
//   {
//     "subchunks": [
//       {
//         "y": -4,
//         "palette": [<block_type_id>, ...],
//         "blocks": <binary, bit-packed indices>,
//         "rotations": <binary, run-length encoded uint8>,
//         "data": { <index>: <CBOR DataContainer>, ... },
//         "displacements": { <index>: [dx, dy, dz], ... }
//       },
//       ...
//     ]
//   }

}  // namespace finevox
```

---

## 11.4 Block Data Bit-Packing

For serialization, block type indices are bit-packed based on palette size:

| Palette Size | Bits per Block | Storage |
|--------------|----------------|---------|
| 1            | 0              | Nothing stored (all same type) |
| 2-16         | 4              | 16 per 64-bit word |
| 17-256       | 8              | 8 per 64-bit word |
| 257-4096     | 12             | 5 per 64-bit word |
| 4097+        | 16             | 4 per 64-bit word |

**Word-aligned packing (no straddling):**

Indices are packed only within 64-bit word boundaries. Remaining bits in each word are zero-padded. This choice is intentional:

- **Simpler code:** No cross-word boundary detection or multi-word masking
- **Better compression:** Word-straddling creates high-entropy bit patterns that defeat LZ4/zlib
- **Minimal overhead:** Few wasted bits per word (e.g., 4 bits per word for 5-bit indices)
- **Historical lesson:** Minecraft's word-straddling implementation was a performance bottleneck without compression benefit

```cpp
// Pack 4096 indices into 64-bit words (word-aligned, no straddling)
std::vector<uint64_t> packBlocks(const uint16_t* indices, int bitsPerIndex) {
    int indicesPerWord = 64 / bitsPerIndex;
    int wordCount = (4096 + indicesPerWord - 1) / indicesPerWord;
    std::vector<uint64_t> words(wordCount, 0);

    for (int i = 0; i < 4096; ++i) {
        int word = i / indicesPerWord;
        int slot = i % indicesPerWord;
        words[word] |= static_cast<uint64_t>(indices[i]) << (slot * bitsPerIndex);
    }
    return words;
}
```

---

[Next: Scripting and Command Language](12-scripting.md)
