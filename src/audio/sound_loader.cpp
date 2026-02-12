#include "finevox/audio/sound_loader.hpp"

#include <filesystem>

namespace finevox::audio {

namespace {

/// Map action name strings to SoundAction enum values
std::optional<SoundAction> parseActionName(std::string_view key) {
    if (key == "place") return SoundAction::Place;
    if (key == "break") return SoundAction::Break;
    if (key == "step")  return SoundAction::Step;
    if (key == "dig")   return SoundAction::Dig;
    if (key == "hit")   return SoundAction::Hit;
    if (key == "fall")  return SoundAction::Fall;
    return std::nullopt;
}

}  // anonymous namespace

std::optional<SoundSetDefinition> SoundLoader::loadFromFile(
    const std::string& name, const std::string& configPath) {

    ConfigParser parser;
    auto doc = parser.parseFile(configPath);
    if (!doc) {
        return std::nullopt;
    }

    return loadFromConfig(name, *doc);
}

std::optional<SoundSetDefinition> SoundLoader::loadFromConfig(
    const std::string& name, const ConfigDocument& doc) {

    SoundSetDefinition def;
    def.name = name;

    // Read optional global modifiers
    def.volume = doc.getFloat("volume", 1.0f);
    def.pitchVariance = doc.getFloat("pitch-variance", 0.1f);

    // Read action entries (place, break, step, dig, hit, fall)
    // Each action can have multiple entries (repeated keys)
    for (const auto& entry : doc.entries()) {
        auto action = parseActionName(entry.key);
        if (!action) continue;

        auto valueStr = entry.value.asString();
        if (valueStr.empty()) continue;

        SoundVariant variant;
        variant.path = std::string(valueStr);
        variant.volumeScale = 1.0f;
        variant.pitchScale = 1.0f;

        def.actions[*action].variants.push_back(std::move(variant));
    }

    // Only return if we have at least one action with variants
    bool hasAny = false;
    for (const auto& [action, group] : def.actions) {
        if (!group.empty()) {
            hasAny = true;
            break;
        }
    }

    if (!hasAny) {
        return std::nullopt;
    }

    return def;
}

size_t SoundLoader::loadDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;

    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) {
        return 0;
    }

    size_t count = 0;

    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;

        auto path = entry.path();
        if (path.extension() != ".sound") continue;

        auto name = path.stem().string();

        auto def = loadFromFile(name, path.string());
        if (def) {
            if (SoundRegistry::global().registerSoundSet(name, std::move(*def))) {
                count++;
            }
        }
    }

    return count;
}

}  // namespace finevox::audio
