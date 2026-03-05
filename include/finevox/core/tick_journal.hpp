#pragma once

/**
 * @file tick_journal.hpp
 * @brief Per-column journal for persisting scheduled ticks across save/load
 *
 * When a column is saved or unloaded, its pending scheduled ticks are written
 * to a binary journal file. When the column loads, the journal is read, ticks
 * are merged back into the UpdateScheduler, and the journal is deleted.
 *
 * Journal format: binary, one entry per tick.
 * Each entry (24 bytes):
 *   int32_t  x, y, z       (BlockCoord)
 *   uint64_t targetTick     (absolute game tick)
 *   uint8_t  tickType       (TickType enum)
 *   uint8_t  pad[3]         (zeroed)
 */

#include "finevox/core/position.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace finevox {

// Forward declaration — full definition in event_queue.hpp
struct ScheduledTick;

/// Size of a single tick journal entry in bytes
constexpr size_t TICK_JOURNAL_ENTRY_SIZE = 24;

/// Manages reading/writing binary journal files for scheduled ticks.
/// Modeled on EventJournal but for ScheduledTick entries.
///
/// Thread safety: NOT thread-safe. Designed to be called from a single thread
/// at a time (IO save thread for writes, IO load thread for reads).
/// ColumnManager's currentlySaving_ set prevents concurrent load+save for
/// the same column, ensuring no two threads access the same journal file.
class TickJournal {
public:
    /// Set the base directory for journal files (e.g., "world/tick_journals/")
    void setDirectory(const std::filesystem::path& dir);

    /// Get the journal directory
    [[nodiscard]] const std::filesystem::path& directory() const { return dir_; }

    /// Write ticks for a column (overwrites any existing journal).
    /// Used during periodic saves to snapshot current tick state.
    bool writeTicks(ColumnPos colPos, const std::vector<ScheduledTick>& ticks);

    /// Append ticks for a column (adds to existing journal).
    /// Used during eviction to add post-save ticks.
    bool appendTicks(ColumnPos colPos, const std::vector<ScheduledTick>& ticks);

    /// Read all ticks from the journal file for a column.
    /// Returns empty vector if no journal exists.
    [[nodiscard]] std::vector<ScheduledTick> readTicks(ColumnPos colPos) const;

    /// Delete the journal file for a column (call after successful merge).
    bool deleteJournal(ColumnPos colPos);

    /// Check if a journal file exists for a column.
    [[nodiscard]] bool hasJournal(ColumnPos colPos) const;

    /// Get the journal file path for a column.
    [[nodiscard]] std::filesystem::path journalPath(ColumnPos colPos) const;

private:
    std::filesystem::path dir_;

    /// Serialize a ScheduledTick to a fixed-size binary entry
    static void serializeEntry(const ScheduledTick& tick, uint8_t* out);

    /// Deserialize a fixed-size binary entry to a ScheduledTick
    static ScheduledTick deserializeEntry(const uint8_t* data);
};

}  // namespace finevox
