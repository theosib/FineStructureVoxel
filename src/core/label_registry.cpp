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

namespace {

template <typename Table>
void parseLabels(const std::string& content, Table& table) {
    std::istringstream stream(content);
    std::string line;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

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

        table[std::move(key)] = std::move(value);
    }
}

}  // namespace

void LabelRegistry::loadFromString(const std::string& content) {
    std::unique_lock lock(mutex_);
    parseLabels(content, labels_);
}

void LabelRegistry::loadLocaleFile(const std::string& locale, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    loadLocaleString(locale, content);
}

void LabelRegistry::loadLocaleString(const std::string& locale, const std::string& content) {
    std::unique_lock lock(mutex_);
    parseLabels(content, locales_[locale]);
}

void LabelRegistry::setLocale(const std::string& locale) {
    std::unique_lock lock(mutex_);
    locale_ = locale;
}

std::string LabelRegistry::locale() const {
    std::shared_lock lock(mutex_);
    return locale_;
}

void LabelRegistry::setFallbackLocale(const std::string& locale) {
    std::unique_lock lock(mutex_);
    fallbackLocale_ = locale;
}

std::vector<std::string> LabelRegistry::locales() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(locales_.size());
    for (const auto& [name, table] : locales_) names.push_back(name);
    return names;
}

const std::string* LabelRegistry::findLocked(std::string_view key) const {
    for (const std::string* locale : {&locale_, &fallbackLocale_}) {
        auto table = locales_.find(*locale);
        if (table == locales_.end()) continue;
        auto it = table->second.find(key);
        if (it != table->second.end()) return &it->second;
    }
    auto it = labels_.find(key);
    return it == labels_.end() ? nullptr : &it->second;
}

const char* LabelRegistry::pluralCategory(std::string_view locale, int64_t n) {
    std::string_view language = locale.substr(0, locale.find_first_of("-_"));
    int64_t abs = n < 0 ? -n : n;

    // No grammatical number
    for (std::string_view l : {"ja", "ko", "zh", "vi", "th", "id"}) {
        if (language == l) return "other";
    }
    if (language == "fr" || language == "pt") return abs <= 1 ? "one" : "other";
    if (language == "ru" || language == "uk" || language == "pl") {
        int64_t mod10 = abs % 10;
        int64_t mod100 = abs % 100;
        if (language != "pl" && mod10 == 1 && mod100 != 11) return "one";
        if (language == "pl" && abs == 1) return "one";
        if (mod10 >= 2 && mod10 <= 4 && !(mod100 >= 12 && mod100 <= 14)) return "few";
        return "many";
    }
    return abs == 1 ? "one" : "other";
}

std::string LabelRegistry::formatLocalized(std::string_view key, const std::vector<Param>& params,
                                           const int64_t* count) const {
    std::shared_lock lock(mutex_);

    const std::string* found = nullptr;
    if (count) {
        std::string pluralKey(key);
        pluralKey += '.';
        pluralKey += pluralCategory(locale_, *count);
        found = findLocked(pluralKey);
        if (!found) found = findLocked(std::string(key) + ".other");
    }
    if (!found) found = findLocked(key);
    std::string_view source = found ? std::string_view(*found) : key;

    std::string result;
    result.reserve(source.size() + 16);
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] != '{') {
            result += source[i];
            continue;
        }
        size_t close = source.find('}', i);
        if (close == std::string_view::npos) {
            result.append(source.substr(i));
            break;
        }
        std::string_view name = source.substr(i + 1, close - i - 1);
        bool replaced = false;
        if (count && name == "count") {
            result += std::to_string(*count);
            replaced = true;
        }
        for (const Param& param : params) {
            if (!replaced && name == param.name) {
                result += param.value;
                replaced = true;
            }
        }
        // An unknown parameter stays visible, so a typo shows up on screen
        if (!replaced) result.append(source.substr(i, close - i + 1));
        i = close;
    }
    return result;
}

std::string_view LabelRegistry::get(std::string_view key) const {
    std::shared_lock lock(mutex_);
    const std::string* found = findLocked(key);
    return found ? std::string_view(*found) : key;  // Fallback: return the key itself
}

std::string LabelRegistry::format(std::string_view key,
                                   const std::vector<std::string>& args) const {
    std::shared_lock lock(mutex_);
    const std::string* found = findLocked(key);
    std::string_view tmpl = found ? std::string_view(*found) : key;

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
    return findLocked(key) != nullptr;
}

void LabelRegistry::clear() {
    std::unique_lock lock(mutex_);
    labels_.clear();
    locales_.clear();
}

}  // namespace finevox
