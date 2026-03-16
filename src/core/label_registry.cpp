#include "finevox/core/label_registry.hpp"

#include <fstream>
#include <sstream>

namespace finevox {

LabelRegistry& LabelRegistry::global() {
    static LabelRegistry instance;
    return instance;
}

void LabelRegistry::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    loadFromString(content);
}

void LabelRegistry::loadFromString(const std::string& content) {
    std::istringstream stream(content);
    std::string line;

    std::unique_lock lock(mutex_);

    while (std::getline(stream, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') continue;

        // Find key: value separator
        auto colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        // Trim key
        std::string key = line.substr(0, colonPos);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        if (key.empty()) continue;

        // Trim value (skip leading whitespace after colon)
        std::string value = line.substr(colonPos + 1);
        size_t start = value.find_first_not_of(' ');
        if (start != std::string::npos) {
            value = value.substr(start);
        } else {
            value.clear();
        }

        labels_[std::move(key)] = std::move(value);
    }
}

std::string_view LabelRegistry::get(std::string_view key) const {
    std::shared_lock lock(mutex_);
    auto it = labels_.find(key);
    if (it != labels_.end()) {
        return it->second;
    }
    return key;  // Fallback: return the key itself
}

std::string LabelRegistry::format(std::string_view key,
                                   const std::vector<std::string>& args) const {
    std::shared_lock lock(mutex_);
    auto it = labels_.find(key);
    std::string_view tmpl = (it != labels_.end()) ? std::string_view(it->second) : key;

    std::string result;
    result.reserve(tmpl.size() + args.size() * 8);

    for (size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] == '{' && i + 2 < tmpl.size() && tmpl[i + 2] == '}') {
            char digit = tmpl[i + 1];
            if (digit >= '0' && digit <= '9') {
                size_t idx = digit - '0';
                if (idx < args.size()) {
                    result += args[idx];
                } else {
                    result += tmpl.substr(i, 3);
                }
                i += 2;
                continue;
            }
        }
        result += tmpl[i];
    }
    return result;
}

bool LabelRegistry::has(std::string_view key) const {
    std::shared_lock lock(mutex_);
    return labels_.find(key) != labels_.end();
}

void LabelRegistry::clear() {
    std::unique_lock lock(mutex_);
    labels_.clear();
}

}  // namespace finevox
