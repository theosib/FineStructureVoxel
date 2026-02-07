#include "finevox/config.hpp"
#include "finevox/config_file.hpp"
#include "finevox/resource_locator.hpp"
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

    // Pre-intern all known config keys (done once)
    initKeys();

    configPath_ = configPath;
    configFile_ = std::make_unique<ConfigFile>();
    configFile_->setHeader("# FineVox Engine Configuration\n# Edit with care\n");

    setDefaults();

    if (std::filesystem::exists(configPath_)) {
        configFile_->load(configPath_);
        // Sync loaded values to data_
        syncFromFile();
    }

    initialized_ = true;
}

void ConfigManager::initKeys() {
    keyCompressionEnabled_ = internKey("compression.enabled");
    keyDebugLogging_ = internKey("debug.logging");
    keyIoThreadCount_ = internKey("io.thread_count");
    keyMaxOpenRegions_ = internKey("io.max_open_regions");
    keyColumnCacheSize_ = internKey("cache.column_size");
    keyBlockPlacementMode_ = internKey("physics.block_placement_mode");
}

void ConfigManager::initFromLocator() {
    auto path = ResourceLocator::instance().resolve("user/config.conf");
    if (path.empty()) {
        // Use default user root if not configured
        path = ResourceLocator::defaultUserRoot() / "config.conf";
    }
    init(path);
}

bool ConfigManager::isInitialized() const {
    std::shared_lock lock(mutex_);
    return initialized_;
}

bool ConfigManager::save() {
    std::unique_lock lock(mutex_);
    if (!initialized_ || !configFile_) return false;

    // Sync data_ to file
    syncToFile();

    if (configFile_->saveAs(configPath_)) {
        dirty_ = false;
        return true;
    }
    return false;
}

bool ConfigManager::reload() {
    std::unique_lock lock(mutex_);
    if (!initialized_) return false;

    setDefaults();
    if (configFile_ && configFile_->load(configPath_)) {
        syncFromFile();
        dirty_ = false;
        return true;
    }
    return false;
}

void ConfigManager::reset() {
    std::unique_lock lock(mutex_);
    data_.clear();
    configFile_.reset();
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
    data_.set(keyCompressionEnabled_, true);
    data_.set(keyDebugLogging_, false);
    data_.set(keyIoThreadCount_, 2);
    data_.set(keyMaxOpenRegions_, 16);
    data_.set(keyColumnCacheSize_, 64);
    data_.set(keyBlockPlacementMode_, std::string("block"));
}

void ConfigManager::syncFromFile() {
    if (!configFile_) return;

    // Copy values from ConfigFile to DataContainer (using pre-interned keys)
    if (configFile_->has("compression.enabled")) {
        data_.set(keyCompressionEnabled_, configFile_->getBool("compression.enabled", true));
    }
    if (configFile_->has("debug.logging")) {
        data_.set(keyDebugLogging_, configFile_->getBool("debug.logging", false));
    }
    if (configFile_->has("io.thread_count")) {
        data_.set(keyIoThreadCount_, configFile_->getInt("io.thread_count", 2));
    }
    if (configFile_->has("io.max_open_regions")) {
        data_.set(keyMaxOpenRegions_, configFile_->getInt("io.max_open_regions", 16));
    }
    if (configFile_->has("cache.column_size")) {
        data_.set(keyColumnCacheSize_, configFile_->getInt("cache.column_size", 64));
    }
    if (configFile_->has("physics.block_placement_mode")) {
        data_.set(keyBlockPlacementMode_, configFile_->getString("physics.block_placement_mode", "block"));
    }
}

void ConfigManager::syncToFile() {
    if (!configFile_) return;

    // Copy values from DataContainer to ConfigFile (using pre-interned keys for reads)
    configFile_->set("compression.enabled", data_.get<bool>(keyCompressionEnabled_, true));
    configFile_->set("debug.logging", data_.get<bool>(keyDebugLogging_, false));
    configFile_->set("io.thread_count", data_.get<int64_t>(keyIoThreadCount_, 2));
    configFile_->set("io.max_open_regions", data_.get<int64_t>(keyMaxOpenRegions_, 16));
    configFile_->set("cache.column_size", data_.get<int64_t>(keyColumnCacheSize_, 64));
    configFile_->set("physics.block_placement_mode",
        std::string_view(data_.get<std::string>(keyBlockPlacementMode_, "block")));
}

