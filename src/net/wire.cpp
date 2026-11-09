#include "finevox/net/wire.hpp"

#include <cmath>
#include <cstring>

namespace finevox::net {

namespace {

constexpr uint8_t UNSIGNED = 0;
constexpr uint8_t NEGATIVE = 1;
constexpr uint8_t BYTES = 2;
constexpr uint8_t TEXT = 3;
constexpr uint8_t ARRAY = 4;
constexpr uint8_t MAP = 5;
constexpr uint8_t TAG = 6;
constexpr uint8_t SIMPLE = 7;

constexpr uint8_t FALSE_VALUE = 20;
constexpr uint8_t TRUE_VALUE = 21;
constexpr uint8_t HALF = 25;
constexpr uint8_t SINGLE = 26;
constexpr uint8_t DOUBLE = 27;

float halfToFloat(uint16_t h) {
    int exponent = (h >> 10) & 0x1F;
    int mantissa = h & 0x3FF;
    float value;
    if (exponent == 0) {
        value = std::ldexp(static_cast<float>(mantissa), -24);
    } else if (exponent == 31) {
        value = mantissa == 0 ? INFINITY : NAN;
    } else {
        value = std::ldexp(static_cast<float>(mantissa + 1024), exponent - 25);
    }
    return (h & 0x8000) ? -value : value;
}

}  // namespace

// ---- CborOut ----------------------------------------------------------------------------

void CborOut::head(uint8_t major, uint64_t value) {
    uint8_t m = static_cast<uint8_t>(major << 5);
    if (value < 24) {
        out_.push_back(m | static_cast<uint8_t>(value));
    } else if (value <= 0xFF) {
        out_.push_back(m | 24);
        out_.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFF) {
        out_.push_back(m | 25);
        for (int shift = 8; shift >= 0; shift -= 8) out_.push_back(static_cast<uint8_t>(value >> shift));
    } else if (value <= 0xFFFFFFFFu) {
        out_.push_back(m | 26);
        for (int shift = 24; shift >= 0; shift -= 8) out_.push_back(static_cast<uint8_t>(value >> shift));
    } else {
        out_.push_back(m | 27);
        for (int shift = 56; shift >= 0; shift -= 8) out_.push_back(static_cast<uint8_t>(value >> shift));
    }
}

void CborOut::uint(uint64_t v) { head(UNSIGNED, v); }

void CborOut::sint(int64_t v) {
    if (v >= 0) {
        head(UNSIGNED, static_cast<uint64_t>(v));
    } else {
        head(NEGATIVE, static_cast<uint64_t>(-1 - v));
    }
}

void CborOut::f32(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    out_.push_back((SIMPLE << 5) | SINGLE);
    for (int shift = 24; shift >= 0; shift -= 8) out_.push_back(static_cast<uint8_t>(bits >> shift));
}

void CborOut::f64(double v) {
    uint64_t bits;
    std::memcpy(&bits, &v, 8);
    out_.push_back((SIMPLE << 5) | DOUBLE);
    for (int shift = 56; shift >= 0; shift -= 8) out_.push_back(static_cast<uint8_t>(bits >> shift));
}

void CborOut::boolean(bool v) { out_.push_back((SIMPLE << 5) | (v ? TRUE_VALUE : FALSE_VALUE)); }

void CborOut::bytes(std::span<const uint8_t> v) {
    head(BYTES, v.size());
    out_.insert(out_.end(), v.begin(), v.end());
}

void CborOut::text(std::string_view v) {
    head(TEXT, v.size());
    out_.insert(out_.end(), v.begin(), v.end());
}

void CborOut::arrayHeader(uint64_t count) { head(ARRAY, count); }
void CborOut::mapHeader(uint64_t count) { head(MAP, count); }

// ---- MapWriter --------------------------------------------------------------------------

CborOut MapWriter::field(uint32_t key) {
    ++count_;
    CborOut out(body_);
    out.uint(key);
    return out;
}

void MapWriter::map(uint32_t key, const MapWriter& inner) {
    field(key);
    inner.appendTo(body_);
}

void MapWriter::appendTo(std::vector<uint8_t>& out) const {
    CborOut(out).mapHeader(count_);
    out.insert(out.end(), body_.begin(), body_.end());
}

std::vector<uint8_t> MapWriter::finish() const {
    std::vector<uint8_t> out;
    out.reserve(body_.size() + 5);
    appendTo(out);
    return out;
}

// ---- CborIn -----------------------------------------------------------------------------

int CborIn::peekMajor() const {
    if (!ok_ || offset_ >= data_.size()) return -1;
    return data_[offset_] >> 5;
}

std::span<const uint8_t> CborIn::take(uint64_t size) {
    if (!ok_ || size > data_.size() - offset_) {
        fail();
        return {};
    }
    auto view = data_.subspan(offset_, size);
    offset_ += size;
    return view;
}

bool CborIn::head(uint8_t& major, uint8_t& info, uint64_t& value) {
    auto first = take(1);
    if (!ok_) return false;
    major = first[0] >> 5;
    info = first[0] & 0x1F;
    if (info < 24) {
        value = info;
        return true;
    }
    int width = info == 24 ? 1 : info == 25 ? 2 : info == 26 ? 4 : info == 27 ? 8 : 0;
    if (width == 0) {
        fail();  // Indefinite lengths and reserved values aren't used here
        return false;
    }
    auto rest = take(width);
    if (!ok_) return false;
    value = 0;
    for (uint8_t b : rest) value = (value << 8) | b;
    return true;
}

uint64_t CborIn::uint() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || major != UNSIGNED) {
        fail();
        return 0;
    }
    return value;
}

