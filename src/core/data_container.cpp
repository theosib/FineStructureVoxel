#include "finevox/core/data_container.hpp"

namespace finevox {

bool DataContainer::has(DataKey key) const {
    return data_.contains(key);
}

void DataContainer::remove(DataKey key) {
    data_.erase(key);
}

const DataValue* DataContainer::getRaw(DataKey key) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return nullptr;
    }
    return &it->second;
}

DataContainer* DataContainer::getChild(DataKey key) {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    auto* ptr = std::get_if<std::unique_ptr<DataContainer>>(&it->second);
    if (!ptr || !*ptr) return nullptr;
    return ptr->get();
}

const DataContainer* DataContainer::getChild(DataKey key) const {
    auto it = data_.find(key);
    if (it == data_.end()) return nullptr;
    auto* ptr = std::get_if<std::unique_ptr<DataContainer>>(&it->second);
    if (!ptr || !*ptr) return nullptr;
    return ptr->get();
}

DataContainer& DataContainer::getOrCreateChild(DataKey key) {
    auto it = data_.find(key);
    if (it != data_.end()) {
        auto* ptr = std::get_if<std::unique_ptr<DataContainer>>(&it->second);
        if (ptr && *ptr) {
            return **ptr;
        }
    }
    // Create new nested DataContainer
    auto child = std::make_unique<DataContainer>();
    auto& ref = *child;
    data_[key] = std::move(child);
    return ref;
}

DataValue DataContainer::cloneValue(const DataValue& value) {
    return std::visit([](const auto& v) -> DataValue {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return std::monostate{};
        } else if constexpr (std::is_same_v<T, std::unique_ptr<DataContainer>>) {
            // Deep copy nested container
            if (v) {
                return v->clone();
            }
            return std::unique_ptr<DataContainer>{};
        } else {
            // Copy primitives and vectors
            return v;
        }
    }, value);
}

std::unique_ptr<DataContainer> DataContainer::clone() const {
    auto result = std::make_unique<DataContainer>();

    for (const auto& [key, value] : data_) {
        result->data_[key] = cloneValue(value);
    }

    return result;
}

// ============================================================================
// CBOR Serialization
// ============================================================================

// CBOR major types
namespace cbor {
    constexpr uint8_t UNSIGNED_INT = 0;
    constexpr uint8_t NEGATIVE_INT = 1;
    constexpr uint8_t BYTE_STRING = 2;
    constexpr uint8_t TEXT_STRING = 3;
    constexpr uint8_t ARRAY = 4;
    constexpr uint8_t MAP = 5;
    constexpr uint8_t TAG = 6;
    constexpr uint8_t SIMPLE = 7;

    // Simple values
    constexpr uint8_t FALSE_VALUE = 20;
    constexpr uint8_t TRUE_VALUE = 21;
    constexpr uint8_t NULL_VALUE = 22;
    // constexpr uint8_t FLOAT16 = 25;  // Not used - we only encode float64
    // constexpr uint8_t FLOAT32 = 26;  // Not used - we only encode float64
    constexpr uint8_t FLOAT64 = 27;

    // Application-defined tags (IANA "first come first served" range)
    // Tag 39 is unassigned - we use it for interned strings
    constexpr uint64_t TAG_INTERNED_STRING = 39;

