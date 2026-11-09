#include "finevox/net/entity_wire.hpp"

namespace finevox::net {

namespace {

// The smallest records ever sent, by the first sender of each. A record shorter than this is
// damaged; anything past it is optional, and read only if it's there.
constexpr size_t SPAWN_AFTER_TYPE_BYTES = 2 + 3 * 8 + 2 * 4;  // faction, position, yaw, pitch
constexpr size_t MOVE_CORE_BYTES = 8 + 8 * 4 + 2;
constexpr size_t PROJECTILE_CORE_BYTES = 6 * 4;
// Each record is at least its length, so no message holds more records than it has bytes
constexpr size_t MIN_RECORD_BYTES = 1;

void writeVec3(ByteWriter& w, const glm::vec3& v) {
    w.f32(v.x);
    w.f32(v.y);
    w.f32(v.z);
}

glm::vec3 readVec3(ByteReader& r) {
    glm::vec3 v;
    v.x = r.f32();
    v.y = r.f32();
    v.z = r.f32();
    return v;
}

}  // namespace

std::vector<uint8_t> encodeEntitySpawn(const EntitySpawnWire& spawn) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.u64(spawn.id);
    w.var(spawn.type);
    w.u16(spawn.faction);
    w.f64(spawn.position.x);
    w.f64(spawn.position.y);
    w.f64(spawn.position.z);
    w.f32(spawn.yaw);
    w.f32(spawn.pitch);
    w.u8(spawn.health);
    return body;
}

std::optional<EntitySpawnWire> decodeEntitySpawn(std::span<const uint8_t> body) {
    // The message is the record: it ends where the message does
    ByteReader r(body);
    EntitySpawnWire spawn;
    spawn.id = r.u64();
    spawn.type = static_cast<uint32_t>(r.var());
    if (!r.ok() || r.remaining() < SPAWN_AFTER_TYPE_BYTES) return std::nullopt;
    spawn.faction = r.u16();
    spawn.position.x = r.f64();
    spawn.position.y = r.f64();
    spawn.position.z = r.f64();
    spawn.yaw = r.f32();
    spawn.pitch = r.f32();
    if (!r.ok()) return std::nullopt;
    if (r.remaining() >= 1) spawn.health = r.u8();
    return spawn;
}

std::vector<uint8_t> encodeEntityBatch(const EntityBatchWire& batch) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.u64(batch.tick);
    w.var(batch.entities.size());
    std::vector<uint8_t> record;
    for (const EntityMoveWire& e : batch.entities) {
        record.clear();
        ByteWriter rw(record);
        rw.u64(e.id);
        writeVec3(rw, e.position);
        writeVec3(rw, e.velocity);
        rw.f32(e.yaw);
        rw.f32(e.pitch);
        rw.u8(e.animation);
        rw.u8(e.onGround ? 1 : 0);
        rw.u8(e.health);
        w.bytes(record);
    }
    return body;
}

std::optional<EntityBatchWire> decodeEntityBatch(std::span<const uint8_t> body) {
    ByteReader r(body);
    EntityBatchWire batch;
    batch.tick = r.u64();
    uint64_t count = r.var();
    if (!r.ok() || count > r.remaining() / MIN_RECORD_BYTES) return std::nullopt;
    batch.entities.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        ByteReader e(r.bytes());
        if (!r.ok()) return std::nullopt;
        if (e.remaining() < MOVE_CORE_BYTES) continue;
        EntityMoveWire move;
        move.id = e.u64();
        move.position = readVec3(e);
        move.velocity = readVec3(e);
        move.yaw = e.f32();
        move.pitch = e.f32();
        move.animation = e.u8();
        move.onGround = (e.u8() & 1) != 0;
        if (e.remaining() >= 1) move.health = e.u8();
        batch.entities.push_back(move);
    }
    return batch;
}

std::vector<uint8_t> encodeProjectiles(const std::vector<ProjectileWire>& shots) {
    std::vector<uint8_t> body;
    ByteWriter w(body);
    w.var(shots.size());
    std::vector<uint8_t> record;
    for (const ProjectileWire& shot : shots) {
        record.clear();
        ByteWriter rw(record);
        writeVec3(rw, shot.position);
        writeVec3(rw, shot.velocity);
        w.bytes(record);
    }
    return body;
}

std::optional<std::vector<ProjectileWire>> decodeProjectiles(std::span<const uint8_t> body) {
    ByteReader r(body);
    uint64_t count = r.var();
    if (!r.ok() || count > r.remaining() / MIN_RECORD_BYTES) return std::nullopt;
    std::vector<ProjectileWire> shots;
    shots.reserve(count);
    for (uint64_t i = 0; i < count; ++i) {
        ByteReader e(r.bytes());
        if (!r.ok()) return std::nullopt;
        if (e.remaining() < PROJECTILE_CORE_BYTES) continue;
        ProjectileWire shot;
        shot.position = readVec3(e);
        shot.velocity = readVec3(e);
        shots.push_back(shot);
    }
    return shots;
}

}  // namespace finevox::net
