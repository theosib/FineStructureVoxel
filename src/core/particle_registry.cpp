#include "finevox/core/particle_registry.hpp"
#include "finevox/core/config_parser.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace finevox {

// ============================================================================
// ParticleTypeRegistry
// ============================================================================

ParticleTypeRegistry& ParticleTypeRegistry::global() {
    static ParticleTypeRegistry instance;
    return instance;
}

bool ParticleTypeRegistry::registerType(std::string_view name, ParticleTypeDef def) {
    std::unique_lock lock(mutex_);
    auto id = ParticleTypeId::fromName(name);
    if (types_.find(id) != types_.end()) return false;
    def.name = std::string(name);
    types_[id] = std::move(def);
    return true;
}

const ParticleTypeDef* ParticleTypeRegistry::get(ParticleTypeId id) const {
    std::shared_lock lock(mutex_);
    auto it = types_.find(id);
    return it != types_.end() ? &it->second : nullptr;
}

const ParticleTypeDef* ParticleTypeRegistry::get(std::string_view name) const {
    return get(ParticleTypeId::fromName(name));
}

ParticleTypeId ParticleTypeRegistry::getId(std::string_view name) const {
    auto id = ParticleTypeId::fromName(name);
    std::shared_lock lock(mutex_);
    return types_.count(id) ? id : ParticleTypeId{};
}

bool ParticleTypeRegistry::has(ParticleTypeId id) const {
    std::shared_lock lock(mutex_);
    return types_.count(id) > 0;
}

bool ParticleTypeRegistry::has(std::string_view name) const {
    return has(ParticleTypeId::fromName(name));
}

size_t ParticleTypeRegistry::size() const {
    std::shared_lock lock(mutex_);
    return types_.size();
}

void ParticleTypeRegistry::clear() {
    std::unique_lock lock(mutex_);
    types_.clear();
}

void ParticleTypeRegistry::forEach(
    const std::function<void(ParticleTypeId, const ParticleTypeDef&)>& fn) const
{
    std::shared_lock lock(mutex_);
    for (auto& [id, def] : types_) fn(id, def);
}

bool ParticleTypeRegistry::setAtlasUV(ParticleTypeId id, glm::vec4 uv) {
    std::unique_lock lock(mutex_);
    auto it = types_.find(id);
    if (it == types_.end()) return false;
    it->second.atlasUV = uv;
    return true;
}

bool ParticleTypeRegistry::setAtlasUV(std::string_view name, glm::vec4 uv) {
    return setAtlasUV(ParticleTypeId::fromName(name), uv);
}

// ============================================================================
// EmitterPresetRegistry
// ============================================================================

EmitterPresetRegistry& EmitterPresetRegistry::global() {
    static EmitterPresetRegistry instance;
    return instance;
}

bool EmitterPresetRegistry::registerPreset(std::string_view name, EmitterPresetDef def) {
    std::unique_lock lock(mutex_);
    auto id = EmitterPresetId::fromName(name);
    if (presets_.find(id) != presets_.end()) return false;
    def.name = std::string(name);
    presets_[id] = std::move(def);
    return true;
}

const EmitterPresetDef* EmitterPresetRegistry::get(EmitterPresetId id) const {
    std::shared_lock lock(mutex_);
    auto it = presets_.find(id);
    return it != presets_.end() ? &it->second : nullptr;
}

const EmitterPresetDef* EmitterPresetRegistry::get(std::string_view name) const {
    return get(EmitterPresetId::fromName(name));
}

EmitterPresetId EmitterPresetRegistry::getId(std::string_view name) const {
    auto id = EmitterPresetId::fromName(name);
    std::shared_lock lock(mutex_);
    return presets_.count(id) ? id : EmitterPresetId{};
}

bool EmitterPresetRegistry::has(EmitterPresetId id) const {
    std::shared_lock lock(mutex_);
    return presets_.count(id) > 0;
}

bool EmitterPresetRegistry::has(std::string_view name) const {
    return has(EmitterPresetId::fromName(name));
}