    // Encode a type and value into CBOR header bytes
    void encodeHeader(std::vector<uint8_t>& out, uint8_t majorType, uint64_t value) {
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

    void encodeInt(std::vector<uint8_t>& out, int64_t value) {
        if (value >= 0) {
            encodeHeader(out, UNSIGNED_INT, static_cast<uint64_t>(value));
        } else {
            encodeHeader(out, NEGATIVE_INT, static_cast<uint64_t>(-1 - value));
        }
    }

    void encodeDouble(std::vector<uint8_t>& out, double value) {
        out.push_back((SIMPLE << 5) | FLOAT64);
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        for (int i = 7; i >= 0; --i) {
            out.push_back(static_cast<uint8_t>(bits >> (i * 8)));
        }
    }

    void encodeString(std::vector<uint8_t>& out, std::string_view str) {
        encodeHeader(out, TEXT_STRING, str.size());
        out.insert(out.end(), str.begin(), str.end());
    }

    void encodeBytes(std::vector<uint8_t>& out, const std::vector<uint8_t>& bytes) {
        encodeHeader(out, BYTE_STRING, bytes.size());
        out.insert(out.end(), bytes.begin(), bytes.end());
    }

    void encodeNull(std::vector<uint8_t>& out) {
        out.push_back((SIMPLE << 5) | NULL_VALUE);
    }

    // Encode a semantic tag (followed by the tagged value)
    void encodeTag(std::vector<uint8_t>& out, uint64_t tag) {
        encodeHeader(out, TAG, tag);
    }

    // Decode helpers
    struct Decoder {
        const uint8_t* data;
        size_t size;
        size_t pos = 0;

        bool hasMore() const { return pos < size; }

        uint8_t peek() const {
            if (pos >= size) return 0;
            return data[pos];
        }

        uint8_t read() {
            if (pos >= size) return 0;
            return data[pos++];
        }

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
            for (uint64_t i = 0; i < length && pos < size; ++i) {
                result.push_back(static_cast<char>(read()));
            }
            return result;
        }

        std::vector<uint8_t> readBytes(uint64_t length) {
            std::vector<uint8_t> result;
            result.reserve(length);
            for (uint64_t i = 0; i < length && pos < size; ++i) {
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
    };

}  // namespace cbor

// Forward declaration for recursive encoding
static void encodeValue(std::vector<uint8_t>& out, const DataValue& value);

static void encodeContainer(std::vector<uint8_t>& out, const DataContainer& container) {
    cbor::encodeHeader(out, cbor::MAP, container.size());

    container.forEach([&out](DataKey key, const DataValue& value) {
        // Encode key as string
        std::string_view keyStr = lookupKey(key);
        cbor::encodeString(out, keyStr);

        // Encode value
        encodeValue(out, value);
    });
}

static void encodeValue(std::vector<uint8_t>& out, const DataValue& value) {
    std::visit([&out](const auto& v) {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            cbor::encodeNull(out);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            cbor::encodeInt(out, v);
        } else if constexpr (std::is_same_v<T, double>) {
            cbor::encodeDouble(out, v);
        } else if constexpr (std::is_same_v<T, std::string>) {
            cbor::encodeString(out, v);
        } else if constexpr (std::is_same_v<T, InternedString>) {
            // Interned strings are tagged so they get re-interned on load
            cbor::encodeTag(out, cbor::TAG_INTERNED_STRING);
            cbor::encodeString(out, v.str());
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            cbor::encodeBytes(out, v);
        } else if constexpr (std::is_same_v<T, std::unique_ptr<DataContainer>>) {
            if (v) {
                encodeContainer(out, *v);
            } else {
                cbor::encodeNull(out);
            }
        } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
            cbor::encodeHeader(out, cbor::ARRAY, v.size());
            for (int64_t item : v) {
                cbor::encodeInt(out, item);
            }
        } else if constexpr (std::is_same_v<T, std::vector<double>>) {
            cbor::encodeHeader(out, cbor::ARRAY, v.size());
            for (double item : v) {
                cbor::encodeDouble(out, item);
            }
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            cbor::encodeHeader(out, cbor::ARRAY, v.size());
            for (const std::string& item : v) {
                cbor::encodeString(out, item);
            }
        }
    }, value);
}

std::vector<uint8_t> DataContainer::toCBOR() const {
    std::vector<uint8_t> result;
    encodeContainer(result, *this);
    return result;
}

// Forward declaration for recursive decoding
static DataValue decodeValue(cbor::Decoder& decoder);

static std::unique_ptr<DataContainer> decodeContainer(cbor::Decoder& decoder) {
    auto [majorType, count] = decoder.readHeader();

    if (majorType != cbor::MAP) {
        return nullptr;
    }

    auto container = std::make_unique<DataContainer>();

    for (uint64_t i = 0; i < count; ++i) {
        // Read key
        auto [keyType, keyLen] = decoder.readHeader();
        if (keyType != cbor::TEXT_STRING) {
            // Skip invalid key
            decoder.readBytes(keyLen);
            continue;
        }
        std::string keyStr = decoder.readString(keyLen);
        DataKey key = internKey(keyStr);

        // Read value
        DataValue value = decodeValue(decoder);
        container->set(key, std::move(value));
    }

    return container;
}

