#pragma once

/**
 * @file event_journal.hpp
 * @brief Per-column event journal for persisting deferred cross-chunk events
 *
 * When block update events target unloaded chunks, they are written to a
 * binary journal file on disk. When the target column loads, the journal
 * is read, events are replayed, and the journal is deleted.
 *
 * Journal format: binary append log, one entry per event.
 * Each entry:
 *   uint8_t  eventType
 *   int32_t  x, y, z  (BlockCoord)
 *   uint32_t blockType (InternedId)
 *   uint32_t fluidType (InternedId)
 *   uint8_t  face
 *   uint8_t  level
 *
 * Total: 20 bytes per entry
 */

#include "finevox/core/block_event.hpp"
#include "finevox/core/position.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace finevox {

/// Size of a single journal entry in bytes
/// Layout: eventType(1) + x(4) + y(4) + z(4) + blockType(4) + face(1) + fluidType(4) + level(1) + pad(1) = 24
constexpr size_t JOURNAL_ENTRY_SIZE = 24;

/// Manages reading/writing binary journal files for deferred events
class EventJournal {
public:
    /// Set the base directory for journal files (e.g., "world/journals/")
    void setDirectory(const std::filesystem::path& dir);

    /// Get the journal directory
    [[nodiscard]] const std::filesystem::path& directory() const { return dir_; }

    /// Append an event to the journal file for the given column
    bool appendEvent(ColumnPos colPos, const BlockEvent& event);

    /// Read all events from the journal file for a column
    /// Returns empty vector if no journal exists
    [[nodiscard]] std::vector<BlockEvent> readEvents(ColumnPos colPos) const;

    /// Delete the journal file for a column (call after successful merge)
    bool deleteJournal(ColumnPos colPos);

    /// Check if a journal file exists for a column
    [[nodiscard]] bool hasJournal(ColumnPos colPos) const;

    /// Get the journal file path for a column
    [[nodiscard]] std::filesystem::path journalPath(ColumnPos colPos) const;

    /// Write multiple events at once (batch append)
    bool appendEvents(ColumnPos colPos, const std::vector<BlockEvent>& events);

private:
    std::filesystem::path dir_;

    /// Serialize a BlockEvent to a fixed-size binary entry
    static void serializeEntry(const BlockEvent& event, uint8_t* out);

    /// Deserialize a fixed-size binary entry to a BlockEvent
    static BlockEvent deserializeEntry(const uint8_t* data);
};

}  // namespace finevox
