#pragma once

/**
 * @file module.hpp
 * @brief Game module loading, registration, and lifecycle
 *
 * Design: [18-modules.md] §18.4 ModuleLoader
 */

#include <string>
#include <string_view>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <filesystem>

namespace finevox {

// Forward declarations
class BlockRegistry;
class EntityRegistry;
class ItemRegistry;
class ModuleRegistry;

// ============================================================================
// GameModule - Interface for loadable game modules
// ============================================================================

/**
 * @brief Base interface for game modules (plugins)
 *
 * Modules are loaded at startup and register blocks, entities, items, etc.
 * They can be built-in (compiled into the executable) or loaded from shared
 * objects (.so/.dll files).
 *
 * Module lifecycle:
 * 1. Module is loaded (shared object opened or built-in registered)
 * 2. onLoad() called after all dependencies are loaded
 * 3. onRegister() called to register blocks, entities, items
 * 4. Game runs...
 * 5. onUnload() called during shutdown (reverse order of loading)
 *
 * Modules are stateless - they register content types but don't hold game state.
 * State is stored in the world (SubChunk extra data, entity components, etc.)
 */
class GameModule {
public:
    virtual ~GameModule() = default;

    /**
     * @brief Get the module's unique identifier
     *
     * This is also the namespace prefix for all content registered by this module.
     * For example, a module named "blockgame" registers blocks like "blockgame:stone".
     *
     * @return Module name (e.g., "blockgame", "mymod")
     */
    [[nodiscard]] virtual std::string_view name() const = 0;

    /**
     * @brief Get the module's version string
     * @return Version (e.g., "1.0.0", "2.3.1-beta")
     */
    [[nodiscard]] virtual std::string_view version() const = 0;

    /**
     * @brief Get list of module names this module depends on
     *
     * Dependencies are loaded before this module. If a dependency is missing,
     * the module fails to load.
     *
     * @return List of dependency module names
     */
    [[nodiscard]] virtual std::vector<std::string_view> dependencies() const {
        return {};
    }

    /**
     * @brief Called after the module and its dependencies are loaded
     *
     * Use this for initialization that needs other modules to be present.
     * The registry provides access to content from dependency modules.
     *
     * @param registry Access to global registries
     */
    virtual void onLoad(ModuleRegistry& registry) {
        (void)registry;  // Unused by default
    }

    /**
     * @brief Register blocks, entities, items, etc.
     *
     * This is the main entry point for module content registration.
     * Called after onLoad() completes for all modules.
     *
     * @param registry Access to global registries with namespace auto-prefixing
     */
    virtual void onRegister(ModuleRegistry& registry) = 0;

    /**
     * @brief Called before the module is unloaded
     *
     * Use for cleanup. Called in reverse order of loading (dependents first).
     */
    virtual void onUnload() {}
};

// ============================================================================
// ModuleRegistry - Provides access to registries during module initialization
// ============================================================================

/**
 * @brief Context provided to modules during registration
 *
 * Provides access to global registries and automatically prefixes
 * registered content with the module's namespace.
 */
class ModuleRegistry {
public:
    /**
     * @brief Construct registry context for a module
     * @param moduleNamespace The module's namespace (its name)
     * @param blocks Reference to global block registry
     * @param entities Reference to global entity registry
     * @param items Reference to global item registry
     */
    ModuleRegistry(std::string_view moduleNamespace,
                   BlockRegistry& blocks,
                   EntityRegistry& entities,
                   ItemRegistry& items);

    /**
     * @brief Get this module's namespace
     * @return Namespace string (e.g., "blockgame")
     */
    [[nodiscard]] std::string_view moduleNamespace() const { return namespace_; }

    /**
     * @brief Get direct access to the block registry
     * @return Reference to global BlockRegistry
     */
    [[nodiscard]] BlockRegistry& blocks() { return blocks_; }

    /**
     * @brief Get direct access to the entity registry
     * @return Reference to global EntityRegistry
     */
    [[nodiscard]] EntityRegistry& entities() { return entities_; }

    /**
     * @brief Get direct access to the item registry
     * @return Reference to global ItemRegistry
     */
    [[nodiscard]] ItemRegistry& items() { return items_; }

