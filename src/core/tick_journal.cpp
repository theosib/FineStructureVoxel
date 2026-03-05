#include "finevox/core/tick_journal.hpp"
#include "finevox/core/event_queue.hpp"  // ScheduledTick, TickType
#include <fstream>
#include <cstring>

namespace finevox {

void TickJournal::setDirectory(const std::filesystem::path& dir) {
    dir_ = dir;
}

std::filesystem::path TickJournal::journalPath(ColumnPos colPos) const {
    return dir_ / (std::to_string(colPos.x) + "_" + std::to_string(colPos.z) + ".ticks");
}

bool TickJournal::hasJournal(ColumnPos colPos) const {
    return std::filesystem::exists(journalPath(colPos));
}

void TickJournal::serializeEntry(const ScheduledTick& tick, uint8_t* out) {
    std::memset(out, 0, TICK_JOURNAL_ENTRY_SIZE);

    // Bytes 0-11: position (3 x int32_t)
    std::memcpy(out + 0, &tick.pos.x, 4);
    std::memcpy(out + 4, &tick.pos.y, 4);
    std::memcpy(out + 8, &tick.pos.z, 4);

    // Bytes 12-19: targetTick (uint64_t)
    std::memcpy(out + 12, &tick.targetTick, 8);

    // Byte 20: tickType
    out[20] = static_cast<uint8_t>(tick.type);

    // Bytes 21-23: padding (already zeroed)
}

ScheduledTick TickJournal::deserializeEntry(const uint8_t* data) {
    ScheduledTick tick;

    // Bytes 0-11: position
    std::memcpy(&tick.pos.x, data + 0, 4);
    std::memcpy(&tick.pos.y, data + 4, 4);
    std::memcpy(&tick.pos.z, data + 8, 4);

    // Bytes 12-19: targetTick
    std::memcpy(&tick.targetTick, data + 12, 8);

    // Byte 20: tickType
    tick.type = static_cast<TickType>(data[20]);

    return tick;
}

bool TickJournal::writeTicks(ColumnPos colPos, const std::vector<ScheduledTick>& ticks) {
    if (dir_.empty()) return false;

    // If no ticks, delete any existing journal
    if (ticks.empty()) {
        deleteJournal(colPos);
        return true;
    }

    // Ensure directory exists
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) return false;

    auto path = journalPath(colPos);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return false;

    uint8_t buf[TICK_JOURNAL_ENTRY_SIZE];
    for (const auto& tick : ticks) {
        serializeEntry(tick, buf);
        file.write(reinterpret_cast<const char*>(buf), TICK_JOURNAL_ENTRY_SIZE);
    }
    return file.good();
}

bool TickJournal::appendTicks(ColumnPos colPos, const std::vector<ScheduledTick>& ticks) {
    if (dir_.empty() || ticks.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) return false;

    auto path = journalPath(colPos);
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) return false;

    uint8_t buf[TICK_JOURNAL_ENTRY_SIZE];
    for (const auto& tick : ticks) {
        serializeEntry(tick, buf);
        file.write(reinterpret_cast<const char*>(buf), TICK_JOURNAL_ENTRY_SIZE);
    }
    return file.good();
}

std::vector<ScheduledTick> TickJournal::readTicks(ColumnPos colPos) const {
    std::vector<ScheduledTick> ticks;
    if (dir_.empty()) return ticks;

    auto path = journalPath(colPos);
    std::ifstream file(path, std::ios::binary);
    if (!file) return ticks;

    uint8_t buf[TICK_JOURNAL_ENTRY_SIZE];
    while (file.read(reinterpret_cast<char*>(buf), TICK_JOURNAL_ENTRY_SIZE)) {
        ticks.push_back(deserializeEntry(buf));
    }

    return ticks;
}

bool TickJournal::deleteJournal(ColumnPos colPos) {
    if (dir_.empty()) return false;

    auto path = journalPath(colPos);
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

}  // namespace finevox
