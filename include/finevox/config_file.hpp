#pragma once

/**
 * @file config_file.hpp
 * @brief Config file parsing with comment preservation
 *
 * Design: [Appendix A] Config File Format
 */

#include "finevox/config_parser.hpp"
#include "finevox/data_container.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace finevox {

// ============================================================================
// ConfigFile - A configuration file that preserves structure when modified
// ============================================================================
//
// This class handles reading and writing configuration files while preserving
// comments, blank lines, and ordering. When a value is modified, only that
// line changes. New keys are appended at the end.
//
// Usage:
//   ConfigFile config;
//   if (config.load("path/to/config.conf")) {
//       auto name = config.getString("name", "default");
//       config.set("name", "new value");
//       config.save();
//   }
//
class ConfigFile {
public:
    ConfigFile() = default;
    explicit ConfigFile(std::filesystem::path path);

    // Load from file (returns false if file doesn't exist or can't be read)
    [[nodiscard]] bool load(const std::filesystem::path& path);

    // Save to file (creates directories if needed)
    [[nodiscard]] bool save();

    // Save to a different path
    [[nodiscard]] bool saveAs(const std::filesystem::path& path);

    // Check if loaded
    [[nodiscard]] bool isLoaded() const { return loaded_; }

    // Check if modified since last save
    [[nodiscard]] bool isDirty() const { return dirty_; }

    // Get the file path
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    // ========================================================================
    // Value access (read)
    // ========================================================================

    [[nodiscard]] bool has(std::string_view key) const;

    [[nodiscard]] std::string getString(std::string_view key,
                                        std::string_view defaultVal = "") const;
    [[nodiscard]] int64_t getInt(std::string_view key, int64_t defaultVal = 0) const;
    [[nodiscard]] double getFloat(std::string_view key, double defaultVal = 0.0) const;
    [[nodiscard]] bool getBool(std::string_view key, bool defaultVal = false) const;

    // Generic get (uses DataContainer internally)
    template<typename T>
    [[nodiscard]] T get(std::string_view key, T defaultVal = T{}) const {
        return data_.get<T>(key, std::move(defaultVal));
    }

    // ========================================================================
    // Value access (write)
    // ========================================================================

    void set(std::string_view key, std::string_view value);
    void set(std::string_view key, int64_t value);
    void set(std::string_view key, double value);
    void set(std::string_view key, bool value);

    // Generic set
    template<typename T>
    void set(std::string_view key, T value) {
        setImpl(key, formatValue(value));
        data_.set(key, std::move(value));
    }

    // Remove a key (comments out the line rather than deleting)
    void remove(std::string_view key);

    // ========================================================================
    // Header comment
    // ========================================================================

    // Set header comment (written at top of file if no content exists)
    void setHeader(std::string_view header) { header_ = header; }

    // ========================================================================
    // Direct DataContainer access (for complex operations)
    // ========================================================================

    [[nodiscard]] const DataContainer& data() const { return data_; }

private:
    // A line in the config file
    struct Line {
        std::string content;      // Original line content
        std::string key;          // Key if this is a key-value line, empty otherwise
        size_t valueStart = 0;    // Position where value starts (after ": ")
        size_t valueEnd = 0;      // Position where value ends
        bool isKeyValue = false;  // True if this line has a key-value pair
    };

    // Parse the raw content into lines
    void parseLines(std::string_view content);

    // Find the line index for a key (-1 if not found)
    [[nodiscard]] int findLine(std::string_view key) const;

    // Implementation for setting values
    void setImpl(std::string_view key, const std::string& formattedValue);

    // Format values for output
    [[nodiscard]] static std::string formatValue(std::string_view value);
    [[nodiscard]] static std::string formatValue(int64_t value);
    [[nodiscard]] static std::string formatValue(double value);
    [[nodiscard]] static std::string formatValue(bool value);

    std::filesystem::path path_;
    std::vector<Line> lines_;
    std::unordered_map<std::string, size_t> keyToLine_;  // Key -> line index
    DataContainer data_;
    std::string header_;
    bool loaded_ = false;
    bool dirty_ = false;
};

// ============================================================================
// Convenience function
// ============================================================================

// Load a config file using ResourceLocator for path resolution
[[nodiscard]] std::optional<ConfigFile> loadConfigFile(const std::string& resourcePath);

}  // namespace finevox
