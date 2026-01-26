#pragma once

#include "finevox/string_interner.hpp"
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace finevox {

// Key type for DataContainer - uses string interning for compact storage
using DataKey = uint32_t;

// Intern a key string (thread-safe, returns same ID for same string)
inline DataKey internKey(std::string_view key) {
    return StringInterner::global().intern(key);
}

// Look up the original string for a key
inline std::string_view lookupKey(DataKey key) {
    return StringInterner::global().lookup(key);
}

// Forward declaration for recursive variant
class DataContainer;

// Type-safe variant for data values
// Covers common block metadata needs:
// - monostate: null/empty value
// - int64_t: all integers (power levels, counters, IDs)
// - double: all floats (progress, rotations)
// - string: text data (sign text, names)
// - vector<uint8_t>: binary blobs
// - unique_ptr<DataContainer>: nested compound data
// - vector<int64_t>: integer arrays
// - vector<double>: float arrays
// - vector<string>: string arrays
using DataValue = std::variant<
    std::monostate,
    int64_t,
    double,
    std::string,
    std::vector<uint8_t>,
    std::unique_ptr<DataContainer>,
    std::vector<int64_t>,
    std::vector<double>,
    std::vector<std::string>
>;

// Container for arbitrary block metadata
// Uses interned keys for compact storage and fast lookup
// Serializes to/from CBOR for disk storage
class DataContainer {
public:
    DataContainer() = default;
    ~DataContainer() = default;

    // Move-only (contains unique_ptr in variant)
    DataContainer(DataContainer&&) = default;
    DataContainer& operator=(DataContainer&&) = default;

    // No copy (use clone() for deep copy)
    DataContainer(const DataContainer&) = delete;
    DataContainer& operator=(const DataContainer&) = delete;

    // Deep copy
    [[nodiscard]] std::unique_ptr<DataContainer> clone() const;

    // ========================================================================
    // Access by interned key (fast path)
    // ========================================================================

    // Get value with default fallback
    // Returns defaultValue if key doesn't exist or type doesn't match
    template<typename T>
    [[nodiscard]] T get(DataKey key, T defaultValue = T{}) const;

    // Set value (overwrites existing)
    template<typename T>
    void set(DataKey key, T value);

    // Set raw DataValue directly (used by deserialization)
    void set(DataKey key, DataValue value) {
        data_[key] = std::move(value);
    }

    // Check if key exists
    [[nodiscard]] bool has(DataKey key) const;

    // Remove key (no-op if doesn't exist)
    void remove(DataKey key);

    // Get raw variant (for type inspection)
    [[nodiscard]] const DataValue* getRaw(DataKey key) const;

    // ========================================================================
    // Access by string key (convenience, auto-interns)
    // ========================================================================

    template<typename T>
    [[nodiscard]] T get(std::string_view key, T defaultValue = T{}) const {
        return get<T>(internKey(key), std::move(defaultValue));
    }

    template<typename T>
    void set(std::string_view key, T value) {
        set<T>(internKey(key), std::move(value));
    }

    [[nodiscard]] bool has(std::string_view key) const {
        return has(internKey(key));
    }

    void remove(std::string_view key) {
        remove(internKey(key));
    }

    // ========================================================================
    // Container operations
    // ========================================================================

    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] bool empty() const { return data_.empty(); }
    void clear() { data_.clear(); }

    // Iterate with callback: void(DataKey key, const DataValue& value)
    template<typename Func>
    void forEach(Func&& func) const {
        for (const auto& [key, value] : data_) {
            func(key, value);
        }
    }

    // ========================================================================
    // Serialization (CBOR)
    // ========================================================================

    // Serialize to CBOR bytes
    // Keys are written as strings (looked up from interner)
    [[nodiscard]] std::vector<uint8_t> toCBOR() const;

    // Deserialize from CBOR bytes
    // Keys are interned during loading
    [[nodiscard]] static std::unique_ptr<DataContainer> fromCBOR(std::span<const uint8_t> data);

    // Helper to deep-copy a DataValue (public for serialization use)
    [[nodiscard]] static DataValue cloneValue(const DataValue& value);

private:
    std::unordered_map<DataKey, DataValue> data_;
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename T>
T DataContainer::get(DataKey key, T defaultValue) const {
    auto it = data_.find(key);
    if (it == data_.end()) {
        return defaultValue;
    }

    // Try to get the value as type T
    if constexpr (std::is_same_v<T, bool>) {
        // Bool stored as int64_t
        if (auto* val = std::get_if<int64_t>(&it->second)) {
            return *val != 0;
        }
    } else if constexpr (std::is_integral_v<T>) {
        // All integers stored as int64_t
        if (auto* val = std::get_if<int64_t>(&it->second)) {
            return static_cast<T>(*val);
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        // All floats stored as double
        if (auto* val = std::get_if<double>(&it->second)) {
            return static_cast<T>(*val);
        }
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (auto* val = std::get_if<std::string>(&it->second)) {
            return *val;
        }
    } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
        if (auto* val = std::get_if<std::vector<uint8_t>>(&it->second)) {
            return *val;
        }
    } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
        if (auto* val = std::get_if<std::vector<int64_t>>(&it->second)) {
            return *val;
        }
    } else if constexpr (std::is_same_v<T, std::vector<double>>) {
        if (auto* val = std::get_if<std::vector<double>>(&it->second)) {
            return *val;
        }
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        if (auto* val = std::get_if<std::vector<std::string>>(&it->second)) {
            return *val;
        }
    }

    return defaultValue;
}

template<typename T>
void DataContainer::set(DataKey key, T value) {
    if constexpr (std::is_same_v<T, bool>) {
        // Store bool as int64_t
        data_[key] = static_cast<int64_t>(value ? 1 : 0);
    } else if constexpr (std::is_integral_v<T>) {
        // Store all integers as int64_t
        data_[key] = static_cast<int64_t>(value);
    } else if constexpr (std::is_floating_point_v<T>) {
        // Store all floats as double
        data_[key] = static_cast<double>(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        data_[key] = std::move(value);
    } else if constexpr (std::is_same_v<T, const char*>) {
        data_[key] = std::string(value);
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        data_[key] = std::string(value);
    } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
        data_[key] = std::move(value);
    } else if constexpr (std::is_same_v<T, std::unique_ptr<DataContainer>>) {
        data_[key] = std::move(value);
    } else if constexpr (std::is_same_v<T, std::vector<int64_t>>) {
        data_[key] = std::move(value);
    } else if constexpr (std::is_same_v<T, std::vector<double>>) {
        data_[key] = std::move(value);
    } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
        data_[key] = std::move(value);
    } else {
        static_assert(sizeof(T) == 0, "Unsupported type for DataContainer::set");
    }
}

}  // namespace finevox
