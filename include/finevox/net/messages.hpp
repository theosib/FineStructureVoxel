#pragma once

/**
 * @file messages.hpp
 * @brief The world's messages (PROTOCOL.md §5 to §8), written and read in one place
 *
 * Each is a CBOR map with integer keys, the keys listed in PROTOCOL.md beside the message. A field
 * at its default is left out, and a reader gives a missing one its default, so a field can be added
 * without an older peer misreading anything. Decoders return nullopt for a message that's damaged,
 * or that lacks a field it can't do without.
 */

#include "finevox/net/protocol.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace finevox::net {

// ---- §5 World ----------------------------------------------------------------------------

/// BlockName, FluidName and EntityTypeName
struct NameWire {
    uint32_t id = 0;
    std::string name;
};
[[nodiscard]] std::vector<uint8_t> encodeName(const NameWire& name);
[[nodiscard]] std::optional<NameWire> decodeName(std::span<const uint8_t> body);

struct VisionWire {
    std::vector<uint8_t> cells;  ///< One a vision cell: 0 unknown, 1 explored, 2 visible
};
[[nodiscard]] std::vector<uint8_t> encodeVision(const VisionWire& vision);
[[nodiscard]] std::optional<VisionWire> decodeVision(std::span<const uint8_t> body, size_t limit);

struct TimeWire {
    uint64_t ticks = 0;
    bool held = false;    ///< The clock isn't moving, by the game's choice
    bool paused = false;  ///< The whole game is stopped
};
[[nodiscard]] std::vector<uint8_t> encodeTime(const TimeWire& time);
[[nodiscard]] std::optional<TimeWire> decodeTime(std::span<const uint8_t> body);

struct GhostCellWire {
    int32_t x = 0, y = 0, z = 0;
    uint32_t block = 0;  ///< A BlockName id
};
struct GhostWire {
    float built = 0.0f;
    std::vector<GhostCellWire> cells;
};
[[nodiscard]] std::vector<uint8_t> encodeGhosts(const std::vector<GhostWire>& ghosts);
[[nodiscard]] std::optional<std::vector<GhostWire>> decodeGhosts(std::span<const uint8_t> body);

// ---- §6 Entities -------------------------------------------------------------------------

struct EntitySpawnWire {
    uint64_t id = 0;
    uint32_t type = 0;
    uint16_t faction = 0;
    glm::dvec3 position{0.0};
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint8_t health = FULL_HEALTH;
};
[[nodiscard]] std::vector<uint8_t> encodeEntitySpawn(const EntitySpawnWire& spawn);
[[nodiscard]] std::optional<EntitySpawnWire> decodeEntitySpawn(std::span<const uint8_t> body);

struct EntityGoneWire {
    uint64_t id = 0;
    bool died = false;
};
[[nodiscard]] std::vector<uint8_t> encodeEntityGone(const EntityGoneWire& gone);
[[nodiscard]] std::optional<EntityGoneWire> decodeEntityGone(std::span<const uint8_t> body);

struct EntityMoveWire {
    uint64_t id = 0;
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    uint8_t animation = 0;
    bool onGround = false;
    uint8_t health = FULL_HEALTH;
};
struct EntityBatchWire {
    uint64_t tick = 0;
    std::vector<EntityMoveWire> entities;
};
[[nodiscard]] std::vector<uint8_t> encodeEntityBatch(const EntityBatchWire& batch);
/// An entry without an id or a position is dropped, not the batch
[[nodiscard]] std::optional<EntityBatchWire> decodeEntityBatch(std::span<const uint8_t> body);

struct ProjectileWire {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
};
[[nodiscard]] std::vector<uint8_t> encodeProjectiles(const std::vector<ProjectileWire>& shots);
[[nodiscard]] std::optional<std::vector<ProjectileWire>> decodeProjectiles(std::span<const uint8_t> body);

// ---- §7 Playing --------------------------------------------------------------------------

struct PlayerStateWire {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool onGround = false;
};
[[nodiscard]] std::vector<uint8_t> encodePlayerState(const PlayerStateWire& state);
/// Also nullopt for a position that isn't finite: it comes from a client
[[nodiscard]] std::optional<PlayerStateWire> decodePlayerState(std::span<const uint8_t> body);

struct ExpectChangeWire {
    int32_t x = 0, y = 0, z = 0;
};
[[nodiscard]] std::vector<uint8_t> encodeExpectChange(const ExpectChangeWire& change);
[[nodiscard]] std::optional<ExpectChangeWire> decodeExpectChange(std::span<const uint8_t> body);

struct ViewHintWire {
    float x = 0.0f, z = 0.0f;
};
[[nodiscard]] std::vector<uint8_t> encodeViewHint(const ViewHintWire& hint);
[[nodiscard]] std::optional<ViewHintWire> decodeViewHint(std::span<const uint8_t> body);

// ---- §8 Content --------------------------------------------------------------------------

/// `data` is a view: of the sender's buffer, or of the message it was read from
struct ContentChunkWire {
    std::array<uint8_t, 32> sha256{};
    uint64_t offset = 0;
    uint64_t total = 0;
    std::span<const uint8_t> data;
};
[[nodiscard]] std::vector<uint8_t> encodeContentChunk(const ContentChunkWire& chunk);
[[nodiscard]] std::optional<ContentChunkWire> decodeContentChunk(std::span<const uint8_t> body);

}  // namespace finevox::net
