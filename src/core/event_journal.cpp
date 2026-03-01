#include "finevox/core/event_journal.hpp"
#include <fstream>
#include <cstring>

namespace finevox {

void EventJournal::setDirectory(const std::filesystem::path& dir) {
    dir_ = dir;
}

std::filesystem::path EventJournal::journalPath(ColumnPos colPos) const {
    return dir_ / (std::to_string(colPos.x) + "_" + std::to_string(colPos.z) + ".journal");
}

bool EventJournal::hasJournal(ColumnPos colPos) const {
    return std::filesystem::exists(journalPath(colPos));
}

void EventJournal::serializeEntry(const BlockEvent& event, uint8_t* out) {
    std::memset(out, 0, JOURNAL_ENTRY_SIZE);

    // Byte 0: eventType
    out[0] = static_cast<uint8_t>(event.type);

    // Bytes 1-12: position (3 x int32_t)
    std::memcpy(out + 1, &event.pos.x, 4);
    std::memcpy(out + 5, &event.pos.y, 4);
    std::memcpy(out + 9, &event.pos.z, 4);

    // Bytes 13-16: blockType (InternedId = uint32_t)
    auto blockId = event.blockType.id;
    std::memcpy(out + 13, &blockId, 4);

    // Byte 17: face
    out[17] = static_cast<uint8_t>(event.face);

    // Bytes 18-21: fluidType (InternedId = uint32_t)
    auto fluidId = event.fluidType.id;
    std::memcpy(out + 18, &fluidId, 4);

    // Byte 22: fluidLevel
    out[22] = event.fluidLevel;

    // Byte 23: padding (already zeroed)
}

BlockEvent EventJournal::deserializeEntry(const uint8_t* data) {
    BlockEvent event;

    // Byte 0: eventType
    event.type = static_cast<EventType>(data[0]);

    // Bytes 1-12: position
    std::memcpy(&event.pos.x, data + 1, 4);
    std::memcpy(&event.pos.y, data + 5, 4);
    std::memcpy(&event.pos.z, data + 9, 4);

    // Compute derived positions
    event.localPos = event.pos.local();
    event.chunkPos = ChunkPos::fromBlock(event.pos);

    // Bytes 13-16: blockType
    InternedId blockId;
    std::memcpy(&blockId, data + 13, 4);
    event.blockType = BlockTypeId(blockId);

    // Byte 17: face
    event.face = static_cast<Face>(data[17]);

    // Bytes 18-21: fluidType
    InternedId fluidId;
    std::memcpy(&fluidId, data + 18, 4);
    event.fluidType = FluidTypeId(fluidId);

    // Byte 22: fluidLevel
    event.fluidLevel = data[22];

    return event;
}

bool EventJournal::appendEvent(ColumnPos colPos, const BlockEvent& event) {
    if (dir_.empty()) return false;

    // Ensure directory exists
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) return false;

    auto path = journalPath(colPos);
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) return false;

    uint8_t buf[JOURNAL_ENTRY_SIZE];
    serializeEntry(event, buf);
    file.write(reinterpret_cast<const char*>(buf), JOURNAL_ENTRY_SIZE);
    return file.good();
}

bool EventJournal::appendEvents(ColumnPos colPos, const std::vector<BlockEvent>& events) {
    if (dir_.empty() || events.empty()) return false;

    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec) return false;

    auto path = journalPath(colPos);
    std::ofstream file(path, std::ios::binary | std::ios::app);
    if (!file) return false;

    uint8_t buf[JOURNAL_ENTRY_SIZE];
    for (const auto& event : events) {
        serializeEntry(event, buf);
        file.write(reinterpret_cast<const char*>(buf), JOURNAL_ENTRY_SIZE);
    }
    return file.good();
}

std::vector<BlockEvent> EventJournal::readEvents(ColumnPos colPos) const {
    std::vector<BlockEvent> events;
    if (dir_.empty()) return events;

    auto path = journalPath(colPos);
    std::ifstream file(path, std::ios::binary);
    if (!file) return events;

    uint8_t buf[JOURNAL_ENTRY_SIZE];
    while (file.read(reinterpret_cast<char*>(buf), JOURNAL_ENTRY_SIZE)) {
        events.push_back(deserializeEntry(buf));
    }

    return events;
}

bool EventJournal::deleteJournal(ColumnPos colPos) {
    if (dir_.empty()) return false;

    auto path = journalPath(colPos);
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}

}  // namespace finevox