static DataValue decodeValue(cbor::Decoder& decoder) {
    uint8_t initial = decoder.peek();
    uint8_t majorType = initial >> 5;
    uint8_t additional = initial & 0x1F;

    switch (majorType) {
        case cbor::UNSIGNED_INT: {
            auto [mt, value] = decoder.readHeader();
            return static_cast<int64_t>(value);
        }

        case cbor::NEGATIVE_INT: {
            auto [mt, value] = decoder.readHeader();
            return static_cast<int64_t>(-1 - static_cast<int64_t>(value));
        }

        case cbor::BYTE_STRING: {
            auto [mt, length] = decoder.readHeader();
            return decoder.readBytes(length);
        }

        case cbor::TEXT_STRING: {
            auto [mt, length] = decoder.readHeader();
            return decoder.readString(length);
        }

        case cbor::ARRAY: {
            auto [mt, count] = decoder.readHeader();

            // Peek at first element to determine array type
            if (count == 0) {
                return std::vector<int64_t>{};
            }

            uint8_t firstType = decoder.peek() >> 5;
            uint8_t firstAdditional = decoder.peek() & 0x1F;

            if (firstType == cbor::UNSIGNED_INT || firstType == cbor::NEGATIVE_INT) {
                std::vector<int64_t> arr;
                arr.reserve(count);
                for (uint64_t i = 0; i < count; ++i) {
                    auto val = decodeValue(decoder);
                    if (auto* intVal = std::get_if<int64_t>(&val)) {
                        arr.push_back(*intVal);
                    }
                }
                return arr;
            } else if (firstType == cbor::SIMPLE && firstAdditional == cbor::FLOAT64) {
                std::vector<double> arr;
                arr.reserve(count);
                for (uint64_t i = 0; i < count; ++i) {
                    auto val = decodeValue(decoder);
                    if (auto* dblVal = std::get_if<double>(&val)) {
                        arr.push_back(*dblVal);
                    }
                }
                return arr;
            } else if (firstType == cbor::TEXT_STRING) {
                std::vector<std::string> arr;
                arr.reserve(count);
                for (uint64_t i = 0; i < count; ++i) {
                    auto val = decodeValue(decoder);
                    if (auto* strVal = std::get_if<std::string>(&val)) {
                        arr.push_back(std::move(*strVal));
                    }
                }
                return arr;
            } else {
                // Skip unsupported array types
                for (uint64_t i = 0; i < count; ++i) {
                    decodeValue(decoder);
                }
                return std::monostate{};
            }
        }

        case cbor::MAP: {
            return decodeContainer(decoder);
        }

        case cbor::TAG: {
            auto [mt, tagValue] = decoder.readHeader();

            if (tagValue == cbor::TAG_INTERNED_STRING) {
                // Tagged interned string - read the string and intern it
                auto [strType, strLen] = decoder.readHeader();
                if (strType == cbor::TEXT_STRING) {
                    std::string str = decoder.readString(strLen);
                    return InternedString(str);
                }
            }
            // Unknown tag - skip the tagged value and return null
            decodeValue(decoder);
            return std::monostate{};
        }

        case cbor::SIMPLE: {
            if (additional == cbor::NULL_VALUE ||
                additional == cbor::FALSE_VALUE ||
                additional == cbor::TRUE_VALUE) {
                decoder.read();
                if (additional == cbor::FALSE_VALUE) return static_cast<int64_t>(0);
                if (additional == cbor::TRUE_VALUE) return static_cast<int64_t>(1);
                return std::monostate{};
            } else if (additional == cbor::FLOAT64) {
                decoder.read(); // Skip the initial byte
                return decoder.readFloat64();
            } else {
                decoder.read();
                return std::monostate{};
            }
        }

        default:
            decoder.read();
            return std::monostate{};
    }
}

std::unique_ptr<DataContainer> DataContainer::fromCBOR(std::span<const uint8_t> data) {
    if (data.empty()) {
        return std::make_unique<DataContainer>();
    }

    cbor::Decoder decoder{data.data(), data.size(), 0};
    return decodeContainer(decoder);
}

}  // namespace finevox
