#include "finevox/region_file.hpp"
#include "finevox/serialization.hpp"
#include <atomic>
#include <chrono>
#include <cstring>

namespace finevox {

// ============================================================================
// TocEntry serialization
// ============================================================================

std::vector<uint8_t> TocEntry::toBytes() const {
    std::vector<uint8_t> out(SERIALIZED_SIZE);
    size_t pos = 0;

    // Local X (2 bytes, little-endian)
    out[pos++] = static_cast<uint8_t>(localX & 0xFF);
    out[pos++] = static_cast<uint8_t>((localX >> 8) & 0xFF);

    // Local Z (2 bytes, little-endian)
    out[pos++] = static_cast<uint8_t>(localZ & 0xFF);
    out[pos++] = static_cast<uint8_t>((localZ >> 8) & 0xFF);

    // Offset (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        out[pos++] = static_cast<uint8_t>((offset >> (i * 8)) & 0xFF);
    }

    // Size (4 bytes, little-endian)
    for (int i = 0; i < 4; ++i) {
        out[pos++] = static_cast<uint8_t>((size >> (i * 8)) & 0xFF);
    }

    // Timestamp (8 bytes, little-endian)
    for (int i = 0; i < 8; ++i) {
        out[pos++] = static_cast<uint8_t>((timestamp >> (i * 8)) & 0xFF);
    }

    return out;
}

std::optional<TocEntry> TocEntry::fromBytes(const uint8_t* data, size_t len) {
    if (len < SERIALIZED_SIZE) {
        return std::nullopt;
    }

    TocEntry entry;
    size_t pos = 0;

    // Local X
    entry.localX = static_cast<int32_t>(data[pos]) |
                   (static_cast<int32_t>(data[pos + 1]) << 8);
    pos += 2;

    // Local Z
    entry.localZ = static_cast<int32_t>(data[pos]) |
                   (static_cast<int32_t>(data[pos + 1]) << 8);
    pos += 2;

    // Offset
    entry.offset = 0;
    for (int i = 0; i < 8; ++i) {
        entry.offset |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
    }
    pos += 8;

    // Size
    entry.size = 0;
    for (int i = 0; i < 4; ++i) {
        entry.size |= static_cast<uint32_t>(data[pos + i]) << (i * 8);
    }
    pos += 4;

    // Timestamp
    entry.timestamp = 0;
    for (int i = 0; i < 8; ++i) {
        entry.timestamp |= static_cast<uint64_t>(data[pos + i]) << (i * 8);
    }

    return entry;
}

// ============================================================================
// RegionFile implementation
// ============================================================================

RegionFile::RegionFile(const std::filesystem::path& basePath, RegionPos pos)
    : pos_(pos)
    , basePath_(basePath)
{
    // Construct file paths
    std::string filename = "r." + std::to_string(pos.rx) + "." + std::to_string(pos.rz);
    datPath_ = basePath_ / (filename + ".dat");
    tocPath_ = basePath_ / (filename + ".toc");

    openFiles();
    loadToc();
}

RegionFile::~RegionFile() {
    flush();
    if (datFile_.is_open()) datFile_.close();
    if (tocFile_.is_open()) tocFile_.close();
}