size_t EmitterPresetRegistry::size() const {
    std::shared_lock lock(mutex_);
    return presets_.size();
}

void EmitterPresetRegistry::clear() {
    std::unique_lock lock(mutex_);
    presets_.clear();
}

void EmitterPresetRegistry::forEach(
    const std::function<void(EmitterPresetId, const EmitterPresetDef&)>& fn) const
{
    std::shared_lock lock(mutex_);
    for (auto& [id, def] : presets_) fn(id, def);
}

// ============================================================================
// ParticleLoader helpers
// ============================================================================

namespace {

std::string_view trimView(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

glm::vec3 parseVec3(std::string_view s, glm::vec3 defaultVal = glm::vec3(0.0f)) {
    s = trimView(s);
    if (s.empty()) return defaultVal;
    // space-separated components
    glm::vec3 result = defaultVal;
    std::string buf(s);
    std::istringstream iss(buf);
    float v;
    int i = 0;
    while (i < 3 && iss >> v) {
        result[i++] = v;
    }
    // If only one value was provided, apply it to all three (scalar shorthand)
    if (i == 1) {
        result = glm::vec3(result[0]);
    }
    return result;
}

glm::vec4 parseVec4(std::string_view s, glm::vec4 defaultVal = glm::vec4(1.0f)) {
    s = trimView(s);
    if (s.empty()) return defaultVal;
    glm::vec4 result = defaultVal;
    std::string buf(s);
    std::istringstream iss(buf);
    float v;
    int i = 0;
    while (i < 4 && iss >> v) {
        result[i++] = v;
    }
    return result;
}

bool parseBoolSafe(std::string_view s, bool defaultVal = false) {
    s = trimView(s);
    if (s.empty()) return defaultVal;
    if (s == "true" || s == "yes" || s == "1" || s == "on") return true;
    if (s == "false" || s == "no" || s == "0" || s == "off") return false;
    return defaultVal;
}

std::string readFileToString(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

/// ConfigParser splits "key: ns:value" into key="key", suffix="ns", value="value".
/// This reconstructs the full original value string when a suffix is present
/// — needed so that names containing a colon (e.g. "finevox:block_fragment")
/// survive the parse.  Mirrors the helper in recipe_loader.cpp.
std::string entryFullValue(const ConfigEntry& e) {
    if (e.hasSuffix()) {
        return e.suffix + ":" + e.value.asStringOwned();
    }
    return e.value.asStringOwned();
}

} // anonymous namespace

// ============================================================================
// ParticleLoader — ParticleTypeDef
// ============================================================================

std::optional<ParticleTypeDef> ParticleLoader::loadTypeFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);
    if (doc.empty()) return std::nullopt;

    ParticleTypeDef def;

    for (const auto& e : doc.entries()) {
        if (e.key == "name") {
            // "name: finevox:block_fragment" would otherwise split.
            def.name = entryFullValue(e);
        } else if (e.key == "lifetime") {
            def.lifetime = Range::parse(e.value.asString());
        } else if (e.key == "initial_speed" || e.key == "speed") {
            def.initialSpeed = Range::parse(e.value.asString());
        } else if (e.key == "velocity_bias" || e.key == "velocity") {
            def.velocityBias = parseVec3(e.value.asString());
        } else if (e.key == "gravity") {
            def.gravity = e.value.asFloat();
        } else if (e.key == "drag") {
            def.drag = e.value.asFloat();
        } else if (e.key == "size") {
            def.size = Range::parse(e.value.asString());
        } else if (e.key == "size_end_scale" || e.key == "size_scale") {
            def.sizeEndScale = e.value.asFloat(1.0f);
        } else if (e.key == "start_tint" || e.key == "tint") {
            def.startTint = parseVec4(e.value.asString(), glm::vec4(1.0f));
        } else if (e.key == "end_tint") {
            def.endTint = parseVec4(e.value.asString(), def.startTint);
        } else if (e.key == "atlas_region" || e.key == "texture") {
            def.atlasRegion = entryFullValue(e);
        } else if (e.key == "billboard") {
            def.billboard = parseBillboardMode(e.value.asString());
        } else if (e.key == "collides") {
            def.collidesWithTerrain = parseBoolSafe(e.value.asString());
        } else if (e.key == "kill_on_collision") {
            def.killOnCollision = parseBoolSafe(e.value.asString(), true);
        } else if (e.key == "restitution") {
            def.restitution = e.value.asFloat(0.3f);
        }
    }

