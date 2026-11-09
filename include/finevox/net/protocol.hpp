#pragma once

/**
 * @file protocol.hpp
 * @brief The wire protocol's numbers and primitives
 *
 * docs/protocol/PROTOCOL.md is the spec, and a change here is a change there.
 */

#include <finenet/connection.hpp>
#include <finescript/value.h>

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace finevox::net {

inline constexpr int PROTOCOL_VERSION = 2;

/// An entity's health on the wire: a fraction in 255ths. Only an entity with nothing missing reads
/// FULL_HEALTH, so a scratch still shows.
inline constexpr uint8_t FULL_HEALTH = 255;
inline constexpr uint16_t DEFAULT_PORT = 7810;

// §7.2
inline constexpr size_t MAX_COMMAND_BYTES = 64 * 1024;
inline constexpr int MAX_COMMANDS_PER_SECOND = 120;
inline constexpr size_t MAX_CHAT_BYTES = 512;
inline constexpr int MAX_CHATS_PER_SECOND = 5;
inline constexpr size_t MAX_NAME_BYTES = 32;
inline constexpr size_t MAX_CONTENT_CHUNK = 256 * 1024;

/// Flags in a Time message
inline constexpr uint8_t TIME_FROZEN = 1;  // The clock is held
inline constexpr uint8_t TIME_PAUSED = 2;  // The whole game is

// §2.4
inline constexpr finenet::ChannelId CH_CONTROL = 0;
inline constexpr finenet::ChannelId CH_INPUT = 1;
inline constexpr finenet::ChannelId CH_ACTIONS = 2;
inline constexpr finenet::ChannelId CH_ENTITIES = 3;
inline constexpr finenet::ChannelId CH_WORLD = 5;
inline constexpr finenet::ChannelId CH_UI = 6;
inline constexpr finenet::ChannelId CH_ASSETS = 7;
inline constexpr finenet::ChannelId CH_CHAT = 8;

namespace msg {

// §4 Session
inline constexpr uint32_t Hello = 0x4001;
inline constexpr uint32_t Welcome = 0x4002;
inline constexpr uint32_t Reject = 0x4003;
inline constexpr uint32_t SetSetting = 0x4004;
inline constexpr uint32_t SetReady = 0x4005;
inline constexpr uint32_t Launch = 0x4006;
inline constexpr uint32_t LobbyState = 0x4007;
inline constexpr uint32_t Chat = 0x4008;
inline constexpr uint32_t Kick = 0x4009;
inline constexpr uint32_t Leave = 0x400A;
inline constexpr uint32_t GameStarting = 0x4010;
inline constexpr uint32_t Loaded = 0x4011;
inline constexpr uint32_t GameStart = 0x4012;
inline constexpr uint32_t GameEnded = 0x4013;
inline constexpr uint32_t SyncDone = 0x4014;
inline constexpr uint32_t Bound = 0x4015;

// §5 World
inline constexpr uint32_t BlockName = 0x20;
inline constexpr uint32_t FluidName = 0x21;
inline constexpr uint32_t EntityTypeName = 0x22;
inline constexpr uint32_t Subchunk = 0x23;
inline constexpr uint32_t Vision = 0x24;
inline constexpr uint32_t Time = 0x25;
inline constexpr uint32_t Ghosts = 0x26;

// §6 Entities
inline constexpr uint32_t EntitySpawn = 0x30;
inline constexpr uint32_t EntityGone = 0x31;
inline constexpr uint32_t EntityBatch = 0x32;
inline constexpr uint32_t Projectiles = 0x33;
inline constexpr uint32_t Burst = 0x34;

// §7 Playing
inline constexpr uint32_t PlayerState = 0x40;
inline constexpr uint32_t ExpectChange = 0x41;
inline constexpr uint32_t ViewHint = 0x42;
inline constexpr uint32_t Command = 0x4020;
inline constexpr uint32_t UiMessage = 0x4021;
inline constexpr uint32_t ClientState = 0x4022;

// §8 Content
inline constexpr uint32_t ContentManifest = 0x4030;
inline constexpr uint32_t ContentRequest = 0x4031;
inline constexpr uint32_t ContentChunk = 0x4032;
inline constexpr uint32_t ContentReady = 0x4033;

}  // namespace msg

/// The channels of §2.4, for both ends of a connection
[[nodiscard]] finenet::ConnectionConfig connectionConfig();

/// Appends the binary field types of §3
class ByteWriter {
public:
    explicit ByteWriter(std::vector<uint8_t>& out) : out_(out) {}

    void u8(uint8_t v) { out_.push_back(v); }
    void u16(uint16_t v) { little(v); }
    void u32(uint32_t v) { little(v); }
    void u64(uint64_t v) { little(v); }
    void f32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, 4);
        little(bits);
    }
    void f64(double v) {
        uint64_t bits;
        std::memcpy(&bits, &v, 8);
        little(bits);
    }
    void var(uint64_t v) {
        while (v >= 0x80) {
            out_.push_back(static_cast<uint8_t>(v) | 0x80);
            v >>= 7;
        }
        out_.push_back(static_cast<uint8_t>(v));
    }
    void svar(int64_t v) {
        var((static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63));
    }
    void str(std::string_view v) {
        var(v.size());
        out_.insert(out_.end(), v.begin(), v.end());
    }
    void bytes(std::span<const uint8_t> v) {
        var(v.size());
        out_.insert(out_.end(), v.begin(), v.end());
    }
    void raw(std::span<const uint8_t> v) { out_.insert(out_.end(), v.begin(), v.end()); }

