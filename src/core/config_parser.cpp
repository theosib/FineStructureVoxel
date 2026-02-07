#include "finevox/core/config_parser.hpp"
#include "finevox/core/resource_locator.hpp"

#include <fstream>
#include <sstream>
#include <cctype>
#include <cstdlib>
#include <algorithm>

namespace finevox {

// ============================================================================
// ConfigValue
// ============================================================================

bool ConfigValue::asBool(bool defaultVal) const {
    if (text_.empty()) return defaultVal;

    // Check for common true values
    if (text_ == "true" || text_ == "yes" || text_ == "1" ||
        text_ == "on" || text_ == "t" || text_ == "y") {
        return true;
    }
    // Check for common false values
    if (text_ == "false" || text_ == "no" || text_ == "0" ||
        text_ == "off" || text_ == "f" || text_ == "n") {
        return false;
    }
    return defaultVal;
}

float ConfigValue::asFloat(float defaultVal) const {
    if (!numbers_.empty()) {
        return numbers_[0];
    }
    if (text_.empty()) return defaultVal;

    char* end;
    float val = std::strtof(text_.data(), &end);
    if (end == text_.data()) return defaultVal;
    return val;
}

int ConfigValue::asInt(int defaultVal) const {
    if (!numbers_.empty()) {
        return static_cast<int>(numbers_[0]);
    }
    if (text_.empty()) return defaultVal;

    char* end;
    long val = std::strtol(text_.data(), &end, 10);
    if (end == text_.data()) return defaultVal;
    return static_cast<int>(val);
}

// ============================================================================
// ConfigDocument
// ============================================================================

void ConfigDocument::addEntry(ConfigEntry entry) {
    entries_.push_back(std::move(entry));
}

const ConfigEntry* ConfigDocument::get(std::string_view key) const {
    // Return last entry with this key (later overrides earlier)
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->key == key) {
            return &(*it);
        }
    }
    return nullptr;
}

const ConfigEntry* ConfigDocument::get(std::string_view key, std::string_view suffix) const {
    // Return last entry with this key and suffix
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        if (it->key == key && it->suffix == suffix) {
            return &(*it);
        }
    }
    return nullptr;
}

std::string_view ConfigDocument::getString(std::string_view key, std::string_view defaultVal) const {
    if (auto* entry = get(key)) {
        auto sv = entry->value.asString();
        if (!sv.empty()) return sv;
    }
    return defaultVal;
}

float ConfigDocument::getFloat(std::string_view key, float defaultVal) const {
    if (auto* entry = get(key)) {
        return entry->value.asFloat(defaultVal);
    }
    return defaultVal;
}

int ConfigDocument::getInt(std::string_view key, int defaultVal) const {
    if (auto* entry = get(key)) {
        return entry->value.asInt(defaultVal);
    }
    return defaultVal;
}

bool ConfigDocument::getBool(std::string_view key, bool defaultVal) const {
    if (auto* entry = get(key)) {
        return entry->value.asBool(defaultVal);
    }
    return defaultVal;
}

std::vector<const ConfigEntry*> ConfigDocument::getAll(std::string_view key) const {
    std::vector<const ConfigEntry*> result;
    for (const auto& entry : entries_) {
        if (entry.key == key) {
            result.push_back(&entry);
        }
    }
    return result;
}

// ============================================================================
// ConfigParser
// ============================================================================

std::optional<ConfigDocument> ConfigParser::parseFile(const std::string& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();

    // Extract base path for relative includes
    std::string basePath;
    auto lastSlash = path.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        basePath = path.substr(0, lastSlash + 1);
    }

    return parseString(buffer.str(), basePath);
}

ConfigDocument ConfigParser::parseString(std::string_view content, const std::string& basePath) const {
    ConfigDocument doc;
    ConfigEntry currentEntry;

    std::string_view remaining = content;

    while (!remaining.empty()) {
        // Find end of line
        auto lineEnd = remaining.find('\n');
        std::string_view line;
        if (lineEnd == std::string_view::npos) {
            line = remaining;
            remaining = {};
        } else {
            line = remaining.substr(0, lineEnd);
            remaining = remaining.substr(lineEnd + 1);
        }

        // Remove trailing \r if present (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line = line.substr(0, line.size() - 1);
        }

        parseLine(line, currentEntry, doc, basePath);
    }

    // Flush any remaining entry
    flushEntry(currentEntry, doc);

    return doc;
}

