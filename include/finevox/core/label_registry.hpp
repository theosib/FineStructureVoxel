#pragma once

/**
 * @file label_registry.hpp
 * @brief Localized string registry for UI text
 *
 * Loads key-value label files (.lang format) and provides fast lookup.
 * Scripts access labels via the `L` native function.
 *
 * Thread safety: All public methods are thread-safe.
 */

#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class LabelRegistry {
public:
    static LabelRegistry& global();

    /// Load labels from a .lang file (key: value format, one per line)
    void loadFile(const std::string& path);

    /// Load labels from a string
    void loadFromString(const std::string& content);

    /// Get a label by key. Returns the key itself if not found.
    [[nodiscard]] std::string_view get(std::string_view key) const;

    /// Get a label with positional argument substitution ({0}, {1}, etc.)
    [[nodiscard]] std::string format(std::string_view key,
                                     const std::vector<std::string>& args) const;

    /// Check if a label key exists
    [[nodiscard]] bool has(std::string_view key) const;

    /// Clear all loaded labels
    void clear();

private:
    LabelRegistry() = default;

    // Transparent hash for heterogeneous lookup with string_view
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const {
            return std::hash<std::string>{}(s);
        }
    };

    std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> labels_;
    mutable std::shared_mutex mutex_;

    // Fallback storage for get() returning string_view when key not found
    // (returns view into the key parameter, which the caller owns)
};

}  // namespace finevox
