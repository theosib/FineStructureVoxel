#include "finevox/config_file.hpp"
#include "finevox/resource_locator.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <cctype>

namespace finevox {

ConfigFile::ConfigFile(std::filesystem::path path)
    : path_(std::move(path)) {
    load(path_);
}

bool ConfigFile::load(const std::filesystem::path& path) {
    path_ = path;
    lines_.clear();
    keyToLine_.clear();
    data_.clear();
    loaded_ = false;
    dirty_ = false;

    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    parseLines(buffer.str());

    loaded_ = true;
    return true;
}

void ConfigFile::parseLines(std::string_view content) {
    size_t pos = 0;
    size_t lineNum = 0;

    while (pos < content.size()) {
        // Find end of line
        size_t lineEnd = content.find('\n', pos);
        std::string_view lineView;
        if (lineEnd == std::string_view::npos) {
            lineView = content.substr(pos);
            pos = content.size();
        } else {
            lineView = content.substr(pos, lineEnd - pos);
            pos = lineEnd + 1;
        }

        // Remove trailing \r
        if (!lineView.empty() && lineView.back() == '\r') {
            lineView = lineView.substr(0, lineView.size() - 1);
        }

        Line line;
        line.content = std::string(lineView);

        // Check if this is a key-value line
        // Skip if empty, comment, or starts with whitespace (data line)
        if (!lineView.empty() && lineView[0] != '#' &&
            !std::isspace(static_cast<unsigned char>(lineView[0]))) {

            auto colonPos = lineView.find(':');
            if (colonPos != std::string_view::npos) {
                // Check for second colon (key:suffix:)
                auto rest = lineView.substr(colonPos + 1);
                auto secondColon = rest.find(':');

                std::string key;
                size_t valueStartInLine;

                if (secondColon != std::string_view::npos) {
                    // Has suffix - treat key:suffix as the full key
                    key = std::string(lineView.substr(0, colonPos));
                    key += ":";
                    // Trim suffix
                    auto suffix = rest.substr(0, secondColon);
                    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.front()))) {
                        suffix = suffix.substr(1);
                    }
                    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back()))) {
                        suffix = suffix.substr(0, suffix.size() - 1);
                    }
                    key += std::string(suffix);
                    valueStartInLine = colonPos + 1 + secondColon + 1;
                } else {
                    key = std::string(lineView.substr(0, colonPos));
                    valueStartInLine = colonPos + 1;
                }

                // Find value bounds (skip leading whitespace)
                while (valueStartInLine < lineView.size() &&
                       std::isspace(static_cast<unsigned char>(lineView[valueStartInLine]))) {
                    valueStartInLine++;
                }

                // Trim trailing whitespace for value end
                size_t valueEndInLine = lineView.size();
                while (valueEndInLine > valueStartInLine &&
                       std::isspace(static_cast<unsigned char>(lineView[valueEndInLine - 1]))) {
                    valueEndInLine--;
                }

                line.key = key;
                line.valueStart = valueStartInLine;
                line.valueEnd = valueEndInLine;
                line.isKeyValue = true;

                // Store in lookup map (later lines override earlier for same key)
                keyToLine_[key] = lineNum;

                // Parse and store value in DataContainer
                if (valueStartInLine < valueEndInLine) {
                    auto valueStr = lineView.substr(valueStartInLine, valueEndInLine - valueStartInLine);

                    // Determine type and store
                    if (valueStr == "true" || valueStr == "yes") {
                        data_.set(key, true);
                    } else if (valueStr == "false" || valueStr == "no") {
                        data_.set(key, false);
                    } else {
                        // Try parsing as number
                        char* end = nullptr;
                        long long intVal;

                        // Check for hex prefix
                        if (valueStr.size() > 2 && valueStr[0] == '0' &&
                            (valueStr[1] == 'x' || valueStr[1] == 'X')) {
                            intVal = std::strtoll(valueStr.data(), &end, 16);
                        } else {
                            intVal = std::strtoll(valueStr.data(), &end, 10);
                        }

                        if (end != valueStr.data() && end == valueStr.data() + valueStr.size()) {
                            data_.set(key, static_cast<int64_t>(intVal));
                        } else {
                            // Try as float
                            double floatVal = std::strtod(valueStr.data(), &end);
                            if (end != valueStr.data() && end == valueStr.data() + valueStr.size()) {
                                data_.set(key, floatVal);
                            } else {
                                // Store as string
                                data_.set(key, std::string(valueStr));
                            }
                        }
                    }
                }
            }
        }

        lines_.push_back(std::move(line));
        lineNum++;
    }
}