    // If only start_tint was specified, mirror it into end_tint.
    // (We default endTint to (1,1,1,1) on construction; if user specified
    // start_tint but not end_tint, align them for constant tint.)
    if (doc.get("end_tint") == nullptr && doc.get("start_tint") != nullptr) {
        def.endTint = def.startTint;
    } else if (doc.get("end_tint") == nullptr && doc.get("tint") != nullptr) {
        def.endTint = def.startTint;
    }

    return def;
}

std::optional<ParticleTypeDef> ParticleLoader::loadTypeFromFile(const std::string& path) {
    auto content = readFileToString(path);
    if (content.empty()) return std::nullopt;
    return loadTypeFromString(content);
}

size_t ParticleLoader::loadTypesFromDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".particle") continue;

        auto def = loadTypeFromFile(entry.path().string());
        if (!def) {
            std::cerr << "[ParticleLoader] Failed to parse " << entry.path() << "\n";
            continue;
        }

        // Name: prefer explicit name field, else use file stem (prefixed finevox: if no colon)
        std::string name = def->name;
        if (name.empty()) {
            name = entry.path().stem().string();
        }
        if (name.find(':') == std::string::npos) {
            name = "finevox:" + name;
        }

        if (ParticleTypeRegistry::global().registerType(name, std::move(*def))) {
            ++count;
        }
    }
    return count;
}

// ============================================================================
// ParticleLoader — EmitterPresetDef
// ============================================================================

std::optional<EmitterPresetDef> ParticleLoader::loadPresetFromString(std::string_view content) {
    ConfigParser parser;
    auto doc = parser.parseString(content);
    if (doc.empty()) return std::nullopt;

    EmitterPresetDef def;

    for (const auto& e : doc.entries()) {
        if (e.key == "name") {
            def.name = entryFullValue(e);
        } else if (e.key == "shape") {
            def.shape = parseEmitterShape(e.value.asString());
        } else if (e.key == "shape_size" || e.key == "size" || e.key == "radius") {
            def.shapeSize = parseVec3(e.value.asString());
        } else if (e.key == "spawn_rate" || e.key == "rate") {
            def.spawnRate = e.value.asFloat();
        } else if (e.key == "burst" || e.key == "burst_count") {
            def.burstCount = static_cast<uint32_t>(e.value.asInt());
        } else if (e.key == "duration") {
            def.duration = e.value.asFloat(-1.0f);
        } else if (e.key == "particle" || e.key == "particle_type") {
            def.particleType = entryFullValue(e);
        } else if (e.key == "tint") {
            def.tint = parseVec4(e.value.asString(), glm::vec4(1.0f));
            def.hasTint = true;
        }
    }

    return def;
}

std::optional<EmitterPresetDef> ParticleLoader::loadPresetFromFile(const std::string& path) {
    auto content = readFileToString(path);
    if (content.empty()) return std::nullopt;
    return loadPresetFromString(content);
}

size_t ParticleLoader::loadPresetsFromDirectory(const std::string& dirPath) {
    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return 0;

    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".emitter") continue;

        auto def = loadPresetFromFile(entry.path().string());
        if (!def) {
            std::cerr << "[ParticleLoader] Failed to parse " << entry.path() << "\n";
            continue;
        }

        std::string name = def->name;
        if (name.empty()) {
            name = entry.path().stem().string();
        }
        if (name.find(':') == std::string::npos) {
            name = "finevox:" + name;
        }

        if (EmitterPresetRegistry::global().registerPreset(name, std::move(*def))) {
            ++count;
        }
    }
    return count;
}

} // namespace finevox
