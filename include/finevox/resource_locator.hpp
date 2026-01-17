#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace finevox {

// ============================================================================
// ResourceLocator - Unified path resolution for all engine resources
// ============================================================================
//
// Maps logical paths to physical filesystem paths. Understands scope hierarchy:
//   engine/   - Engine defaults (shipped with library)
//   game/     - Game assets (textures, shaders, etc.)
//   user/     - User-level settings
//   world/<name>/         - Per-world data
//   world/<name>/dim/<d>/ - Dimensions within world
//
// Thread safety: All public methods are thread-safe.
//
class ResourceLocator {
public:
    // Singleton access
    static ResourceLocator& instance();

    // ========================================================================
    // Root path configuration
    // ========================================================================

    // Set engine root (shipped defaults)
    void setEngineRoot(const std::filesystem::path& path);

    // Set game assets root (provided by game layer)
    void setGameRoot(const std::filesystem::path& path);

    // Set user settings root (e.g., ~/.config/finevox)
    // Handles ~ expansion on Unix-like systems
    void setUserRoot(const std::filesystem::path& path);

    // Get configured roots
    [[nodiscard]] std::filesystem::path engineRoot() const;
    [[nodiscard]] std::filesystem::path gameRoot() const;
    [[nodiscard]] std::filesystem::path userRoot() const;

    // ========================================================================
    // World/dimension management
    // ========================================================================

    // Register a world with its save directory
    void registerWorld(const std::string& name, const std::filesystem::path& path);

    // Unregister a world
    void unregisterWorld(const std::string& name);

    // Check if a world is registered
    [[nodiscard]] bool hasWorld(const std::string& name) const;

    // Get list of registered worlds
    [[nodiscard]] std::vector<std::string> registeredWorlds() const;

    // Register a dimension within a world
    // subpath is relative to world dir (default: "dim/<name>")
    void registerDimension(const std::string& world, const std::string& dim,
                          const std::string& subpath = "");

    // Check if a dimension is registered
    [[nodiscard]] bool hasDimension(const std::string& world, const std::string& dim) const;

    // ========================================================================
    // Path resolution
    // ========================================================================

    // Resolve a logical path to a physical path
    // Returns empty path if scope is unknown
    // Examples:
    //   "engine/defaults.cbor" → /usr/share/finevox/defaults.cbor
    //   "user/config.cbor" → ~/.config/finevox/config.cbor
    //   "world/MyWorld/world.cbor" → /path/to/saves/MyWorld/world.cbor
    //   "world/MyWorld/dim/nether/regions" → /path/to/saves/MyWorld/dim/nether/regions
    [[nodiscard]] std::filesystem::path resolve(const std::string& logicalPath) const;

    // Check if a logical path exists on disk
    [[nodiscard]] bool exists(const std::string& logicalPath) const;

    // ========================================================================
    // Convenience methods
    // ========================================================================

    // Get world root directory
    [[nodiscard]] std::filesystem::path worldPath(const std::string& name) const;

    // Get dimension directory within a world
    [[nodiscard]] std::filesystem::path dimensionPath(const std::string& world,
                                                      const std::string& dim) const;

    // Get region files directory for a world/dimension
    // dimension defaults to "overworld" (which uses world root, not dim/ subdir)
    [[nodiscard]] std::filesystem::path regionPath(const std::string& world,
                                                   const std::string& dim = "overworld") const;

    // ========================================================================
    // Utility
    // ========================================================================

    // Reset all state (for testing)
    void reset();

    // Expand ~ to home directory (utility function)
    [[nodiscard]] static std::filesystem::path expandHome(const std::filesystem::path& path);

    // Get platform-appropriate default user root
    [[nodiscard]] static std::filesystem::path defaultUserRoot();

private:
    ResourceLocator() = default;
    ~ResourceLocator() = default;

    ResourceLocator(const ResourceLocator&) = delete;
    ResourceLocator& operator=(const ResourceLocator&) = delete;

    // Parse logical path into scope and remainder
    // Returns {scope, remainder} or {empty, empty} if invalid
    [[nodiscard]] std::pair<std::string, std::string> parsePath(const std::string& path) const;

    mutable std::shared_mutex mutex_;

    std::filesystem::path engineRoot_;
    std::filesystem::path gameRoot_;
    std::filesystem::path userRoot_;

    // World name → world root path
    std::unordered_map<std::string, std::filesystem::path> worlds_;

    // "world/dim" → subpath (relative to world root)
    std::unordered_map<std::string, std::string> dimensions_;
};

}  // namespace finevox
