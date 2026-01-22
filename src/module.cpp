#include "finevox/module.hpp"
#include "finevox/block_type.hpp"
#include "finevox/entity_registry.hpp"
#include "finevox/item_registry.hpp"

#include <iostream>
#include <algorithm>
#include <queue>
#include <unordered_set>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace finevox {

// ============================================================================
// ModuleRegistry Implementation
// ============================================================================

ModuleRegistry::ModuleRegistry(std::string_view moduleNamespace,
                               BlockRegistry& blocks,
                               EntityRegistry& entities,
                               ItemRegistry& items)
    : namespace_(moduleNamespace)
    , blocks_(blocks)
    , entities_(entities)
    , items_(items)
{
}

std::string ModuleRegistry::qualifiedName(std::string_view localName) const {
    return BlockRegistry::makeQualifiedName(namespace_, localName);
}

void ModuleRegistry::log(std::string_view message) const {
    std::cout << "[" << namespace_ << "] " << message << "\n";
}

void ModuleRegistry::warn(std::string_view message) const {
    std::cerr << "[" << namespace_ << "] WARNING: " << message << "\n";
}

void ModuleRegistry::error(std::string_view message) const {
    std::cerr << "[" << namespace_ << "] ERROR: " << message << "\n";
}

// ============================================================================
// ModuleLoader Implementation
// ============================================================================

ModuleLoader::ModuleLoader() = default;

ModuleLoader::~ModuleLoader() {
    shutdownAll();

    // Close all shared object handles
    for (auto& [name, loaded] : modules_) {
        if (loaded.handle) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(loaded.handle));
#else
            dlclose(loaded.handle);
#endif
        }
    }
}

bool ModuleLoader::load(const std::filesystem::path& path) {
    // Open the shared object
    void* handle = nullptr;

#ifdef _WIN32
    handle = LoadLibraryW(path.c_str());
    if (!handle) {
        std::cerr << "Failed to load module: " << path << " (error " << GetLastError() << ")\n";
        return false;
    }
#else
    handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "Failed to load module: " << path << " (" << dlerror() << ")\n";
        return false;
    }
#endif

    // Look up the entry point
    using CreateFunc = GameModule* (*)();
    CreateFunc createFunc = nullptr;

#ifdef _WIN32
    createFunc = reinterpret_cast<CreateFunc>(
        GetProcAddress(static_cast<HMODULE>(handle), "finevox_create_module"));
#else
    createFunc = reinterpret_cast<CreateFunc>(dlsym(handle, "finevox_create_module"));
#endif

    if (!createFunc) {
        std::cerr << "Module " << path << " missing finevox_create_module entry point\n";
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Create the module instance
    GameModule* module = createFunc();
    if (!module) {
        std::cerr << "Module " << path << " finevox_create_module returned null\n";
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Check for duplicate name
    std::string name(module->name());
    if (modules_.find(name) != modules_.end()) {
        std::cerr << "Module " << name << " already loaded\n";
        delete module;
#ifdef _WIN32
        FreeLibrary(static_cast<HMODULE>(handle));
#else
        dlclose(handle);
#endif
        return false;
    }

    // Store the module
    LoadedModule& loaded = modules_[name];
    loaded.module = std::unique_ptr<GameModule>(module);
    loaded.handle = handle;
    loaded.initialized = false;

    std::cout << "Loaded module: " << name << " v" << module->version() << " from " << path << "\n";
    return true;
}

bool ModuleLoader::registerBuiltin(std::unique_ptr<GameModule> module) {
    if (!module) {
        return false;
    }

    std::string name(module->name());
    if (modules_.find(name) != modules_.end()) {
        std::cerr << "Module " << name << " already registered\n";
        return false;
    }

    LoadedModule& loaded = modules_[name];
    loaded.module = std::move(module);
    loaded.handle = nullptr;  // Built-in, no handle
    loaded.initialized = false;

    std::cout << "Registered built-in module: " << name << " v" << loaded.module->version() << "\n";
    return true;
}

std::vector<std::string> ModuleLoader::resolveDependencies() const {
    // Build dependency graph and compute in-degrees
    std::unordered_map<std::string, std::vector<std::string>> dependents;  // module -> modules that depend on it
    std::unordered_map<std::string, int> inDegree;

    // Initialize
    for (const auto& [name, loaded] : modules_) {
        inDegree[name] = 0;
    }

    // Build graph
    for (const auto& [name, loaded] : modules_) {
        for (const auto& dep : loaded.module->dependencies()) {
            std::string depName(dep);

            // Check if dependency exists
            if (modules_.find(depName) == modules_.end()) {
                std::cerr << "Module " << name << " depends on missing module " << depName << "\n";
                return {};  // Dependency resolution failed
            }

            dependents[depName].push_back(name);
            inDegree[name]++;
        }
    }

    // Kahn's algorithm for topological sort
    std::queue<std::string> ready;
    for (const auto& [name, degree] : inDegree) {
        if (degree == 0) {
            ready.push(name);
        }
    }

    std::vector<std::string> order;
    while (!ready.empty()) {
        std::string current = ready.front();
        ready.pop();
        order.push_back(current);

        for (const auto& dependent : dependents[current]) {
            inDegree[dependent]--;
            if (inDegree[dependent] == 0) {
                ready.push(dependent);
            }
        }
    }

    // Check for cycle
    if (order.size() != modules_.size()) {
        std::cerr << "Circular dependency detected in modules\n";
        return {};
    }

    return order;
}

bool ModuleLoader::initializeAll(BlockRegistry& blocks, EntityRegistry& entities, ItemRegistry& items) {
    // Resolve dependencies and get initialization order
    initOrder_ = resolveDependencies();
    if (initOrder_.empty() && !modules_.empty()) {
        return false;  // Dependency resolution failed
    }

    // Initialize in order
    for (const auto& name : initOrder_) {
        auto& loaded = modules_[name];
        if (loaded.initialized) {
            continue;  // Already initialized
        }

        // Create registry context for this module
        ModuleRegistry registry(name, blocks, entities, items);

        // Call lifecycle methods
        try {
            loaded.module->onLoad(registry);
            loaded.module->onRegister(registry);
            loaded.initialized = true;
            std::cout << "Initialized module: " << name << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Module " << name << " initialization failed: " << e.what() << "\n";
            return false;
        }
    }

    return true;
}

void ModuleLoader::shutdownAll() {
    // Shutdown in reverse initialization order
    for (auto it = initOrder_.rbegin(); it != initOrder_.rend(); ++it) {
        auto moduleIt = modules_.find(*it);
        if (moduleIt != modules_.end() && moduleIt->second.initialized) {
            try {
                moduleIt->second.module->onUnload();
                moduleIt->second.initialized = false;
                std::cout << "Unloaded module: " << *it << "\n";
            } catch (const std::exception& e) {
                std::cerr << "Module " << *it << " unload failed: " << e.what() << "\n";
            }
        }
    }

    initOrder_.clear();
}

GameModule* ModuleLoader::getModule(std::string_view name) {
    auto it = modules_.find(std::string(name));
    if (it != modules_.end()) {
        return it->second.module.get();
    }
    return nullptr;
}

std::vector<std::string_view> ModuleLoader::loadedModules() const {
    std::vector<std::string_view> names;
    names.reserve(initOrder_.size());
    for (const auto& name : initOrder_) {
        names.push_back(name);
    }
    return names;
}

bool ModuleLoader::hasModule(std::string_view name) const {
    return modules_.find(std::string(name)) != modules_.end();
}

size_t ModuleLoader::moduleCount() const {
    return modules_.size();
}

}  // namespace finevox
