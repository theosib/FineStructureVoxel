#pragma once

/**
 * @file script_cache.hpp
 * @brief File-mtime aware wrapper around ScriptEngine::loadScript()
 *
 * Tracks modification times so scripts can be hot-reloaded when changed.
 */

#include <finescript/script_engine.h>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace finevox::script {

class ScriptCache {
public:
    explicit ScriptCache(finescript::ScriptEngine& engine);

    /// Load script, checking file modification time.
    /// Returns nullptr if file doesn't exist or parse fails.
    finescript::CompiledScript* load(const std::string& path);

    /// Check all loaded scripts for file changes, reload if needed.
    /// Returns number of scripts reloaded.
    size_t reloadChanged();

    /// Force reload a specific script.
    void invalidate(const std::string& path);

private:
    finescript::ScriptEngine& engine_;
    struct CacheEntry {
        std::filesystem::file_time_type lastModified;
        std::string resolvedPath;
    };
    std::unordered_map<std::string, CacheEntry> entries_;
};

}  // namespace finevox::script