// Typed accessors (use pre-interned keys for fast lookups)

bool ConfigManager::compressionEnabled() const {
    std::shared_lock lock(mutex_);
    return data_.get<bool>(keyCompressionEnabled_, true);
}

void ConfigManager::setCompressionEnabled(bool enabled) {
    std::unique_lock lock(mutex_);
    data_.set(keyCompressionEnabled_, enabled);
    dirty_ = true;
}

bool ConfigManager::debugLogging() const {
    std::shared_lock lock(mutex_);
    return data_.get<bool>(keyDebugLogging_, false);
}

void ConfigManager::setDebugLogging(bool enabled) {
    std::unique_lock lock(mutex_);
    data_.set(keyDebugLogging_, enabled);
    dirty_ = true;
}

int ConfigManager::ioThreadCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<int>(data_.get<int64_t>(keyIoThreadCount_, 2));
}

void ConfigManager::setIOThreadCount(int count) {
    std::unique_lock lock(mutex_);
    data_.set(keyIoThreadCount_, static_cast<int64_t>(count));
    dirty_ = true;
}

size_t ConfigManager::maxOpenRegions() const {
    std::shared_lock lock(mutex_);
    return static_cast<size_t>(data_.get<int64_t>(keyMaxOpenRegions_, 16));
}

void ConfigManager::setMaxOpenRegions(size_t count) {
    std::unique_lock lock(mutex_);
    data_.set(keyMaxOpenRegions_, static_cast<int64_t>(count));
    dirty_ = true;
}

size_t ConfigManager::columnCacheSize() const {
    std::shared_lock lock(mutex_);
    return static_cast<size_t>(data_.get<int64_t>(keyColumnCacheSize_, 64));
}

void ConfigManager::setColumnCacheSize(size_t count) {
    std::unique_lock lock(mutex_);
    data_.set(keyColumnCacheSize_, static_cast<int64_t>(count));
    dirty_ = true;
}

std::string ConfigManager::blockPlacementMode() const {
    std::shared_lock lock(mutex_);
    return data_.get<std::string>(keyBlockPlacementMode_, "block");
}

