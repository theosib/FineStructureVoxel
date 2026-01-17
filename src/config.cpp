#include "finevox/config.hpp"
#include <fstream>
#include <chrono>

namespace finevox {

// ============================================================================
// ConfigManager implementation
// ============================================================================

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

ConfigManager::~ConfigManager() {
    if (dirty_ && initialized_) {
        save();
    }
}

void ConfigManager::init(const std::filesystem::path& configPath) {
    std::unique_lock lock(mutex_);

    configPath_ = configPath;
    setDefaults();

    if (std::filesystem::exists(configPath_)) {
        loadFromFile();
    }

    initialized_ = true;
}

bool ConfigManager::isInitialized() const {
    std::shared_lock lock(mutex_);
    return initialized_;
}

bool ConfigManager::save() {
    std::unique_lock lock(mutex_);
    if (!initialized_) return false;
    return saveToFile();
}

bool ConfigManager::reload() {
    std::unique_lock lock(mutex_);
    if (!initialized_) return false;

    setDefaults();
    return loadFromFile();
}

void ConfigManager::reset() {
    std::unique_lock lock(mutex_);
    data_.clear();
    configPath_.clear();
    initialized_ = false;
    dirty_ = false;
}

std::filesystem::path ConfigManager::configPath() const {
    std::shared_lock lock(mutex_);
    return configPath_;
}

void ConfigManager::setDefaults() {
    data_.clear();
    data_.set("compression.enabled", true);
    data_.set("debug.logging", false);
    data_.set("io.thread_count", 2);
    data_.set("io.max_open_regions", 16);
    data_.set("cache.column_size", 64);
}

bool ConfigManager::loadFromFile() {
    std::ifstream file(configPath_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // Read file contents
    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);

    if (!file.good()) {
        return false;
    }

    // Parse CBOR
    auto loaded = DataContainer::fromCBOR(bytes);
    if (loaded) {
        // Merge loaded values (keep defaults for missing keys)
        // For now, just replace entirely
        data_ = std::move(*loaded);
    }

    dirty_ = false;
    return true;
}

bool ConfigManager::saveToFile() {
    // Create parent directories if needed
    auto parent = configPath_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    auto bytes = data_.toCBOR();

    std::ofstream file(configPath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));

    if (!file.good()) {
        return false;
    }

    dirty_ = false;
    return true;
}

// Typed accessors

bool ConfigManager::compressionEnabled() const {
    std::shared_lock lock(mutex_);
    return data_.get<bool>("compression.enabled", true);
}

void ConfigManager::setCompressionEnabled(bool enabled) {
    std::unique_lock lock(mutex_);
    data_.set("compression.enabled", enabled);
    dirty_ = true;
}

bool ConfigManager::debugLogging() const {
    std::shared_lock lock(mutex_);
    return data_.get<bool>("debug.logging", false);
}

void ConfigManager::setDebugLogging(bool enabled) {
    std::unique_lock lock(mutex_);
    data_.set("debug.logging", enabled);
    dirty_ = true;
}

int ConfigManager::ioThreadCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<int>(data_.get<int64_t>("io.thread_count", 2));
}

void ConfigManager::setIOThreadCount(int count) {
    std::unique_lock lock(mutex_);
    data_.set("io.thread_count", static_cast<int64_t>(count));
    dirty_ = true;
}

size_t ConfigManager::maxOpenRegions() const {
    std::shared_lock lock(mutex_);
    return static_cast<size_t>(data_.get<int64_t>("io.max_open_regions", 16));
}

void ConfigManager::setMaxOpenRegions(size_t count) {
    std::unique_lock lock(mutex_);
    data_.set("io.max_open_regions", static_cast<int64_t>(count));
    dirty_ = true;
}

size_t ConfigManager::columnCacheSize() const {
    std::shared_lock lock(mutex_);
    return static_cast<size_t>(data_.get<int64_t>("cache.column_size", 64));
}

void ConfigManager::setColumnCacheSize(size_t count) {
    std::unique_lock lock(mutex_);
    data_.set("cache.column_size", static_cast<int64_t>(count));
    dirty_ = true;
}

bool ConfigManager::has(const std::string& key) const {
    std::shared_lock lock(mutex_);
    return data_.has(key);
}

void ConfigManager::remove(const std::string& key) {
    std::unique_lock lock(mutex_);
    data_.remove(key);
    dirty_ = true;
}

// ============================================================================
// WorldConfig implementation
// ============================================================================

WorldConfig::WorldConfig(const std::filesystem::path& worldDir)
    : worldDir_(worldDir)
    , configPath_(worldDir / "world.cbor") {

    setDefaults();

    if (std::filesystem::exists(configPath_)) {
        loadFromFile();
    }
}

void WorldConfig::setDefaults() {
    data_.clear();
    data_.set("name", std::string("New World"));
    data_.set("seed", static_cast<int64_t>(0));

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();

    data_.set("created", timestamp);
    data_.set("last_played", timestamp);
}

bool WorldConfig::loadFromFile() {
    std::ifstream file(configPath_, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    file.seekg(0, std::ios::end);
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);

    if (!file.good()) {
        return false;
    }

    auto loaded = DataContainer::fromCBOR(bytes);
    if (loaded) {
        data_ = std::move(*loaded);
    }

    dirty_ = false;
    return true;
}

bool WorldConfig::saveToFile() {
    std::filesystem::create_directories(worldDir_);

    auto bytes = data_.toCBOR();

    std::ofstream file(configPath_, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));

    if (!file.good()) {
        return false;
    }

    dirty_ = false;
    return true;
}

bool WorldConfig::save() {
    return saveToFile();
}

bool WorldConfig::reload() {
    setDefaults();
    return loadFromFile();
}

std::string WorldConfig::worldName() const {
    return data_.get<std::string>("name", "New World");
}

void WorldConfig::setWorldName(const std::string& name) {
    data_.set("name", name);
    dirty_ = true;
}

int64_t WorldConfig::seed() const {
    return data_.get<int64_t>("seed", 0);
}

void WorldConfig::setSeed(int64_t seed) {
    data_.set("seed", seed);
    dirty_ = true;
}

int64_t WorldConfig::createdTimestamp() const {
    return data_.get<int64_t>("created", 0);
}

int64_t WorldConfig::lastPlayedTimestamp() const {
    return data_.get<int64_t>("last_played", 0);
}

void WorldConfig::updateLastPlayed() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    data_.set("last_played", timestamp);
    dirty_ = true;
}

bool WorldConfig::compressionEnabled() const {
    // Check for world-specific override
    if (data_.has("compression.enabled")) {
        return data_.get<bool>("compression.enabled", true);
    }
    // Fall back to global config
    return ConfigManager::instance().compressionEnabled();
}

void WorldConfig::setCompressionEnabled(bool enabled) {
    data_.set("compression.enabled", enabled);
    dirty_ = true;
}

void WorldConfig::clearCompressionOverride() {
    data_.remove("compression.enabled");
    dirty_ = true;
}

bool WorldConfig::has(const std::string& key) const {
    return data_.has(key);
}

void WorldConfig::remove(const std::string& key) {
    data_.remove(key);
    dirty_ = true;
}

}  // namespace finevox
