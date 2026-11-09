#include "finevox/net/messages.hpp"

#include "finevox/net/subchunk_codec.hpp"
#include "finevox/net/wire.hpp"

#include <cmath>

namespace finevox::net {

namespace {

// Each message's keys, as PROTOCOL.md lists them. A key is never reused for something else.
namespace name_key { enum : uint32_t { ID = 0, NAME = 1 }; }
namespace vision_key { enum : uint32_t { CELLS = 0, RAW_SIZE = 1 }; }
namespace time_key { enum : uint32_t { TICKS = 0, HELD = 1, PAUSED = 2 }; }
namespace ghosts_key { enum : uint32_t { GROUPS = 0 }; }
namespace ghost_key { enum : uint32_t { BUILT = 0, CELLS = 1 }; }
namespace spawn_key {
enum : uint32_t { ID = 0, TYPE = 1, FACTION = 2, X = 3, Y = 4, Z = 5, YAW = 6, PITCH = 7, HEALTH = 8 };
}
namespace gone_key { enum : uint32_t { ID = 0, DIED = 1 }; }
namespace batch_key { enum : uint32_t { TICK = 0, ENTITIES = 1 }; }
namespace move_key {
enum : uint32_t {
    ID = 0, X = 1, Y = 2, Z = 3, VX = 4, VY = 5, VZ = 6, YAW = 7, PITCH = 8, ANIMATION = 9, ON_GROUND = 10,
    HEALTH = 11,
};
}
namespace shots_key { enum : uint32_t { SHOTS = 0 }; }
namespace shot_key { enum : uint32_t { X = 0, Y = 1, Z = 2, VX = 3, VY = 4, VZ = 5 }; }
namespace player_key {
enum : uint32_t { X = 0, Y = 1, Z = 2, VX = 3, VY = 4, VZ = 5, YAW = 6, PITCH = 7, ON_GROUND = 8 };
}
namespace change_key { enum : uint32_t { X = 0, Y = 1, Z = 2 }; }
namespace hint_key { enum : uint32_t { X = 0, Z = 1 }; }
namespace chunk_key { enum : uint32_t { SHA256 = 0, OFFSET = 1, TOTAL = 2, DATA = 3 }; }

/// A float field, left out when it's zero
void f32Unless0(MapWriter& w, uint32_t key, float v) {
    if (v != 0.0f) w.f32(key, v);
}

float readF32(CborIn& in) { return static_cast<float>(in.number()); }

}  // namespace

// ---- §5 World ----------------------------------------------------------------------------

std::vector<uint8_t> encodeName(const NameWire& name) {
    MapWriter w;
    w.uint(name_key::ID, name.id);
    w.text(name_key::NAME, name.name);
    return w.finish();
}

std::optional<NameWire> decodeName(std::span<const uint8_t> body) {
    CborIn in(body);
    NameWire name;
    bool haveId = false, haveName = false;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case name_key::ID: name.id = static_cast<uint32_t>(in.uint()); haveId = true; return true;
            case name_key::NAME: name.name = std::string(in.text()); haveName = true; return true;
            default: return false;
        }
    });
    if (!read || !haveId || !haveName) return std::nullopt;
    return name;
}

std::vector<uint8_t> encodeVision(const VisionWire& vision) {
    MapWriter w;
    w.bytes(vision_key::CELLS, lz4Compress(vision.cells));
    w.uint(vision_key::RAW_SIZE, vision.cells.size());
    return w.finish();
}

std::optional<VisionWire> decodeVision(std::span<const uint8_t> body, size_t limit) {
    CborIn in(body);
    std::span<const uint8_t> block;
    std::optional<uint64_t> rawSize;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case vision_key::CELLS: block = in.bytes(); return true;
            case vision_key::RAW_SIZE: rawSize = in.uint(); return true;
            default: return false;
        }
    });
    VisionWire vision;
    if (!read || !rawSize || !lz4Decompress(block, *rawSize, limit, vision.cells)) return std::nullopt;
    return vision;
}

std::vector<uint8_t> encodeTime(const TimeWire& time) {
    MapWriter w;
    w.uint(time_key::TICKS, time.ticks);
    if (time.held) w.boolean(time_key::HELD, true);
    if (time.paused) w.boolean(time_key::PAUSED, true);
    return w.finish();
}

std::optional<TimeWire> decodeTime(std::span<const uint8_t> body) {
    CborIn in(body);
    TimeWire time;
    bool haveTicks = false;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case time_key::TICKS: time.ticks = in.uint(); haveTicks = true; return true;
            case time_key::HELD: time.held = in.boolean(); return true;
            case time_key::PAUSED: time.paused = in.boolean(); return true;
            default: return false;
        }
    });
    if (!read || !haveTicks) return std::nullopt;
    return time;
}