int64_t CborIn::sint() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || (major != UNSIGNED && major != NEGATIVE) || value > INT64_MAX) {
        fail();
        return 0;
    }
    return major == UNSIGNED ? static_cast<int64_t>(value) : -1 - static_cast<int64_t>(value);
}

double CborIn::number() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value)) return 0.0;
    if (major == UNSIGNED) return static_cast<double>(value);
    if (major == NEGATIVE) return -1.0 - static_cast<double>(value);
    if (major == SIMPLE) {
        if (info == HALF) return halfToFloat(static_cast<uint16_t>(value));
        if (info == SINGLE) {
            uint32_t bits = static_cast<uint32_t>(value);
            float f;
            std::memcpy(&f, &bits, 4);
            return f;
        }
        if (info == DOUBLE) {
            double d;
            std::memcpy(&d, &value, 8);
            return d;
        }
    }
    fail();
    return 0.0;
}

bool CborIn::boolean() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || major != SIMPLE || (info != TRUE_VALUE && info != FALSE_VALUE)) {
        fail();
        return false;
    }
    return info == TRUE_VALUE;
}

std::span<const uint8_t> CborIn::bytes() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || major != BYTES) {
        fail();
        return {};
    }
    return take(value);
}

std::string_view CborIn::text() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || major != TEXT) {
        fail();
        return {};
    }
    auto view = take(value);
    return {reinterpret_cast<const char*>(view.data()), view.size()};
}

uint64_t CborIn::array() {
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value) || major != ARRAY || value > data_.size() - offset_) {
        fail();
        return 0;
    }
    return value;
}

uint64_t CborIn::map() {
    uint8_t major, info;
    uint64_t value;
    // Each entry is two values, so two bytes at least
    if (!head(major, info, value) || major != MAP || value > (data_.size() - offset_) / 2) {
        fail();
        return 0;
    }
    return value;
}

void CborIn::skipAt(int depth) {
    if (depth > MAX_DEPTH) {
        fail();
        return;
    }
    uint8_t major, info;
    uint64_t value;
    if (!head(major, info, value)) return;
    switch (major) {
        case UNSIGNED:
        case NEGATIVE:
        case SIMPLE:
            break;  // The head was all of it, floats included
        case BYTES:
        case TEXT:
            take(value);
            break;
        case ARRAY:
            if (value > data_.size() - offset_) return fail();
            for (uint64_t i = 0; ok_ && i < value; ++i) skipAt(depth + 1);
            break;
        case MAP:
            if (value > (data_.size() - offset_) / 2) return fail();
            for (uint64_t i = 0; ok_ && i < value * 2; ++i) skipAt(depth + 1);
            break;
        case TAG:
            skipAt(depth + 1);
            break;
    }
}

}  // namespace finevox::net
