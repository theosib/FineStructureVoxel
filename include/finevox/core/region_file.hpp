#pragma once

/**
 * @file region_file.hpp
 * @brief 32x32 chunk region file I/O
 *
 * Design: [11-persistence.md] §11.4 Region Files
 */

#include "finevox/core/position.hpp"
#include "finevox/core/chunk_column.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <unordered_map>
#include <vector>

namespace finevox {

// Region = 32x32 column area (1024 columns per region)
constexpr int32_t REGION_SIZE = 32;
constexpr int32_t COLUMNS_PER_REGION = REGION_SIZE * REGION_SIZE;

// Chunk data flags (stored in chunk header)
namespace ChunkFlags {
    constexpr uint32_t NONE = 0;
    constexpr uint32_t COMPRESSED_LZ4 = 1 << 0;  // Data is LZ4 compressed
    // Reserved: bits 1-31 for future use
}

// Region position (identifies which region file)
struct RegionPos {
    int32_t rx = 0;
    int32_t rz = 0;

    constexpr bool operator==(const RegionPos&) const = default;

    // Get region position from column position
    [[nodiscard]] static RegionPos fromColumn(ColumnPos col) {
        // Use arithmetic shift for correct negative handling
        return RegionPos{
            col.x >= 0 ? col.x / REGION_SIZE : (col.x - REGION_SIZE + 1) / REGION_SIZE,
            col.z >= 0 ? col.z / REGION_SIZE : (col.z - REGION_SIZE + 1) / REGION_SIZE
        };
    }

    // Get local coordinates within region (0-31)
    [[nodiscard]] static std::pair<int32_t, int32_t> toLocal(ColumnPos col) {
        int32_t lx = col.x % REGION_SIZE;
        int32_t lz = col.z % REGION_SIZE;
        if (lx < 0) lx += REGION_SIZE;
        if (lz < 0) lz += REGION_SIZE;
        return {lx, lz};
    }
};

}  // namespace finevox

// Hash for RegionPos
template<>
struct std::hash<finevox::RegionPos> {
    size_t operator()(const finevox::RegionPos& p) const noexcept {
        return std::hash<int64_t>{}(
            (static_cast<int64_t>(p.rx) << 32) | (static_cast<uint32_t>(p.rz))
        );
    }
};

namespace finevox {

// Entry in the Table of Contents
struct TocEntry {
    int32_t localX = 0;      // 0-31
    int32_t localZ = 0;      // 0-31
    uint64_t offset = 0;     // Offset in .dat file
    uint32_t size = 0;       // Compressed size in bytes
    uint64_t timestamp = 0;  // For conflict resolution (newer wins)

    // Convert to/from bytes for file storage
    [[nodiscard]] std::vector<uint8_t> toBytes() const;
    [[nodiscard]] static std::optional<TocEntry> fromBytes(const uint8_t* data, size_t len);

    static constexpr size_t SERIALIZED_SIZE = 2 + 2 + 8 + 4 + 8;  // 24 bytes
};

// Free span in the data file
struct FreeSpan {
    uint64_t offset = 0;
    uint64_t size = 0;

    bool operator<(const FreeSpan& other) const {
        // Sort by size first (for best-fit), then by offset
        if (size != other.size) return size < other.size;
        return offset < other.offset;
    }
};

// Region file manager - handles one 32x32 region
//
// File structure:
//   r.{rx}.{rz}.dat - Chunk data (append-mostly)
//   r.{rx}.{rz}.toc - Table of contents (journal-style)
//
// The ToC is append-only during normal operation. Each entry records
// where a chunk is stored in the .dat file. Latest entry for each (x,z)
// is authoritative. Periodic compaction removes obsolete entries.
//
class RegionFile {
public:
    // Open or create a region file
    // basePath should be the regions directory (e.g., "world/regions/")
    explicit RegionFile(const std::filesystem::path& basePath, RegionPos pos);
    ~RegionFile();

    // Non-copyable, non-movable (owns file handles)
    RegionFile(const RegionFile&) = delete;
    RegionFile& operator=(const RegionFile&) = delete;
    RegionFile(RegionFile&&) = delete;
    RegionFile& operator=(RegionFile&&) = delete;

    // Save a column (serializes and writes)
    // Returns true on success
    bool saveColumn(const ChunkColumn& column, ColumnPos pos);

    // Save pre-serialized column data (avoids double serialization)
    // The data should be CBOR-encoded column data
    // Returns true on success
    bool saveColumnRaw(ColumnPos pos, std::span<const uint8_t> cborData);

    // Load a column
    // Returns nullptr if column doesn't exist or on error
    [[nodiscard]] std::unique_ptr<ChunkColumn> loadColumn(ColumnPos pos);

    // Check if column exists in this region
    [[nodiscard]] bool hasColumn(ColumnPos pos) const;

    // Get column positions that exist in this region
    [[nodiscard]] std::vector<ColumnPos> getExistingColumns() const;

    // Flush pending writes to disk
    void flush();

    // Compact the ToC file (removes obsolete entries)
    // Call periodically or on close
    void compactToc();

    // Get region position
    [[nodiscard]] RegionPos position() const { return pos_; }

    // Statistics
    [[nodiscard]] size_t columnCount() const { return index_.size(); }
    [[nodiscard]] size_t freeSpaceCount() const { return freeSpans_.size(); }
    [[nodiscard]] uint64_t dataFileSize() const { return dataFileEnd_; }

private:
    RegionPos pos_;
    std::filesystem::path basePath_;
    std::filesystem::path datPath_;
    std::filesystem::path tocPath_;

    std::fstream datFile_;
    std::fstream tocFile_;

    // In-memory index: local coords -> latest ToC entry
    std::unordered_map<uint32_t, TocEntry> index_;

    // Free space tracking (sorted by size for best-fit)
    std::multiset<FreeSpan> freeSpans_;

    // End of data file (for appending)
    uint64_t dataFileEnd_ = 0;

    // Convert local (x,z) to index key
    [[nodiscard]] static uint32_t localKey(int32_t lx, int32_t lz) {
        return static_cast<uint32_t>(lz * REGION_SIZE + lx);
    }

    // Open/create files
    bool openFiles();

    // Load ToC and build index
    bool loadToc();

    // Append entry to ToC file
    bool appendTocEntry(const TocEntry& entry);

    // Write chunk data to dat file at given offset
    // flags: ChunkFlags bitmask (e.g., COMPRESSED_LZ4)
    bool writeChunkData(uint64_t offset, const std::vector<uint8_t>& data, uint32_t flags = 0);

    // Read chunk data from dat file
    // outFlags: receives the flags from the chunk header (can be nullptr)
    [[nodiscard]] std::vector<uint8_t> readChunkData(uint64_t offset, uint32_t size, uint32_t* outFlags = nullptr);

    // Find best-fit free span for given size
    // Returns offset, or nullopt if no suitable span (append instead)
    [[nodiscard]] std::optional<uint64_t> findFreeSpan(uint64_t size);

    // Add a free span (merges with adjacent spans)
    void addFreeSpan(uint64_t offset, uint64_t size);

    // Remove a free span (when allocated)
    void removeFreeSpan(uint64_t offset, uint64_t size);

    // Get current timestamp
    [[nodiscard]] static uint64_t currentTimestamp();
};

// Magic numbers
constexpr uint32_t DAT_CHUNK_MAGIC = 0x56584348;  // "VXCH"
constexpr uint32_t TOC_MAGIC = 0x56585443;        // "VXTC"
constexpr uint32_t TOC_VERSION = 1;

}  // namespace finevox
