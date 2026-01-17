#pragma once

#include "finevox/data_container.hpp"
#include <filesystem>
#include <shared_mutex>
#include <optional>
#include <string>

namespace finevox {

// ============================================================================
// ConfigManager - Global engine configuration
// ============================================================================
//
// Manages engine-wide settings stored in CBOR format.
// Settings can be queried and modified at runtime.
// Changes are persisted to disk on save() or destruction.
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
    bool loadFromFile();
    bool saveToFile();

    mutable std::shared_mutex mutex_;
    std::filesystem::path configPath_;
    DataContainer data_;
    bool initialized_ = false;
    bool dirty_ = false;
};

// ============================================================================
// WorldConfig - Per-world configuration
// ============================================================================
//
// Settings specific to a single world, stored in the world directory.
// Includes world metadata (name, seed) and per-world overrides.
//
class WorldConfig {
public:
    // Create/load world config from world directory
    explicit WorldConfig(const std::filesystem::path& worldDir);

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
    bool loadFromFile();
    bool saveToFile();
    void setDefaults();

    std::filesystem::path worldDir_;
    std::filesystem::path configPath_;
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
