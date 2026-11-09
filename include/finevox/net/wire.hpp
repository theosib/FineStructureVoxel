#pragma once

/**
 * @file wire.hpp
 * @brief CBOR as the world's messages use it (PROTOCOL.md §3): maps with small integer keys
 *
 * Every world message is one map. A key under 24 costs a byte, and a field at its default is left
 * out, so a map is about as small as fixed binary was, and a field can be added, or dropped, without
 * an older peer misreading the rest. Reading is for bytes off the network: every length is checked
 * against what's there, nesting is limited, and a value of the wrong type fails the read.
 */

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace finevox::net {

/// Raw CBOR values, appended to a buffer
class CborOut {
public:
    explicit CborOut(std::vector<uint8_t>& out) : out_(out) {}

    void uint(uint64_t v);
    void sint(int64_t v);
    void f32(float v);
    void f64(double v);
    void boolean(bool v);
    void bytes(std::span<const uint8_t> v);
    void text(std::string_view v);
    void arrayHeader(uint64_t count);
    void mapHeader(uint64_t count);
    /// Bytes that are already one whole CBOR value
    void raw(std::span<const uint8_t> v) { out_.insert(out_.end(), v.begin(), v.end()); }

private:
    void head(uint8_t major, uint64_t value);
    std::vector<uint8_t>& out_;
};

/// One map with unsigned integer keys. Fields go in as they're added, and finish() puts the map's
/// header in front. Each field's call writes its key and exactly one value.
class MapWriter {
public:
    void uint(uint32_t key, uint64_t v) { field(key).uint(v); }
    void sint(uint32_t key, int64_t v) { field(key).sint(v); }
    void f32(uint32_t key, float v) { field(key).f32(v); }
    void f64(uint32_t key, double v) { field(key).f64(v); }
    void boolean(uint32_t key, bool v) { field(key).boolean(v); }
    void bytes(uint32_t key, std::span<const uint8_t> v) { field(key).bytes(v); }
    void text(uint32_t key, std::string_view v) { field(key).text(v); }
    void map(uint32_t key, const MapWriter& inner);

    /// An array of `count` values, written by `fill(CborOut&)`: the caller writes exactly that many
    template <typename Fill>
    void array(uint32_t key, uint64_t count, Fill&& fill) {
        CborOut out = field(key);
        out.arrayHeader(count);
        fill(out);
    }

    /// An array of maps, the i'th written by `fill(i, MapWriter&)`
    template <typename Fill>
    void maps(uint32_t key, size_t count, Fill&& fill) {
        CborOut out = field(key);
        out.arrayHeader(count);
        MapWriter item;
        for (size_t i = 0; i < count; ++i) {
            item.clear();
            fill(i, item);
            item.appendTo(body_);
        }
    }

    void clear() {
        body_.clear();
        count_ = 0;
    }
    void appendTo(std::vector<uint8_t>& out) const;
    [[nodiscard]] std::vector<uint8_t> finish() const;

private:
    CborOut field(uint32_t key);
    std::vector<uint8_t> body_;
    uint64_t count_ = 0;
};

/// Reads CBOR from the network. Past the end, or on a value of the wrong type, ok() goes false and
/// stays false, and reads give zeroes.
class CborIn {
public:
    static constexpr int MAX_DEPTH = 16;

    explicit CborIn(std::span<const uint8_t> data) : data_(data) {}

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] bool atEnd() const { return offset_ >= data_.size(); }

    uint64_t uint();
    int64_t sint();
    /// Any number: an integer, or a float of any width
    double number();
    bool boolean();
    std::span<const uint8_t> bytes();
    std::string_view text();
    /// An array's length. Each element takes a byte at least, so one claiming more than the bytes
    /// left fails here.
    uint64_t array();
    uint64_t map();
    /// Pass over one value of any kind
    void skip() { skipAt(0); }

    /// A map with integer keys: `fn(key)` reads that key's value and returns true, or returns false
    /// to have it skipped. Keys that aren't integers are skipped with their values.
    template <typename Fn>
    bool fields(Fn&& fn) {
        uint64_t count = map();
        for (uint64_t i = 0; ok_ && i < count; ++i) {
            if (peekMajor() != 0) {
                skip();
                skip();
                continue;
            }
            uint64_t key = uint();
            if (!ok_) break;
            if (!fn(key)) skip();
        }
        return ok_;
    }

private:
    int peekMajor() const;
    bool head(uint8_t& major, uint8_t& info, uint64_t& value);
    std::span<const uint8_t> take(uint64_t size);
    void skipAt(int depth);
    void fail() { ok_ = false; }

    std::span<const uint8_t> data_;
    size_t offset_ = 0;
    bool ok_ = true;
};

}  // namespace finevox::net