void ConfigManager::setBlockPlacementMode(const std::string& mode) {
    std::unique_lock lock(mutex_);
    // Validate: only "block" or "push" are valid
    if (mode == "block" || mode == "push") {
        data_.set(keyBlockPlacementMode_, mode);
        dirty_ = true;
    }
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
    , configPath_(worldDir / "world.conf")
    , configFile_(std::make_unique<ConfigFile>()) {

    // Pre-intern all known config keys (done once per instance)
    initKeys();

    configFile_->setHeader("# World Configuration\n");

    setDefaults();

    if (std::filesystem::exists(configPath_)) {
        configFile_->load(configPath_);
        syncFromFile();
    }
}

void WorldConfig::initKeys() {
    keyName_ = internKey("name");
    keySeed_ = internKey("seed");
    keyCreated_ = internKey("created");
    keyLastPlayed_ = internKey("last_played");
    keyCompressionEnabled_ = internKey("compression.enabled");
}

WorldConfig::~WorldConfig() = default;
WorldConfig::WorldConfig(WorldConfig&&) noexcept = default;
WorldConfig& WorldConfig::operator=(WorldConfig&&) noexcept = default;

std::optional<WorldConfig> WorldConfig::fromWorld(const std::string& worldName) {
    auto worldPath = ResourceLocator::instance().worldPath(worldName);
    if (worldPath.empty()) {
        return std::nullopt;
    }
    return WorldConfig(worldPath);
}

void WorldConfig::setDefaults() {
    data_.clear();
    data_.set(keyName_, std::string("New World"));
    data_.set(keySeed_, static_cast<int64_t>(0));

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();

    data_.set(keyCreated_, timestamp);
    data_.set(keyLastPlayed_, timestamp);
}

void WorldConfig::syncFromFile() {
    if (!configFile_) return;

    // Sync known keys from file to data_ (using pre-interned keys for writes)
    if (configFile_->has("name")) {
        data_.set(keyName_, configFile_->getString("name", "New World"));
    }
    if (configFile_->has("seed")) {
        data_.set(keySeed_, configFile_->getInt("seed", 0));
    }
    if (configFile_->has("created")) {
        data_.set(keyCreated_, configFile_->getInt("created", 0));
    }
    if (configFile_->has("last_played")) {
        data_.set(keyLastPlayed_, configFile_->getInt("last_played", 0));
    }
    if (configFile_->has("compression.enabled")) {
        data_.set(keyCompressionEnabled_, configFile_->getBool("compression.enabled", true));
    }

    // Also sync any custom keys from file's DataContainer
    configFile_->data().forEach([this](DataKey key, const DataValue& value) {
        // Skip keys we've already handled (compare by interned key)
        if (key == keyName_ || key == keySeed_ || key == keyCreated_ ||
            key == keyLastPlayed_ || key == keyCompressionEnabled_) {
            return;
        }

        std::visit([this, key](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                data_.set(key, val);
            } else if constexpr (std::is_same_v<T, double>) {
                data_.set(key, val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                data_.set(key, val);
            }
        }, value);
    });
}

void WorldConfig::syncToFile() {
    if (!configFile_) return;

    // Sync all data_ entries to ConfigFile
    // Note: ConfigFile API still uses strings, so we lookup key names here
    data_.forEach([this](DataKey key, const DataValue& value) {
        auto keyStr = lookupKey(key);

        std::visit([this, key, &keyStr](auto&& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // Skip null values
            } else if constexpr (std::is_same_v<T, int64_t>) {
                // Check if this is a known boolean key
                if (key == keyCompressionEnabled_ && (val == 0 || val == 1)) {
                    configFile_->set(keyStr, val != 0);
                } else {
                    configFile_->set(keyStr, val);
                }
            } else if constexpr (std::is_same_v<T, double>) {
                configFile_->set(keyStr, val);
            } else if constexpr (std::is_same_v<T, std::string>) {
                configFile_->set(keyStr, std::string_view(val));
            }
        }, value);
    });
}

bool WorldConfig::save() {
    std::filesystem::create_directories(worldDir_);
    syncToFile();
    if (configFile_->saveAs(configPath_)) {
        dirty_ = false;
        return true;
    }
    return false;
}

bool WorldConfig::reload() {
    setDefaults();
    if (configFile_ && configFile_->load(configPath_)) {
        syncFromFile();
        dirty_ = false;
        return true;
    }
    return false;
}

std::string WorldConfig::worldName() const {
    return data_.get<std::string>(keyName_, "New World");
}

void WorldConfig::setWorldName(const std::string& name) {
    data_.set(keyName_, name);
    dirty_ = true;
}

int64_t WorldConfig::seed() const {
    return data_.get<int64_t>(keySeed_, 0);
}

void WorldConfig::setSeed(int64_t seed) {
    data_.set(keySeed_, seed);
    dirty_ = true;
}

int64_t WorldConfig::createdTimestamp() const {
    return data_.get<int64_t>(keyCreated_, 0);
}

int64_t WorldConfig::lastPlayedTimestamp() const {
    return data_.get<int64_t>(keyLastPlayed_, 0);
}

void WorldConfig::updateLastPlayed() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()
    ).count();
    data_.set(keyLastPlayed_, timestamp);
    dirty_ = true;
}

bool WorldConfig::compressionEnabled() const {
    // Check for world-specific override (using pre-interned key)
    if (data_.has(keyCompressionEnabled_)) {
        return data_.get<bool>(keyCompressionEnabled_, true);
    }
    // Fall back to global config
    return ConfigManager::instance().compressionEnabled();
}

void WorldConfig::setCompressionEnabled(bool enabled) {
    data_.set(keyCompressionEnabled_, enabled);
    dirty_ = true;
}

void WorldConfig::clearCompressionOverride() {
    data_.remove(keyCompressionEnabled_);
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