private:
    template <typename T>
    void little(T v) {
        for (size_t i = 0; i < sizeof(T); ++i) out_.push_back(static_cast<uint8_t>(v >> (i * 8)));
    }

    std::vector<uint8_t>& out_;
};

/// Reads the binary field types of §3. Past the end, or on a bad value, ok() goes false and stays
/// false, and reads give zeroes: the input comes from the network, so nothing may assume it's sane.
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> data) : data_(data) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] size_t remaining() const { return data_.size() - offset_; }

    uint8_t u8() { return little<uint8_t>(); }
    uint16_t u16() { return little<uint16_t>(); }
    uint32_t u32() { return little<uint32_t>(); }
    uint64_t u64() { return little<uint64_t>(); }
    float f32() {
        uint32_t bits = little<uint32_t>();
        float v;
        std::memcpy(&v, &bits, 4);
        return v;
    }
    double f64() {
        uint64_t bits = little<uint64_t>();
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    }
    uint64_t var() {
        uint64_t value = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            uint8_t byte = u8();
            if (!ok_) return 0;
            value |= static_cast<uint64_t>(byte & 0x7F) << shift;
            if (!(byte & 0x80)) return value;
        }
        ok_ = false;
        return 0;
    }
    int64_t svar() {
        uint64_t v = var();
        return static_cast<int64_t>(v >> 1) ^ -static_cast<int64_t>(v & 1);
    }
    std::string_view str() {
        auto view = bytes();
        return {reinterpret_cast<const char*>(view.data()), view.size()};
    }
    std::span<const uint8_t> bytes() { return take(var()); }
    std::span<const uint8_t> take(uint64_t size) {
        if (!ok_ || size > remaining()) {
            ok_ = false;
            return {};
        }
        auto view = data_.subspan(offset_, size);
        offset_ += size;
        return view;
    }

private:
    template <typename T>
    T little() {
        if (!ok_ || remaining() < sizeof(T)) {
            ok_ = false;
            return 0;
        }
        T v = 0;
        for (size_t i = 0; i < sizeof(T); ++i) v = static_cast<T>(v | (static_cast<T>(data_[offset_ + i]) << (i * 8)));
        offset_ += sizeof(T);
        return v;
    }

    std::span<const uint8_t> data_;
    size_t offset_ = 0;
    bool ok_ = true;
};

/// A Value as a CBOR message body
[[nodiscard]] std::vector<uint8_t> encodeBody(const finescript::Value& value);

/// A CBOR message body as a Value. A nil if it won't parse: the input comes from the network.
[[nodiscard]] finescript::Value decodeBody(std::span<const uint8_t> body);

/// Shorthand for building and reading the maps of control messages
[[nodiscard]] uint32_t key(std::string_view name);
[[nodiscard]] finescript::Value field(const finescript::Value& map, std::string_view name);
[[nodiscard]] std::string textField(const finescript::Value& map, std::string_view name);
[[nodiscard]] int64_t intField(const finescript::Value& map, std::string_view name, int64_t fallback = 0);
[[nodiscard]] double numberField(const finescript::Value& map, std::string_view name, double fallback = 0.0);
[[nodiscard]] bool boolField(const finescript::Value& map, std::string_view name, bool fallback = false);
void setField(finescript::Value& map, std::string_view name, finescript::Value value);

/// A symbol, as the protocol spells phases and reasons
[[nodiscard]] finescript::Value symbol(std::string_view name);

}  // namespace finevox::net
