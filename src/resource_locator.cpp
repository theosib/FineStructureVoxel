#include "finevox/resource_locator.hpp"
#include <cstdlib>

namespace finevox {

ResourceLocator& ResourceLocator::instance() {
    static ResourceLocator instance;
    return instance;
}

// ============================================================================
// Root path configuration
// ============================================================================

void ResourceLocator::setEngineRoot(const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    engineRoot_ = path;
}

void ResourceLocator::setGameRoot(const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    gameRoot_ = path;
}

void ResourceLocator::setUserRoot(const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    userRoot_ = expandHome(path);
}

std::filesystem::path ResourceLocator::engineRoot() const {
    std::shared_lock lock(mutex_);
    return engineRoot_;
}

std::filesystem::path ResourceLocator::gameRoot() const {
    std::shared_lock lock(mutex_);
    return gameRoot_;
}

std::filesystem::path ResourceLocator::userRoot() const {
    std::shared_lock lock(mutex_);
    return userRoot_;
}

// ============================================================================
// World/dimension management
// ============================================================================

void ResourceLocator::registerWorld(const std::string& name, const std::filesystem::path& path) {
    std::unique_lock lock(mutex_);
    worlds_[name] = path;

    // Auto-register overworld dimension (uses world root directly)
    std::string key = name + "/overworld";
    dimensions_[key] = "";  // Empty subpath = world root
}

void ResourceLocator::unregisterWorld(const std::string& name) {
    std::unique_lock lock(mutex_);
    worlds_.erase(name);

    // Remove all dimensions for this world
    auto it = dimensions_.begin();
    while (it != dimensions_.end()) {
        if (it->first.starts_with(name + "/")) {
            it = dimensions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool ResourceLocator::hasWorld(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return worlds_.contains(name);
}

std::vector<std::string> ResourceLocator::registeredWorlds() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(worlds_.size());
    for (const auto& [name, path] : worlds_) {
        result.push_back(name);
    }
    return result;
}

void ResourceLocator::registerDimension(const std::string& world, const std::string& dim,
                                        const std::string& subpath) {
    std::unique_lock lock(mutex_);

    std::string key = world + "/" + dim;
    if (subpath.empty()) {
        // Default subpath: dim/<name>
        dimensions_[key] = "dim/" + dim;
    } else {
        dimensions_[key] = subpath;
    }
}

bool ResourceLocator::hasDimension(const std::string& world, const std::string& dim) const {
    std::shared_lock lock(mutex_);
    std::string key = world + "/" + dim;
    return dimensions_.contains(key);
}

// ============================================================================
// Path resolution
// ============================================================================

std::pair<std::string, std::string> ResourceLocator::parsePath(const std::string& path) const {
    // Find first slash
    auto pos = path.find('/');
    if (pos == std::string::npos) {
        // No slash - scope only, no remainder
        return {path, ""};
    }

    std::string scope = path.substr(0, pos);
    std::string remainder = path.substr(pos + 1);
    return {scope, remainder};
}

std::filesystem::path ResourceLocator::resolve(const std::string& logicalPath) const {
    std::shared_lock lock(mutex_);

    auto [scope, remainder] = parsePath(logicalPath);

    if (scope == "engine") {
        if (engineRoot_.empty()) return {};
        return remainder.empty() ? engineRoot_ : engineRoot_ / remainder;
    }

    if (scope == "game") {
        if (gameRoot_.empty()) return {};
        return remainder.empty() ? gameRoot_ : gameRoot_ / remainder;
    }

    if (scope == "user") {
        if (userRoot_.empty()) return {};
        return remainder.empty() ? userRoot_ : userRoot_ / remainder;
    }

    if (scope == "world") {
        // Parse world name from remainder
        // "world/MyWorld/regions" → world="MyWorld", rest="regions"
        if (remainder.empty()) return {};

        auto slashPos = remainder.find('/');
        std::string worldName;
        std::string rest;

        if (slashPos == std::string::npos) {
            worldName = remainder;
        } else {
            worldName = remainder.substr(0, slashPos);
            rest = remainder.substr(slashPos + 1);
        }

        auto worldIt = worlds_.find(worldName);
        if (worldIt == worlds_.end()) return {};

        const auto& worldRoot = worldIt->second;

        if (rest.empty()) {
            return worldRoot;
        }

        // Check if rest starts with "dim/"
        if (rest.starts_with("dim/")) {
            // Parse dimension name
            // "dim/nether/regions" → dim="nether", dimRest="regions"
            std::string dimPart = rest.substr(4);  // Skip "dim/"
            auto dimSlash = dimPart.find('/');

            std::string dimName;
            std::string dimRest;

            if (dimSlash == std::string::npos) {
                dimName = dimPart;
            } else {
                dimName = dimPart.substr(0, dimSlash);
                dimRest = dimPart.substr(dimSlash + 1);
            }

            // Look up dimension subpath
            std::string dimKey = worldName + "/" + dimName;
            auto dimIt = dimensions_.find(dimKey);

            std::filesystem::path dimPath;
            if (dimIt != dimensions_.end() && !dimIt->second.empty()) {
                dimPath = worldRoot / dimIt->second;
            } else {
                // Default: dim/<name>
                dimPath = worldRoot / "dim" / dimName;
            }

            return dimRest.empty() ? dimPath : dimPath / dimRest;
        }

        return worldRoot / rest;
    }

    // Unknown scope
    return {};
}

bool ResourceLocator::exists(const std::string& logicalPath) const {
    auto resolved = resolve(logicalPath);
    if (resolved.empty()) return false;
    return std::filesystem::exists(resolved);
}

// ============================================================================
// Convenience methods
// ============================================================================

std::filesystem::path ResourceLocator::worldPath(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = worlds_.find(name);
    if (it == worlds_.end()) return {};
    return it->second;
}

std::filesystem::path ResourceLocator::dimensionPath(const std::string& world,
                                                     const std::string& dim) const {
    return resolve("world/" + world + "/dim/" + dim);
}

std::filesystem::path ResourceLocator::regionPath(const std::string& world,
                                                  const std::string& dim) const {
    if (dim == "overworld" || dim.empty()) {
        // Overworld regions are in world root
        return resolve("world/" + world + "/regions");
    }
    // Other dimensions use dim/<name>/regions
    return resolve("world/" + world + "/dim/" + dim + "/regions");
}

// ============================================================================
// Utility
// ============================================================================

void ResourceLocator::reset() {
    std::unique_lock lock(mutex_);
    engineRoot_.clear();
    gameRoot_.clear();
    userRoot_.clear();
    worlds_.clear();
    dimensions_.clear();
}

std::filesystem::path ResourceLocator::expandHome(const std::filesystem::path& path) {
    std::string pathStr = path.string();

    if (pathStr.empty() || pathStr[0] != '~') {
        return path;
    }

    // Get home directory
    const char* home = std::getenv("HOME");
    if (!home) {
        // Try platform-specific alternatives
#ifdef _WIN32
        home = std::getenv("USERPROFILE");
#endif
    }

    if (!home) {
        return path;  // Can't expand, return as-is
    }

    if (pathStr.size() == 1) {
        // Just "~"
        return std::filesystem::path(home);
    }

    if (pathStr[1] == '/' || pathStr[1] == '\\') {
        // "~/..." - replace ~ with home
        return std::filesystem::path(home) / pathStr.substr(2);
    }

    // "~user/..." - not supported, return as-is
    return path;
}

std::filesystem::path ResourceLocator::defaultUserRoot() {
#ifdef __APPLE__
    // macOS: ~/Library/Application Support/finevox
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / "Library" / "Application Support" / "finevox";
    }
#elif defined(_WIN32)
    // Windows: %APPDATA%/finevox
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::filesystem::path(appdata) / "finevox";
    }
#else
    // Linux/Unix: ~/.config/finevox (XDG standard)
    const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
    if (xdgConfig) {
        return std::filesystem::path(xdgConfig) / "finevox";
    }
    const char* home = std::getenv("HOME");
    if (home) {
        return std::filesystem::path(home) / ".config" / "finevox";
    }
#endif

    // Fallback: current directory
    return std::filesystem::current_path() / "finevox_config";
}

}  // namespace finevox