bool ConfigFile::save() {
    return saveAs(path_);
}

bool ConfigFile::saveAs(const std::filesystem::path& path) {
    // Create parent directories
    auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    // If no lines exist, write header
    if (lines_.empty() && !header_.empty()) {
        file << header_;
        if (!header_.empty() && header_.back() != '\n') {
            file << '\n';
        }
    }

    // Write all lines
    for (const auto& line : lines_) {
        file << line.content << '\n';
    }

    if (!file.good()) {
        return false;
    }

    path_ = path;
    dirty_ = false;
    return true;
}

bool ConfigFile::has(std::string_view key) const {
    return keyToLine_.find(std::string(key)) != keyToLine_.end();
}

std::string ConfigFile::getString(std::string_view key, std::string_view defaultVal) const {
    return data_.get<std::string>(key, std::string(defaultVal));
}

int64_t ConfigFile::getInt(std::string_view key, int64_t defaultVal) const {
    return data_.get<int64_t>(key, defaultVal);
}

double ConfigFile::getFloat(std::string_view key, double defaultVal) const {
    return data_.get<double>(key, defaultVal);
}

bool ConfigFile::getBool(std::string_view key, bool defaultVal) const {
    return data_.get<bool>(key, defaultVal);
}

int ConfigFile::findLine(std::string_view key) const {
    auto it = keyToLine_.find(std::string(key));
    if (it != keyToLine_.end()) {
        return static_cast<int>(it->second);
    }
    return -1;
}

void ConfigFile::setImpl(std::string_view key, const std::string& formattedValue) {
    int lineIdx = findLine(key);

    if (lineIdx >= 0) {
        // Update existing line - replace just the value portion
        auto& line = lines_[static_cast<size_t>(lineIdx)];
        std::string newContent = line.content.substr(0, line.valueStart);
        newContent += formattedValue;
        // Preserve any trailing content (unlikely but possible)

        line.content = std::move(newContent);
        line.valueEnd = line.valueStart + formattedValue.size();
    } else {
        // Add new line at end
        Line newLine;
        newLine.key = std::string(key);
        newLine.content = std::string(key) + ": " + formattedValue;
        newLine.valueStart = key.size() + 2;  // "key: " length
        newLine.valueEnd = newLine.content.size();
        newLine.isKeyValue = true;

        keyToLine_[std::string(key)] = lines_.size();
        lines_.push_back(std::move(newLine));
    }

    dirty_ = true;
}

void ConfigFile::set(std::string_view key, std::string_view value) {
    setImpl(key, formatValue(value));
    data_.set(key, std::string(value));
}

void ConfigFile::set(std::string_view key, int64_t value) {
    setImpl(key, formatValue(value));
    data_.set(key, value);
}

void ConfigFile::set(std::string_view key, double value) {
    setImpl(key, formatValue(value));
    data_.set(key, value);
}

void ConfigFile::set(std::string_view key, bool value) {
    setImpl(key, formatValue(value));
    data_.set(key, value);
}

void ConfigFile::remove(std::string_view key) {
    int lineIdx = findLine(key);
    if (lineIdx >= 0) {
        // Comment out the line instead of removing it
        auto& line = lines_[static_cast<size_t>(lineIdx)];
        line.content = "# " + line.content;
        line.isKeyValue = false;

        keyToLine_.erase(std::string(key));
        data_.remove(key);
        dirty_ = true;
    }
}

std::string ConfigFile::formatValue(std::string_view value) {
    return std::string(value);
}

std::string ConfigFile::formatValue(int64_t value) {
    return std::to_string(value);
}

std::string ConfigFile::formatValue(double value) {
    std::ostringstream oss;
    oss << value;
    return oss.str();
}

std::string ConfigFile::formatValue(bool value) {
    return value ? "true" : "false";
}

// ============================================================================
// Convenience function
// ============================================================================

std::optional<ConfigFile> loadConfigFile(const std::string& resourcePath) {
    auto resolvedPath = ResourceLocator::instance().resolve(resourcePath);
    if (resolvedPath.empty()) {
        return std::nullopt;
    }

    ConfigFile config;
    if (config.load(resolvedPath)) {
        return config;
    }
    return std::nullopt;
}

}  // namespace finevox