bool RegionFile::openFiles() {
    // Ensure directory exists
    std::filesystem::create_directories(basePath_);

    // Open data file (create if doesn't exist)
    datFile_.open(datPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!datFile_.is_open()) {
        // Try creating it
        std::ofstream create(datPath_, std::ios::binary);
        create.close();
        datFile_.open(datPath_, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!datFile_.is_open()) {
        return false;
    }

    // Get data file size
    datFile_.seekg(0, std::ios::end);
    dataFileEnd_ = static_cast<uint64_t>(datFile_.tellg());

    // Open ToC file
    tocFile_.open(tocPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!tocFile_.is_open()) {
        // Try creating it with header
        std::ofstream create(tocPath_, std::ios::binary);
        if (create.is_open()) {
            // Write header
            uint8_t header[8];
            for (int i = 0; i < 4; ++i) {
                header[i] = static_cast<uint8_t>((TOC_MAGIC >> (i * 8)) & 0xFF);
            }
            for (int i = 0; i < 4; ++i) {
                header[4 + i] = static_cast<uint8_t>((TOC_VERSION >> (i * 8)) & 0xFF);
            }
            create.write(reinterpret_cast<char*>(header), 8);
            create.close();
        }
        tocFile_.open(tocPath_, std::ios::in | std::ios::out | std::ios::binary);
    }

    return datFile_.is_open() && tocFile_.is_open();
}

bool RegionFile::loadToc() {
    if (!tocFile_.is_open()) {
        return false;
    }

    tocFile_.seekg(0, std::ios::end);
    auto fileSize = tocFile_.tellg();
    tocFile_.seekg(0, std::ios::beg);

    if (fileSize < 8) {
        return false;  // Too small for header
    }

    // Read and verify header
    uint8_t header[8];
    tocFile_.read(reinterpret_cast<char*>(header), 8);

    uint32_t magic = 0;
    for (int i = 0; i < 4; ++i) {
        magic |= static_cast<uint32_t>(header[i]) << (i * 8);
    }
    // Note: version is stored in header[4..7] but not currently used

    if (magic != TOC_MAGIC) {
        return false;  // Invalid file
    }

    // Read entries
    std::vector<uint8_t> entryBuf(TocEntry::SERIALIZED_SIZE);
    while (tocFile_.read(reinterpret_cast<char*>(entryBuf.data()), TocEntry::SERIALIZED_SIZE)) {
        auto entry = TocEntry::fromBytes(entryBuf.data(), entryBuf.size());
        if (entry) {
            uint32_t key = localKey(entry->localX, entry->localZ);

            // Check if this is newer than existing entry
            auto it = index_.find(key);
            if (it == index_.end() || entry->timestamp > it->second.timestamp) {
                // If replacing, add old span to free list
                if (it != index_.end()) {
                    addFreeSpan(it->second.offset, it->second.size);
                }
                index_[key] = *entry;
            } else {
                // Older entry, add to free list
                addFreeSpan(entry->offset, entry->size);
            }
        }
    }

    tocFile_.clear();  // Clear EOF flag
    return true;
}

bool RegionFile::appendTocEntry(const TocEntry& entry) {
    if (!tocFile_.is_open()) {
        return false;
    }

    tocFile_.seekp(0, std::ios::end);
    auto bytes = entry.toBytes();
    tocFile_.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    tocFile_.flush();

    return tocFile_.good();
}

bool RegionFile::writeChunkData(uint64_t offset, const std::vector<uint8_t>& data) {
    if (!datFile_.is_open()) {
        return false;
    }

    // Write header: magic (4) + size (4)
    uint8_t header[8];
    for (int i = 0; i < 4; ++i) {
        header[i] = static_cast<uint8_t>((DAT_CHUNK_MAGIC >> (i * 8)) & 0xFF);
    }
    uint32_t dataSize = static_cast<uint32_t>(data.size());
    for (int i = 0; i < 4; ++i) {
        header[4 + i] = static_cast<uint8_t>((dataSize >> (i * 8)) & 0xFF);
    }

    datFile_.seekp(static_cast<std::streamoff>(offset));
    datFile_.write(reinterpret_cast<const char*>(header), 8);
    datFile_.write(reinterpret_cast<const char*>(data.data()), data.size());
    datFile_.flush();

    return datFile_.good();
}

std::vector<uint8_t> RegionFile::readChunkData(uint64_t offset, uint32_t size) {
    if (!datFile_.is_open()) {
        return {};
    }

    // Read header
    uint8_t header[8];
    datFile_.seekg(static_cast<std::streamoff>(offset));
    datFile_.read(reinterpret_cast<char*>(header), 8);

    if (!datFile_.good()) {
        return {};
    }

    // Verify magic
    uint32_t magic = 0;
    for (int i = 0; i < 4; ++i) {
        magic |= static_cast<uint32_t>(header[i]) << (i * 8);
    }
    if (magic != DAT_CHUNK_MAGIC) {
        return {};
    }

    // Read size from header (for verification)
    uint32_t storedSize = 0;
    for (int i = 0; i < 4; ++i) {
        storedSize |= static_cast<uint32_t>(header[4 + i]) << (i * 8);
    }

    // Use the smaller of stored size and expected size
    uint32_t readSize = std::min(storedSize, size - 8);

    // Read data
    std::vector<uint8_t> data(readSize);
    datFile_.read(reinterpret_cast<char*>(data.data()), readSize);

    if (!datFile_.good()) {
        return {};
    }

    return data;
}

std::optional<uint64_t> RegionFile::findFreeSpan(uint64_t size) {
    // Find smallest span that fits (best-fit)
    FreeSpan target{0, size};
    auto it = freeSpans_.lower_bound(target);

    if (it != freeSpans_.end()) {
        return it->offset;
    }

    return std::nullopt;
}

void RegionFile::addFreeSpan(uint64_t offset, uint64_t size) {
    if (size == 0) return;

    // TODO: Merge with adjacent spans
    // For now, just add it
    freeSpans_.insert(FreeSpan{offset, size});
}

void RegionFile::removeFreeSpan(uint64_t offset, uint64_t size) {
    // Find and remove the span
    for (auto it = freeSpans_.begin(); it != freeSpans_.end(); ++it) {
        if (it->offset == offset && it->size >= size) {
            uint64_t remaining = it->size - size;
            freeSpans_.erase(it);
            if (remaining > 0) {
                // Add back the remaining portion
                freeSpans_.insert(FreeSpan{offset + size, remaining});
            }
            return;
        }
    }
}

uint64_t RegionFile::currentTimestamp() {
    // Use a combination of real time (microseconds) and a monotonic counter
    // to ensure uniqueness even within the same microsecond
    static std::atomic<uint64_t> counter{0};

    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    uint64_t micros = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count()
    );

    // Combine time with counter to ensure monotonicity
    // Use lower 20 bits for counter (allows ~1M ops per microsecond)
    uint64_t count = counter.fetch_add(1, std::memory_order_relaxed);
    return (micros << 20) | (count & 0xFFFFF);
}

