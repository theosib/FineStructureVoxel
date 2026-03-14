#include "finevox/core/entity_state.hpp"
#include "finevox/core/entity.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/cbor.hpp"

namespace finevox {

// ============================================================================
// Constructors / Destructors
// ============================================================================

EntityState::EntityState() = default;
EntityState::~EntityState() = default;

EntityState::EntityState(const EntityState& other)
    : id(other.id)
    , entityType(other.entityType)
    , position(other.position)
    , velocity(other.velocity)
    , onGround(other.onGround)
    , yaw(other.yaw)
    , pitch(other.pitch)
    , animationId(other.animationId)
    , animationTime(other.animationTime)
    , inputSequence(other.inputSequence)
    , extra(other.extra ? other.extra->clone() : nullptr)
{}

EntityState& EntityState::operator=(const EntityState& other) {
    if (this != &other) {
        id = other.id;
        entityType = other.entityType;
        position = other.position;
        velocity = other.velocity;
        onGround = other.onGround;
        yaw = other.yaw;
        pitch = other.pitch;
        animationId = other.animationId;
        animationTime = other.animationTime;
        inputSequence = other.inputSequence;
        extra = other.extra ? other.extra->clone() : nullptr;
    }
    return *this;
}

EntityState::EntityState(EntityState&&) noexcept = default;
EntityState& EntityState::operator=(EntityState&&) noexcept = default;

// ============================================================================
// Factory
// ============================================================================

EntityState EntityState::fromEntity(const Entity& entity) {
    EntityState state;
    state.id = entity.id();
    state.entityType = static_cast<uint16_t>(entity.type());
    state.position = glm::dvec3(entity.position());
    state.velocity = glm::dvec3(entity.velocity());
    state.onGround = entity.isOnGround();
    state.yaw = entity.yaw();
    state.pitch = entity.pitch();
    state.animationId = entity.animationId();
    state.animationTime = entity.animationTime();

    // Copy entity's extensible data if present
    if (entity.entityData()) {
        state.extra = entity.entityData()->clone();
    }

    return state;
}

// ============================================================================
// CBOR Serialization
// ============================================================================

std::vector<uint8_t> EntityState::toCBOR() const {
    std::vector<uint8_t> out;

    // Count fields: 11 base fields + 1 if extra is present
    size_t fieldCount = 11;
    if (extra && !extra->empty()) ++fieldCount;

    cbor::encodeMapHeader(out, fieldCount);

    cbor::encodeString(out, "id");
    cbor::encodeInt(out, static_cast<int64_t>(id));

    cbor::encodeString(out, "entity_type");
    cbor::encodeInt(out, entityType);

    cbor::encodeString(out, "pos");
    cbor::encodeArrayHeader(out, 3);
    cbor::encodeDouble(out, position.x);
    cbor::encodeDouble(out, position.y);
    cbor::encodeDouble(out, position.z);

    cbor::encodeString(out, "vel");
    cbor::encodeArrayHeader(out, 3);
    cbor::encodeDouble(out, velocity.x);
    cbor::encodeDouble(out, velocity.y);
    cbor::encodeDouble(out, velocity.z);

    cbor::encodeString(out, "on_ground");
    cbor::encodeBool(out, onGround);

    cbor::encodeString(out, "yaw");
    cbor::encodeDouble(out, yaw);

    cbor::encodeString(out, "pitch");
    cbor::encodeDouble(out, pitch);

    cbor::encodeString(out, "anim_id");
    cbor::encodeInt(out, animationId);

    cbor::encodeString(out, "anim_time");
    cbor::encodeDouble(out, animationTime);

    cbor::encodeString(out, "input_seq");
    cbor::encodeInt(out, static_cast<int64_t>(inputSequence));

    cbor::encodeString(out, "version");
    cbor::encodeInt(out, 1);

    // Extra data (DataContainer as nested CBOR bytes)
    if (extra && !extra->empty()) {
        cbor::encodeString(out, "extra");
        auto extraBytes = extra->toCBOR();
        cbor::encodeBytes(out, extraBytes);
    }

    return out;
}

// Helper: reinterpret uint64_t bits from readHeader() as double.
// CBOR float64 (additional=27) stores 8 bytes that readHeader() returns as uint64_t.
static double bitsToDouble(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

// Helper: read a CBOR-encoded double value (handles both float64 and integer fallback)
static double readDouble(cbor::Decoder& dec) {
    auto [mt, val] = dec.readHeader();
    if (mt == cbor::SIMPLE) return bitsToDouble(val);
    if (mt == cbor::UNSIGNED_INT) return static_cast<double>(val);
    if (mt == cbor::NEGATIVE_INT) return static_cast<double>(-1 - static_cast<int64_t>(val));
    return 0.0;
}

EntityState EntityState::fromCBOR(std::span<const uint8_t> data) {
    EntityState state;
    cbor::Decoder dec(data);

    auto [mt, mapSize] = dec.readHeader();
    if (mt != cbor::MAP) return state;

    for (uint64_t i = 0; i < mapSize; ++i) {
        auto [keyMt, keyLen] = dec.readHeader();
        if (keyMt != cbor::TEXT_STRING) { dec.skipValue(); continue; }
        std::string key = dec.readString(keyLen);

        if (key == "id") {
            state.id = static_cast<EntityId>(dec.readInt());
        } else if (key == "entity_type") {
            state.entityType = static_cast<uint16_t>(dec.readInt());
        } else if (key == "pos") {
            auto [aMt, aLen] = dec.readHeader();
            if (aMt == cbor::ARRAY && aLen >= 3) {
                state.position.x = readDouble(dec);
                state.position.y = readDouble(dec);
                state.position.z = readDouble(dec);
            }
        } else if (key == "vel") {
            auto [aMt, aLen] = dec.readHeader();
            if (aMt == cbor::ARRAY && aLen >= 3) {
                state.velocity.x = readDouble(dec);
                state.velocity.y = readDouble(dec);
                state.velocity.z = readDouble(dec);
            }
        } else if (key == "on_ground") {
            auto [bMt, bVal] = dec.readHeader();
            state.onGround = (bMt == cbor::SIMPLE && bVal == cbor::TRUE_VALUE);
        } else if (key == "yaw") {
            state.yaw = static_cast<float>(readDouble(dec));
        } else if (key == "pitch") {
            state.pitch = static_cast<float>(readDouble(dec));
        } else if (key == "anim_id") {
            state.animationId = static_cast<uint8_t>(dec.readInt());
        } else if (key == "anim_time") {
            state.animationTime = static_cast<float>(readDouble(dec));
        } else if (key == "input_seq") {
            state.inputSequence = static_cast<uint64_t>(dec.readInt());
        } else if (key == "extra") {
            auto [bMt, bLen] = dec.readHeader();
            if (bMt == cbor::BYTE_STRING) {
                auto extraBytes = dec.readBytes(bLen);
                state.extra = DataContainer::fromCBOR(extraBytes);
            }
        } else {
            dec.skipValue();
        }
    }

    return state;
}

}  // namespace finevox
