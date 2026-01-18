#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <functional>
#include <optional>
#include <variant>

namespace finevox {

// ============================================================================
// ConfigValue - A parsed configuration value
// ============================================================================

/**
 * @brief A configuration value that can be a string, number, or list of numbers
 */
class ConfigValue {
public:
    ConfigValue() = default;
    explicit ConfigValue(std::string_view text) : text_(text) {}
    explicit ConfigValue(std::vector<float> numbers) : numbers_(std::move(numbers)) {}

    // String access
    [[nodiscard]] std::string_view asString() const { return text_; }
    [[nodiscard]] std::string asStringOwned() const { return std::string(text_); }

    // Boolean access
    [[nodiscard]] bool asBool(bool defaultVal = false) const;

    // Numeric access
    [[nodiscard]] float asFloat(float defaultVal = 0.0f) const;
    [[nodiscard]] int asInt(int defaultVal = 0) const;

    // Number list access (for data lines)
    [[nodiscard]] const std::vector<float>& asNumbers() const { return numbers_; }
    [[nodiscard]] bool hasNumbers() const { return !numbers_.empty(); }

    // Check if empty
    [[nodiscard]] bool empty() const { return text_.empty() && numbers_.empty(); }

private:
    std::string text_;
    std::vector<float> numbers_;
};

// ============================================================================
// ConfigEntry - A key-value pair with optional suffix and data lines
// ============================================================================

/**
 * @brief A configuration entry
 *
 * Represents entries like:
 *   key: value
 *   key:suffix: value
 *   key:suffix:
 *       data line 1
 *       data line 2
 */
struct ConfigEntry {
    std::string key;              // Primary key (e.g., "face", "texture")
    std::string suffix;           // Optional suffix (e.g., "top", "bottom")
    ConfigValue value;            // Value after the colon(s)
    std::vector<std::vector<float>> dataLines;  // Indented data lines (parsed as floats)

    [[nodiscard]] bool hasSuffix() const { return !suffix.empty(); }
    [[nodiscard]] bool hasData() const { return !dataLines.empty(); }
};

// ============================================================================
// ConfigDocument - A parsed configuration file
// ============================================================================

/**
 * @brief A parsed configuration document
 *
 * Contains all entries from a config file, in order. Supports:
 * - Iteration over all entries
 * - Lookup by key (returns first match)
 * - Lookup by key+suffix
 * - Multiple entries with same key (later overrides earlier for simple lookups)
 */
class ConfigDocument {
public:
    ConfigDocument() = default;

    // Add an entry
    void addEntry(ConfigEntry entry);

    // Lookup by key (returns last entry with this key, or nullptr)
    [[nodiscard]] const ConfigEntry* get(std::string_view key) const;

    // Lookup by key and suffix
    [[nodiscard]] const ConfigEntry* get(std::string_view key, std::string_view suffix) const;

    // Get value directly (convenience)
    [[nodiscard]] std::string_view getString(std::string_view key, std::string_view defaultVal = "") const;
    [[nodiscard]] float getFloat(std::string_view key, float defaultVal = 0.0f) const;
    [[nodiscard]] int getInt(std::string_view key, int defaultVal = 0) const;
    [[nodiscard]] bool getBool(std::string_view key, bool defaultVal = false) const;

    // Get all entries with a given key
    [[nodiscard]] std::vector<const ConfigEntry*> getAll(std::string_view key) const;

    // Iteration
    [[nodiscard]] const std::vector<ConfigEntry>& entries() const { return entries_; }
    [[nodiscard]] auto begin() const { return entries_.begin(); }
    [[nodiscard]] auto end() const { return entries_.end(); }
    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    std::vector<ConfigEntry> entries_;
};

// ============================================================================
// ConfigParser - Parses configuration files
// ============================================================================

/**
 * @brief Parser for simple configuration files
 *
 * Format:
 * ```
 * # Comments start with #
 * key: value
 * key:suffix: value
 * key:suffix:
 *     1.0 2.0 3.0
 *     4.0 5.0 6.0
 * include: other_file
 * ```
 *
 * Features:
 * - Line-based parsing
 * - Indented data blocks (space or tab)
 * - Include directives with override semantics
 * - Comments with #
 *
 * Usage:
 * ```cpp
 * ConfigParser parser;
 * parser.setIncludeResolver([](const std::string& path) {
 *     return ResourceLocator::global().resolve(path);
 * });
 *
 * auto doc = parser.parseFile("game://config/blocks.conf");
 * if (doc) {
 *     auto texture = doc->getString("texture");
 *     auto translucent = doc->getBool("translucent", false);
 *
 *     for (auto* entry : doc->getAll("face")) {
 *         // entry->suffix is "top", "bottom", etc.
 *         // entry->dataLines contains vertex data
 *     }
 * }
 * ```
 */
class ConfigParser {
public:
    using IncludeResolver = std::function<std::string(const std::string&)>;

    ConfigParser() = default;

    /**
     * @brief Set the include resolver
     *
     * Called when an `include:` directive is encountered.
     * Should return the full filesystem path for a logical include path.
     */
    void setIncludeResolver(IncludeResolver resolver) { includeResolver_ = std::move(resolver); }

    /**
     * @brief Parse a configuration file
     * @param path Filesystem path to the file
     * @return Parsed document, or nullopt on error
     */
    [[nodiscard]] std::optional<ConfigDocument> parseFile(const std::string& path) const;

    /**
     * @brief Parse configuration from a string
     * @param content The configuration content
     * @param basePath Base path for resolving includes (optional)
     * @return Parsed document
     */
    [[nodiscard]] ConfigDocument parseString(std::string_view content,
                                              const std::string& basePath = "") const;

private:
    // Parse a single line, returns true if it's a directive (not a data line)
    bool parseLine(std::string_view line, ConfigEntry& currentEntry,
                   ConfigDocument& doc, const std::string& basePath) const;

    // Parse numbers from a data line
    std::vector<float> parseDataLine(std::string_view line) const;

    // Flush the current entry to the document
    void flushEntry(ConfigEntry& entry, ConfigDocument& doc) const;

    IncludeResolver includeResolver_;
};

// ============================================================================
// Convenience functions
// ============================================================================

/**
 * @brief Parse a config file using ResourceLocator for path resolution
 */
[[nodiscard]] std::optional<ConfigDocument> parseConfig(const std::string& resourcePath);

}  // namespace finevox