bool ConfigParser::parseLine(std::string_view line, ConfigEntry& currentEntry,
                              ConfigDocument& doc, const std::string& basePath) const {
    // Empty line
    if (line.empty()) {
        return true;
    }

    // Check if this is a data line (starts with whitespace)
    if (std::isspace(static_cast<unsigned char>(line[0]))) {
        // Parse as data line and add to current entry
        auto numbers = parseDataLine(line);
        if (!numbers.empty()) {
            currentEntry.dataLines.push_back(std::move(numbers));
        }
        return false;  // Not a directive
    }

    // Not a data line - flush current entry first
    flushEntry(currentEntry, doc);
    currentEntry = ConfigEntry{};

    // Skip comments
    if (line[0] == '#') {
        return true;
    }

    // Find the first colon
    auto colonPos = line.find(':');
    if (colonPos == std::string_view::npos) {
        // No colon - treat as simple key with no value
        currentEntry.key = std::string(line);
        return true;
    }

    // Extract key
    currentEntry.key = std::string(line.substr(0, colonPos));

    // Check for second colon (key:suffix:)
    auto rest = line.substr(colonPos + 1);
    auto secondColon = rest.find(':');

    if (secondColon != std::string_view::npos) {
        // Has suffix
        currentEntry.suffix = std::string(rest.substr(0, secondColon));

        // Trim leading/trailing whitespace from suffix
        while (!currentEntry.suffix.empty() && std::isspace(static_cast<unsigned char>(currentEntry.suffix.front()))) {
            currentEntry.suffix.erase(0, 1);
        }
        while (!currentEntry.suffix.empty() && std::isspace(static_cast<unsigned char>(currentEntry.suffix.back()))) {
            currentEntry.suffix.pop_back();
        }

        rest = rest.substr(secondColon + 1);
    }

    // Trim leading whitespace from value
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest[0]))) {
        rest = rest.substr(1);
    }
    // Trim trailing whitespace
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) {
        rest = rest.substr(0, rest.size() - 1);
    }

    // Handle special directives
    if (currentEntry.key == "include") {
        std::string includePath(rest);

        // Resolve the include path
        std::string resolvedPath;
        if (includeResolver_) {
            resolvedPath = includeResolver_(includePath);
        } else {
            // Default: relative to base path
            resolvedPath = basePath + includePath;
        }

        // Parse the included file and merge
        if (auto includedDoc = parseFile(resolvedPath)) {
            for (const auto& entry : *includedDoc) {
                doc.addEntry(entry);
            }
        }

        currentEntry = ConfigEntry{};  // Clear - don't add include as entry
        return true;
    }

    // Store the value
    if (!rest.empty()) {
        currentEntry.value = ConfigValue(rest);
    }

    return true;
}

std::vector<float> ConfigParser::parseDataLine(std::string_view line) const {
    std::vector<float> numbers;

    size_t pos = 0;
    while (pos < line.size()) {
        // Skip whitespace
        while (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
            pos++;
        }
        if (pos >= line.size()) break;

        // Parse number
        const char* start = line.data() + pos;
        char* end;
        float val = std::strtof(start, &end);

        if (end == start) {
            // Not a number - skip this token
            while (pos < line.size() && !std::isspace(static_cast<unsigned char>(line[pos]))) {
                pos++;
            }
        } else {
            numbers.push_back(val);
            pos = end - line.data();
        }
    }

    return numbers;
}

void ConfigParser::flushEntry(ConfigEntry& entry, ConfigDocument& doc) const {
    if (!entry.key.empty()) {
        doc.addEntry(std::move(entry));
        entry = ConfigEntry{};
    }
}

// ============================================================================
// Convenience functions
// ============================================================================

std::optional<ConfigDocument> parseConfig(const std::string& resourcePath) {
    auto resolvedPath = ResourceLocator::instance().resolve(resourcePath);
    if (resolvedPath.empty()) {
        return std::nullopt;
    }

    ConfigParser parser;
    parser.setIncludeResolver([](const std::string& path) -> std::string {
        auto resolved = ResourceLocator::instance().resolve(path);
        return resolved.string();
    });

    return parser.parseFile(resolvedPath.string());
}

}  // namespace finevox
