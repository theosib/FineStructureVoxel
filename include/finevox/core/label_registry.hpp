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

#include <cstdint>
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

    /// Load a .lang file as one locale's strings (e.g. "fr" from lang/fr.lang)
    void loadLocaleFile(const std::string& locale, const std::string& path);
    void loadLocaleString(const std::string& locale, const std::string& content);

    /// Select the active locale. Lookups try it, then the fallback locale, then
    /// strings loaded without a locale, then show the key.
    void setLocale(const std::string& locale);
    [[nodiscard]] std::string locale() const;
    void setFallbackLocale(const std::string& locale);

    /// Locales that have strings loaded
    [[nodiscard]] std::vector<std::string> locales() const;

    /// Get a label by key. Returns the key itself if not found.
    [[nodiscard]] std::string_view get(std::string_view key) const;

    /// Get a label with positional argument substitution ({0}, {1}, etc.)
    [[nodiscard]] std::string format(std::string_view key,
                                     const std::vector<std::string>& args) const;

    /// Parameter for formatLocalized
    struct Param {
        std::string name;
        std::string value;
    };

    /// Localized lookup with named {param} substitution. With a count, the key
    /// gains a plural suffix chosen by the active locale's rules (`key.one`,
    /// `key.few`, `key.many`, `key.other`), falling back to `key.other` then `key`.
    /// `{count}` is substituted automatically.
    [[nodiscard]] std::string formatLocalized(std::string_view key,
                                              const std::vector<Param>& params,
                                              const int64_t* count = nullptr) const;

    /// CLDR-style plural category for a locale ("one", "few", "many", "other")
    [[nodiscard]] static const char* pluralCategory(std::string_view locale, int64_t n);

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

    using Table = std::unordered_map<std::string, std::string, StringHash, std::equal_to<>>;

    /// Caller holds mutex_. Nullptr if no table has the key.
    [[nodiscard]] const std::string* findLocked(std::string_view key) const;

    Table labels_;  // Strings loaded without a locale
    std::unordered_map<std::string, Table, StringHash, std::equal_to<>> locales_;
    std::string locale_ = "en";
    std::string fallbackLocale_ = "en";
    mutable std::shared_mutex mutex_;

    // Fallback storage for get() returning string_view when key not found
    // (returns view into the key parameter, which the caller owns)
};

}  // namespace finevox