bool RegionFile::saveColumn(const ChunkColumn& column, ColumnPos pos) {
    // Verify this column belongs to our region
    if (RegionPos::fromColumn(pos) != pos_) {
        return false;
    }

    auto [lx, lz] = RegionPos::toLocal(pos);

    // Serialize the column to CBOR
    auto cbor = ColumnSerializer::toCBOR(column, pos.x, pos.z);

    // TODO: LZ4 compress here
    // For now, store uncompressed

    // Calculate total size (header + data)
    uint32_t totalSize = 8 + static_cast<uint32_t>(cbor.size());

    // Find location to write
    uint64_t writeOffset;
    auto freeSpot = findFreeSpan(totalSize);
    if (freeSpot) {
        writeOffset = *freeSpot;
        removeFreeSpan(writeOffset, totalSize);
    } else {
        // Append to end
        // Note: If there's a free span at EOF that's too small, we don't extend it.
        // This leaves a small gap, but compactToc() or defrag will reclaim it eventually.
        writeOffset = dataFileEnd_;
        dataFileEnd_ += totalSize;
    }

    // Write chunk data
    if (!writeChunkData(writeOffset, cbor)) {
        return false;
    }

    // Update index
    uint32_t key = localKey(lx, lz);
    auto it = index_.find(key);
    if (it != index_.end()) {
        // Add old location to free list
        addFreeSpan(it->second.offset, it->second.size);
    }

    // Create and append ToC entry
    TocEntry entry;
    entry.localX = lx;
    entry.localZ = lz;
    entry.offset = writeOffset;
    entry.size = totalSize;
    entry.timestamp = currentTimestamp();

    if (!appendTocEntry(entry)) {
        return false;
    }

    // Update in-memory index
    index_[key] = entry;

    return true;
}

std::unique_ptr<ChunkColumn> RegionFile::loadColumn(ColumnPos pos) {
    // Verify this column belongs to our region
    if (RegionPos::fromColumn(pos) != pos_) {
        return nullptr;
    }

    auto [lx, lz] = RegionPos::toLocal(pos);
    uint32_t key = localKey(lx, lz);

    auto it = index_.find(key);
    if (it == index_.end()) {
        return nullptr;  // Column doesn't exist
    }

    const TocEntry& entry = it->second;

    // Read chunk data
    auto data = readChunkData(entry.offset, entry.size);
    if (data.empty()) {
        return nullptr;
    }

    // TODO: LZ4 decompress here
    // For now, data is uncompressed CBOR

    // Deserialize
    int32_t x, z;
    return ColumnSerializer::fromCBOR(data, &x, &z);
}

bool RegionFile::hasColumn(ColumnPos pos) const {
    if (RegionPos::fromColumn(pos) != pos_) {
        return false;
    }

    auto [lx, lz] = RegionPos::toLocal(pos);
    return index_.contains(localKey(lx, lz));
}

std::vector<ColumnPos> RegionFile::getExistingColumns() const {
    std::vector<ColumnPos> result;
    result.reserve(index_.size());

    for (const auto& [key, entry] : index_) {
        // Convert local coords back to world coords
        int32_t worldX = pos_.rx * REGION_SIZE + entry.localX;
        int32_t worldZ = pos_.rz * REGION_SIZE + entry.localZ;
        result.push_back(ColumnPos{worldX, worldZ});
    }

    return result;
}

void RegionFile::flush() {
    if (datFile_.is_open()) {
        datFile_.flush();
    }
    if (tocFile_.is_open()) {
        tocFile_.flush();
    }
}

void RegionFile::compactToc() {
    if (!tocFile_.is_open()) {
        return;
    }

    // Create a temporary file with compacted entries
    auto tempPath = tocPath_;
    tempPath += ".tmp";

    {
        std::ofstream tempFile(tempPath, std::ios::binary);
        if (!tempFile.is_open()) {
            return;
        }

        // Write header
        uint8_t header[8];
        for (int i = 0; i < 4; ++i) {
            header[i] = static_cast<uint8_t>((TOC_MAGIC >> (i * 8)) & 0xFF);
        }
        for (int i = 0; i < 4; ++i) {
            header[4 + i] = static_cast<uint8_t>((TOC_VERSION >> (i * 8)) & 0xFF);
        }
        tempFile.write(reinterpret_cast<char*>(header), 8);

        // Write current index entries (only latest for each position)
        for (const auto& [key, entry] : index_) {
            auto bytes = entry.toBytes();
            tempFile.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        }
    }

    // Close current ToC file
    tocFile_.close();

    // Replace with compacted file
    std::filesystem::remove(tocPath_);
    std::filesystem::rename(tempPath, tocPath_);

    // Reopen
    tocFile_.open(tocPath_, std::ios::in | std::ios::out | std::ios::binary);
}

}  // namespace finevox
