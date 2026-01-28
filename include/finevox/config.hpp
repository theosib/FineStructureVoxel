#pragma once

/**
 * @file config.hpp
 * @brief Engine and world configuration management
 *
 * Design: [23-distance-and-loading.md] §23.2 Configuration
 */

#include "finevox/data_container.hpp"
#include <filesystem>
#include <shared_mutex>
#include <optional>
#include <string>
#include <memory>

namespace finevox {

// Forward declaration
class ConfigFile;

// ============================================================================
// ConfigManager - Global engine configuration
// ============================================================================
//
// Manages engine-wide settings stored in human-readable text format.
// Settings can be queried and modified at runtime.
// Changes are persisted to disk on save() or destruction.
//
// Config file format (key: value pairs):
//   compression.enabled: true
//   debug.logging: false
//   io.thread_count: 2
//
// Thread safety: All public methods are thread-safe.
//
class ConfigManager {
public:
    // Singleton access
    static ConfigManager& instance();

    // Initialize with config file path
    // If file doesn't exist, uses defaults
    void init(const std::filesystem::path& configPath);

    // Initialize using ResourceLocator to find user config
    // Resolves "user/config.conf" via ResourceLocator
    // Requires ResourceLocator::instance().setUserRoot() to be called first
    void initFromLocator();

    // Check if initialized
    [[nodiscard]] bool isInitialized() const;

    // Save current config to disk
    bool save();

    // Reload config from disk (discards unsaved changes)
    bool reload();

    // Reset to uninitialized state (for testing)
    void reset();

    // Get config file path
    [[nodiscard]] std::filesystem::path configPath() const;

    // ========================================================================
    // Typed accessors for common settings
    // ========================================================================

    // Compression settings
    [[nodiscard]] bool compressionEnabled() const;
    void setCompressionEnabled(bool enabled);

    // Debug settings
    [[nodiscard]] bool debugLogging() const;
    void setDebugLogging(bool enabled);

    // I/O settings
    [[nodiscard]] int ioThreadCount() const;
    void setIOThreadCount(int count);

    [[nodiscard]] size_t maxOpenRegions() const;
    void setMaxOpenRegions(size_t count);

    // Cache settings
    [[nodiscard]] size_t columnCacheSize() const;
    void setColumnCacheSize(size_t count);

    // ========================================================================
    // Generic accessors (for custom settings)
    // ========================================================================

    template<typename T>
    [[nodiscard]] std::optional<T> get(const std::string& key) const;

    template<typename T>
    void set(const std::string& key, const T& value);

    [[nodiscard]] bool has(const std::string& key) const;
    void remove(const std::string& key);

private:
    ConfigManager() = default;
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    void setDefaults();
    void syncFromFile();
    void syncToFile();

    mutable std::shared_mutex mutex_;
    std::filesystem::path configPath_;
    std::unique_ptr<ConfigFile> configFile_;
    DataContainer data_;
    bool initialized_ = false;
    bool dirty_ = false;
};

// ============================================================================
// WorldConfig - Per-world configuration
// ============================================================================
//
// Settings specific to a single world, stored in human-readable format
// in the world directory (world.conf).
// Includes world metadata (name, seed) and per-world overrides.
//
class WorldConfig {
public:
    // Create/load world config from world directory
    explicit WorldConfig(const std::filesystem::path& worldDir);

    // Move-only (has unique_ptr member)
    ~WorldConfig();
    WorldConfig(WorldConfig&&) noexcept;
    WorldConfig& operator=(WorldConfig&&) noexcept;
    WorldConfig(const WorldConfig&) = delete;
    WorldConfig& operator=(const WorldConfig&) = delete;

    // Create/load world config using ResourceLocator
    // Resolves "world/<name>/world.conf" via ResourceLocator
    // Requires the world to be registered with ResourceLocator first
    static std::optional<WorldConfig> fromWorld(const std::string& worldName);

    // Save config to disk
    bool save();

    // Reload from disk
    bool reload();

    // ========================================================================
    // World metadata
    // ========================================================================

    [[nodiscard]] std::string worldName() const;
    void setWorldName(const std::string& name);

    [[nodiscard]] int64_t seed() const;
    void setSeed(int64_t seed);

    [[nodiscard]] int64_t createdTimestamp() const;
    [[nodiscard]] int64_t lastPlayedTimestamp() const;
    void updateLastPlayed();

    // ========================================================================
    // Per-world settings (override global config)
    // ========================================================================

    // Returns world-specific setting, or falls back to global ConfigManager
    [[nodiscard]] bool compressionEnabled() const;
    void setCompressionEnabled(bool enabled);
    void clearCompressionOverride();  // Use global setting

    // ========================================================================
    // Generic accessors
    // ========================================================================

    template<typename T>
    [[nodiscard]] std::optional<T> get(const std::string& key) const;

    template<typename T>
    void set(const std::string& key, const T& value);

    [[nodiscard]] bool has(const std::string& key) const;
    void remove(const std::string& key);

    // Get the underlying data container (for serialization)
    [[nodiscard]] const DataContainer& data() const { return data_; }

private:
    void setDefaults();
    void syncFromFile();
    void syncToFile();

    std::filesystem::path worldDir_;
    std::filesystem::path configPath_;
    std::unique_ptr<ConfigFile> configFile_;
    DataContainer data_;
    bool dirty_ = false;
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename T>
std::optional<T> ConfigManager::get(const std::string& key) const {
    std::shared_lock lock(mutex_);
    if (!data_.has(key)) {
        return std::nullopt;
    }
    return data_.get<T>(key);
}

template<typename T>
void ConfigManager::set(const std::string& key, const T& value) {
    std::unique_lock lock(mutex_);
    data_.set(key, value);
    dirty_ = true;
}

template<typename T>
std::optional<T> WorldConfig::get(const std::string& key) const {
    if (!data_.has(key)) {
        return std::nullopt;
    }
    return data_.get<T>(key);
}

template<typename T>
void WorldConfig::set(const std::string& key, const T& value) {
    data_.set(key, value);
    dirty_ = true;
}

}  // namespace finevox