std::vector<uint8_t> encodeGhosts(const std::vector<GhostWire>& ghosts) {
    MapWriter w;
    w.maps(ghosts_key::GROUPS, ghosts.size(), [&](size_t i, MapWriter& g) {
        const GhostWire& ghost = ghosts[i];
        g.f32(ghost_key::BUILT, ghost.built);
        // Four numbers a cell, flat: a map a cell would be most of the message
        g.array(ghost_key::CELLS, ghost.cells.size() * 4, [&](CborOut& out) {
            for (const GhostCellWire& cell : ghost.cells) {
                out.sint(cell.x);
                out.sint(cell.y);
                out.sint(cell.z);
                out.uint(cell.block);
            }
        });
    });
    return w.finish();
}

std::optional<std::vector<GhostWire>> decodeGhosts(std::span<const uint8_t> body) {
    CborIn in(body);
    std::vector<GhostWire> ghosts;
    bool read = in.fields([&](uint64_t key) {
        if (key != ghosts_key::GROUPS) return false;
        uint64_t groups = in.array();
        ghosts.reserve(groups);
        for (uint64_t i = 0; in.ok() && i < groups; ++i) {
            GhostWire ghost;
            in.fields([&](uint64_t field) {
                switch (field) {
                    case ghost_key::BUILT: ghost.built = readF32(in); return true;
                    case ghost_key::CELLS: {
                        uint64_t count = in.array();
                        if (count % 4 != 0) {
                            for (uint64_t n = 0; n < count; ++n) in.skip();
                            return true;
                        }
                        ghost.cells.resize(count / 4);
                        for (auto& cell : ghost.cells) {
                            cell.x = static_cast<int32_t>(in.sint());
                            cell.y = static_cast<int32_t>(in.sint());
                            cell.z = static_cast<int32_t>(in.sint());
                            cell.block = static_cast<uint32_t>(in.uint());
                        }
                        return true;
                    }
                    default: return false;
                }
            });
            ghosts.push_back(std::move(ghost));
        }
        return true;
    });
    if (!read) return std::nullopt;
    return ghosts;
}

// ---- §6 Entities -------------------------------------------------------------------------

std::vector<uint8_t> encodeEntitySpawn(const EntitySpawnWire& spawn) {
    MapWriter w;
    w.uint(spawn_key::ID, spawn.id);
    w.uint(spawn_key::TYPE, spawn.type);
    if (spawn.faction != 0) w.uint(spawn_key::FACTION, spawn.faction);
    w.f64(spawn_key::X, spawn.position.x);
    w.f64(spawn_key::Y, spawn.position.y);
    w.f64(spawn_key::Z, spawn.position.z);
    f32Unless0(w, spawn_key::YAW, spawn.yaw);
    f32Unless0(w, spawn_key::PITCH, spawn.pitch);
    if (spawn.health != FULL_HEALTH) w.uint(spawn_key::HEALTH, spawn.health);
    return w.finish();
}

std::optional<EntitySpawnWire> decodeEntitySpawn(std::span<const uint8_t> body) {
    CborIn in(body);
    EntitySpawnWire spawn;
    int need = 0;  // id, type and position: a bit each
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case spawn_key::ID: spawn.id = in.uint(); need |= 1; return true;
            case spawn_key::TYPE: spawn.type = static_cast<uint32_t>(in.uint()); need |= 2; return true;
            case spawn_key::FACTION: spawn.faction = static_cast<uint16_t>(in.uint()); return true;
            case spawn_key::X: spawn.position.x = in.number(); need |= 4; return true;
            case spawn_key::Y: spawn.position.y = in.number(); need |= 8; return true;
            case spawn_key::Z: spawn.position.z = in.number(); need |= 16; return true;
            case spawn_key::YAW: spawn.yaw = readF32(in); return true;
            case spawn_key::PITCH: spawn.pitch = readF32(in); return true;
            case spawn_key::HEALTH: spawn.health = static_cast<uint8_t>(in.uint()); return true;
            default: return false;
        }
    });
    if (!read || need != 31) return std::nullopt;
    return spawn;
}

std::vector<uint8_t> encodeEntityGone(const EntityGoneWire& gone) {
    MapWriter w;
    w.uint(gone_key::ID, gone.id);
    if (gone.died) w.boolean(gone_key::DIED, true);
    return w.finish();
}

std::optional<EntityGoneWire> decodeEntityGone(std::span<const uint8_t> body) {
    CborIn in(body);
    EntityGoneWire gone;
    bool haveId = false;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case gone_key::ID: gone.id = in.uint(); haveId = true; return true;
            case gone_key::DIED: gone.died = in.boolean(); return true;
            default: return false;
        }
    });
    if (!read || !haveId) return std::nullopt;
    return gone;
}

