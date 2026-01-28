#pragma once

/**
 * @file cbor.hpp
 * @brief CBOR encoding and decoding for serialization
 *
 * Design: [11-persistence.md] §11.2 CBOR Format
 */

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace finevox {
namespace cbor {

// CBOR major types
constexpr uint8_t UNSIGNED_INT = 0;
constexpr uint8_t NEGATIVE_INT = 1;
constexpr uint8_t BYTE_STRING = 2;
constexpr uint8_t TEXT_STRING = 3;
constexpr uint8_t ARRAY = 4;
constexpr uint8_t MAP = 5;
constexpr uint8_t SIMPLE = 7;

// Simple values
constexpr uint8_t FALSE_VALUE = 20;
constexpr uint8_t TRUE_VALUE = 21;
constexpr uint8_t NULL_VALUE = 22;
constexpr uint8_t FLOAT64 = 27;

// ============================================================================
// Encoding
// ============================================================================

// Encode a CBOR header (major type + argument)
inline void encodeHeader(std::vector<uint8_t>& out, uint8_t majorType, uint64_t value) {
    uint8_t mt = majorType << 5;

    if (value < 24) {
        out.push_back(mt | static_cast<uint8_t>(value));
    } else if (value <= 0xFF) {
        out.push_back(mt | 24);
        out.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFF) {
        out.push_back(mt | 25);
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value));
    } else if (value <= 0xFFFFFFFF) {
        out.push_back(mt | 26);
        out.push_back(static_cast<uint8_t>(value >> 24));
        out.push_back(static_cast<uint8_t>(value >> 16));
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value));
    } else {
        out.push_back(mt | 27);
        out.push_back(static_cast<uint8_t>(value >> 56));
        out.push_back(static_cast<uint8_t>(value >> 48));
        out.push_back(static_cast<uint8_t>(value >> 40));
        out.push_back(static_cast<uint8_t>(value >> 32));
        out.push_back(static_cast<uint8_t>(value >> 24));
        out.push_back(static_cast<uint8_t>(value >> 16));
        out.push_back(static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value));
    }
}

inline void encodeInt(std::vector<uint8_t>& out, int64_t value) {
    if (value >= 0) {
        encodeHeader(out, UNSIGNED_INT, static_cast<uint64_t>(value));
    } else {
        encodeHeader(out, NEGATIVE_INT, static_cast<uint64_t>(-1 - value));
    }
}

inline void encodeDouble(std::vector<uint8_t>& out, double value) {
    out.push_back((SIMPLE << 5) | FLOAT64);
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    for (int i = 7; i >= 0; --i) {
        out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
    }
}

inline void encodeString(std::vector<uint8_t>& out, std::string_view str) {
    encodeHeader(out, TEXT_STRING, str.size());
    out.insert(out.end(), str.begin(), str.end());
}

inline void encodeBytes(std::vector<uint8_t>& out, std::span<const uint8_t> bytes) {
    encodeHeader(out, BYTE_STRING, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void encodeNull(std::vector<uint8_t>& out) {
    out.push_back((SIMPLE << 5) | NULL_VALUE);
}

inline void encodeBool(std::vector<uint8_t>& out, bool value) {
    out.push_back((SIMPLE << 5) | (value ? TRUE_VALUE : FALSE_VALUE));
}

// Start a map with known size
inline void encodeMapHeader(std::vector<uint8_t>& out, size_t count) {
    encodeHeader(out, MAP, count);
}

// Start an array with known size
inline void encodeArrayHeader(std::vector<uint8_t>& out, size_t count) {
    encodeHeader(out, ARRAY, count);
}

// ============================================================================
// Decoding
// ============================================================================

class Decoder {
public:
    Decoder(const uint8_t* data, size_t size) : data_(data), size_(size), pos_(0) {}
    Decoder(std::span<const uint8_t> span) : data_(span.data()), size_(span.size()), pos_(0) {}

    [[nodiscard]] bool hasMore() const { return pos_ < size_; }
    [[nodiscard]] size_t position() const { return pos_; }
    [[nodiscard]] size_t remaining() const { return size_ - pos_; }

    [[nodiscard]] uint8_t peek() const {
        if (pos_ >= size_) return 0;
        return data_[pos_];
    }

    uint8_t read() {
        if (pos_ >= size_) return 0;
        return data_[pos_++];
    }

    // Read CBOR header, returns (major type, argument value)
    std::pair<uint8_t, uint64_t> readHeader() {
        uint8_t initial = read();
        uint8_t majorType = initial >> 5;
        uint8_t additional = initial & 0x1F;

        uint64_t value;
        if (additional < 24) {
            value = additional;
        } else if (additional == 24) {
            value = read();
        } else if (additional == 25) {
            value = (static_cast<uint64_t>(read()) << 8) | read();
        } else if (additional == 26) {
            value = (static_cast<uint64_t>(read()) << 24) |
                    (static_cast<uint64_t>(read()) << 16) |
                    (static_cast<uint64_t>(read()) << 8) |
                    read();
        } else if (additional == 27) {
            value = (static_cast<uint64_t>(read()) << 56) |
                    (static_cast<uint64_t>(read()) << 48) |
                    (static_cast<uint64_t>(read()) << 40) |
                    (static_cast<uint64_t>(read()) << 32) |
                    (static_cast<uint64_t>(read()) << 24) |
                    (static_cast<uint64_t>(read()) << 16) |
                    (static_cast<uint64_t>(read()) << 8) |
                    read();
        } else {
            value = 0; // Indefinite or reserved
        }

        return {majorType, value};
    }

    std::string readString(uint64_t length) {
        std::string result;
        result.reserve(length);
        for (uint64_t i = 0; i < length && pos_ < size_; ++i) {
            result.push_back(static_cast<char>(read()));
        }
        return result;
    }

    std::vector<uint8_t> readBytes(uint64_t length) {
        std::vector<uint8_t> result;
        result.reserve(length);
        for (uint64_t i = 0; i < length && pos_ < size_; ++i) {
            result.push_back(read());
        }
        return result;
    }

    double readFloat64() {
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits = (bits << 8) | read();
        }
        double result;
        std::memcpy(&result, &bits, sizeof(result));
        return result;
    }

    // Read an integer (handles both unsigned and negative)
    int64_t readInt() {
        auto [majorType, value] = readHeader();
        if (majorType == UNSIGNED_INT) {
            return static_cast<int64_t>(value);
        } else if (majorType == NEGATIVE_INT) {
            return -1 - static_cast<int64_t>(value);
        }
        return 0;
    }

    // Skip a CBOR value (useful for unknown fields)
    void skipValue() {
        auto [majorType, value] = readHeader();
        switch (majorType) {
            case UNSIGNED_INT:
            case NEGATIVE_INT:
                // Already read
                break;
            case BYTE_STRING:
            case TEXT_STRING:
                pos_ += value;
                break;
            case ARRAY:
                for (uint64_t i = 0; i < value; ++i) {
                    skipValue();
                }
                break;
            case MAP:
                for (uint64_t i = 0; i < value; ++i) {
                    skipValue(); // key
                    skipValue(); // value
                }
                break;
            case SIMPLE:
                if ((value & 0x1F) == FLOAT64) {
                    pos_ += 8;
                }
                break;
        }
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_;
};

}  // namespace cbor
}  // namespace finevox