    /**
     * @brief Build a fully-qualified name with this module's namespace
     *
     * Convenience method that prefixes a local name with the module namespace.
     * Example: qualifiedName("stone") -> "blockgame:stone"
     *
     * @param localName Name without namespace prefix
     * @return Fully-qualified name with namespace
     */
    [[nodiscard]] std::string qualifiedName(std::string_view localName) const;

    // Logging (outputs to engine log with module prefix)
    void log(std::string_view message) const;
    void warn(std::string_view message) const;
    void error(std::string_view message) const;

private:
    std::string namespace_;
    BlockRegistry& blocks_;
    EntityRegistry& entities_;
    ItemRegistry& items_;
};

// ============================================================================
// ModuleLoader - Loads and manages game modules
// ============================================================================

/**
 * @brief Manages loading and lifecycle of game modules
 *
 * Handles both built-in modules and modules loaded from shared objects.
 * Resolves dependencies and ensures correct initialization order.
 */
class ModuleLoader {
public:
    ModuleLoader();
    ~ModuleLoader();

    // Non-copyable (owns module handles)
    ModuleLoader(const ModuleLoader&) = delete;
    ModuleLoader& operator=(const ModuleLoader&) = delete;

    /**
     * @brief Load a module from a shared object file
     *
     * The shared object must export:
     *   extern "C" finevox::GameModule* finevox_create_module();
     *
     * @param path Path to .so/.dll file
     * @return true if loaded successfully
     */
    bool load(const std::filesystem::path& path);

    /**
     * @brief Register a built-in module
     *
     * Use this for modules compiled directly into the executable,
     * or for testing with mock modules.
     *
     * @param module Module instance (takes ownership)
     * @return true if registered successfully (no duplicate name)
     */
    bool registerBuiltin(std::unique_ptr<GameModule> module);

    /**
     * @brief Initialize all loaded modules
     *
     * Resolves dependencies, calls onLoad() and onRegister() in correct order.
     * Uses the provided registries for content registration.
     *
     * @param blocks Global block registry
     * @param entities Global entity registry
     * @param items Global item registry
     * @return true if all modules initialized successfully
     */
    bool initializeAll(BlockRegistry& blocks, EntityRegistry& entities, ItemRegistry& items);

    /**
     * @brief Shutdown all modules
     *
     * Calls onUnload() in reverse initialization order.
     */
    void shutdownAll();

    /**
     * @brief Get a loaded module by name
     * @param name Module name
     * @return Pointer to module, or nullptr if not found
     */
    [[nodiscard]] GameModule* getModule(std::string_view name);

    /**
     * @brief Get list of all loaded module names
     * @return Vector of module names in load order
     */
    [[nodiscard]] std::vector<std::string_view> loadedModules() const;

    /**
     * @brief Check if a module is loaded
     * @param name Module name
     * @return true if module is loaded
     */
    [[nodiscard]] bool hasModule(std::string_view name) const;

    /**
     * @brief Get number of loaded modules
     */
    [[nodiscard]] size_t moduleCount() const;

private:
    struct LoadedModule {
        std::unique_ptr<GameModule> module;
        void* handle = nullptr;  // dlopen handle (nullptr for built-in)
        bool initialized = false;
    };

    // Topological sort for dependency resolution
    // Returns modules in initialization order, or empty if cycle detected
    std::vector<std::string> resolveDependencies() const;

    // Module storage (name -> module)
    std::unordered_map<std::string, LoadedModule> modules_;

    // Initialization order (filled by initializeAll)
    std::vector<std::string> initOrder_;
};

// ============================================================================
// Module entry point macro
// ============================================================================

/**
 * @brief Macro to define the module entry point in a shared object
 *
 * Usage in your module's .cpp file:
 *
 *   class MyModule : public finevox::GameModule {
 *       // ... implementation ...
 *   };
 *
 *   FINEVOX_MODULE(MyModule)
 */
#define FINEVOX_MODULE(ModuleClass) \
    extern "C" finevox::GameModule* finevox_create_module() { \
        return new ModuleClass(); \
    }

}  // namespace finevox