std::vector<uint8_t> encodeEntityBatch(const EntityBatchWire& batch) {
    MapWriter w;
    w.uint(batch_key::TICK, batch.tick);
    w.maps(batch_key::ENTITIES, batch.entities.size(), [&](size_t i, MapWriter& e) {
        const EntityMoveWire& move = batch.entities[i];
        e.uint(move_key::ID, move.id);
        e.f32(move_key::X, move.position.x);
        e.f32(move_key::Y, move.position.y);
        e.f32(move_key::Z, move.position.z);
        f32Unless0(e, move_key::VX, move.velocity.x);
        f32Unless0(e, move_key::VY, move.velocity.y);
        f32Unless0(e, move_key::VZ, move.velocity.z);
        f32Unless0(e, move_key::YAW, move.yaw);
        f32Unless0(e, move_key::PITCH, move.pitch);
        if (move.animation != 0) e.uint(move_key::ANIMATION, move.animation);
        if (move.onGround) e.boolean(move_key::ON_GROUND, true);
        if (move.health != FULL_HEALTH) e.uint(move_key::HEALTH, move.health);
    });
    return w.finish();
}

std::optional<EntityBatchWire> decodeEntityBatch(std::span<const uint8_t> body) {
    CborIn in(body);
    EntityBatchWire batch;
    bool haveTick = false;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case batch_key::TICK: batch.tick = in.uint(); haveTick = true; return true;
            case batch_key::ENTITIES: {
                uint64_t count = in.array();
                batch.entities.reserve(count);
                for (uint64_t i = 0; in.ok() && i < count; ++i) {
                    EntityMoveWire move;
                    int need = 0;
                    in.fields([&](uint64_t field) {
                        switch (field) {
                            case move_key::ID: move.id = in.uint(); need |= 1; return true;
                            case move_key::X: move.position.x = readF32(in); need |= 2; return true;
                            case move_key::Y: move.position.y = readF32(in); need |= 4; return true;
                            case move_key::Z: move.position.z = readF32(in); need |= 8; return true;
                            case move_key::VX: move.velocity.x = readF32(in); return true;
                            case move_key::VY: move.velocity.y = readF32(in); return true;
                            case move_key::VZ: move.velocity.z = readF32(in); return true;
                            case move_key::YAW: move.yaw = readF32(in); return true;
                            case move_key::PITCH: move.pitch = readF32(in); return true;
                            case move_key::ANIMATION: move.animation = static_cast<uint8_t>(in.uint()); return true;
                            case move_key::ON_GROUND: move.onGround = in.boolean(); return true;
                            case move_key::HEALTH: move.health = static_cast<uint8_t>(in.uint()); return true;
                            default: return false;
                        }
                    });
                    if (need == 15) batch.entities.push_back(move);
                }
                return true;
            }
            default: return false;
        }
    });
    if (!read || !haveTick) return std::nullopt;
    return batch;
}

std::vector<uint8_t> encodeProjectiles(const std::vector<ProjectileWire>& shots) {
    MapWriter w;
    w.maps(shots_key::SHOTS, shots.size(), [&](size_t i, MapWriter& s) {
        const ProjectileWire& shot = shots[i];
        s.f32(shot_key::X, shot.position.x);
        s.f32(shot_key::Y, shot.position.y);
        s.f32(shot_key::Z, shot.position.z);
        f32Unless0(s, shot_key::VX, shot.velocity.x);
        f32Unless0(s, shot_key::VY, shot.velocity.y);
        f32Unless0(s, shot_key::VZ, shot.velocity.z);
    });
    return w.finish();
}

std::optional<std::vector<ProjectileWire>> decodeProjectiles(std::span<const uint8_t> body) {
    CborIn in(body);
    std::vector<ProjectileWire> shots;
    bool read = in.fields([&](uint64_t key) {
        if (key != shots_key::SHOTS) return false;
        uint64_t count = in.array();
        shots.reserve(count);
        for (uint64_t i = 0; in.ok() && i < count; ++i) {
            ProjectileWire shot;
            int need = 0;
            in.fields([&](uint64_t field) {
                switch (field) {
                    case shot_key::X: shot.position.x = readF32(in); need |= 1; return true;
                    case shot_key::Y: shot.position.y = readF32(in); need |= 2; return true;
                    case shot_key::Z: shot.position.z = readF32(in); need |= 4; return true;
                    case shot_key::VX: shot.velocity.x = readF32(in); return true;
                    case shot_key::VY: shot.velocity.y = readF32(in); return true;
                    case shot_key::VZ: shot.velocity.z = readF32(in); return true;
                    default: return false;
                }
            });
            if (need == 7) shots.push_back(shot);
        }
        return true;
    });
    if (!read) return std::nullopt;
    return shots;
}

// ---- §7 Playing --------------------------------------------------------------------------

std::vector<uint8_t> encodePlayerState(const PlayerStateWire& state) {
    MapWriter w;
    w.f32(player_key::X, state.position.x);
    w.f32(player_key::Y, state.position.y);
    w.f32(player_key::Z, state.position.z);
    f32Unless0(w, player_key::VX, state.velocity.x);
    f32Unless0(w, player_key::VY, state.velocity.y);
    f32Unless0(w, player_key::VZ, state.velocity.z);
    f32Unless0(w, player_key::YAW, state.yaw);
    f32Unless0(w, player_key::PITCH, state.pitch);
    if (state.onGround) w.boolean(player_key::ON_GROUND, true);
    return w.finish();
}

std::optional<PlayerStateWire> decodePlayerState(std::span<const uint8_t> body) {
    CborIn in(body);
    PlayerStateWire state;
    int need = 0;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case player_key::X: state.position.x = readF32(in); need |= 1; return true;
            case player_key::Y: state.position.y = readF32(in); need |= 2; return true;
            case player_key::Z: state.position.z = readF32(in); need |= 4; return true;
            case player_key::VX: state.velocity.x = readF32(in); return true;
            case player_key::VY: state.velocity.y = readF32(in); return true;
            case player_key::VZ: state.velocity.z = readF32(in); return true;
            case player_key::YAW: state.yaw = readF32(in); return true;
            case player_key::PITCH: state.pitch = readF32(in); return true;
            case player_key::ON_GROUND: state.onGround = in.boolean(); return true;
            default: return false;
        }
    });
    if (!read || need != 7) return std::nullopt;
    if (!std::isfinite(state.position.x) || !std::isfinite(state.position.y) || !std::isfinite(state.position.z)) {
        return std::nullopt;
    }
    return state;
}

std::vector<uint8_t> encodeExpectChange(const ExpectChangeWire& change) {
    MapWriter w;
    w.sint(change_key::X, change.x);
    w.sint(change_key::Y, change.y);
    w.sint(change_key::Z, change.z);
    return w.finish();
}

std::optional<ExpectChangeWire> decodeExpectChange(std::span<const uint8_t> body) {
    CborIn in(body);
    ExpectChangeWire change;
    int need = 0;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case change_key::X: change.x = static_cast<int32_t>(in.sint()); need |= 1; return true;
            case change_key::Y: change.y = static_cast<int32_t>(in.sint()); need |= 2; return true;
            case change_key::Z: change.z = static_cast<int32_t>(in.sint()); need |= 4; return true;
            default: return false;
        }
    });
    if (!read || need != 7) return std::nullopt;
    return change;
}

std::vector<uint8_t> encodeViewHint(const ViewHintWire& hint) {
    MapWriter w;
    w.f32(hint_key::X, hint.x);
    w.f32(hint_key::Z, hint.z);
    return w.finish();
}

std::optional<ViewHintWire> decodeViewHint(std::span<const uint8_t> body) {
    CborIn in(body);
    ViewHintWire hint;
    int need = 0;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case hint_key::X: hint.x = readF32(in); need |= 1; return true;
            case hint_key::Z: hint.z = readF32(in); need |= 2; return true;
            default: return false;
        }
    });
    if (!read || need != 3 || !std::isfinite(hint.x) || !std::isfinite(hint.z)) return std::nullopt;
    return hint;
}

// ---- §8 Content --------------------------------------------------------------------------

std::vector<uint8_t> encodeContentChunk(const ContentChunkWire& chunk) {
    MapWriter w;
    w.bytes(chunk_key::SHA256, chunk.sha256);
    w.uint(chunk_key::OFFSET, chunk.offset);
    w.uint(chunk_key::TOTAL, chunk.total);
    w.bytes(chunk_key::DATA, chunk.data);
    return w.finish();
}

std::optional<ContentChunkWire> decodeContentChunk(std::span<const uint8_t> body) {
    CborIn in(body);
    ContentChunkWire chunk;
    int need = 0;
    bool read = in.fields([&](uint64_t key) {
        switch (key) {
            case chunk_key::SHA256: {
                auto hash = in.bytes();
                if (hash.size() != chunk.sha256.size()) return true;
                std::copy(hash.begin(), hash.end(), chunk.sha256.begin());
                need |= 1;
                return true;
            }
            case chunk_key::OFFSET: chunk.offset = in.uint(); need |= 2; return true;
            case chunk_key::TOTAL: chunk.total = in.uint(); need |= 4; return true;
            case chunk_key::DATA: chunk.data = in.bytes(); need |= 8; return true;
            default: return false;
        }
    });
    if (!read || need != 15) return std::nullopt;
    return chunk;
}

}  // namespace finevox::net
