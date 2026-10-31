#pragma once

/**
 * @file content_pack_loader.hpp
 * @brief Unified loader for finevox content packs (blocks, geometries, fluids,
 *        recipes, loot tables, particles, emitters, entities, tags, sounds,
 *        localization, and sky configuration).
 */

#include "finevox/core/block_model.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/position.hpp"
#include "finevox/core/sky.hpp"
#include "finevox/core/string_interner.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace finevox {

class World;

/// Texture names for a block's faces, relative to <root>/textures, no extension.
struct BlockTextureSet {
    std::string top;
    std::string bottom;
    std::string side;
};

/// Summary statistics from loading a content pack directory
struct ContentPackLoadStats {
    size_t blocksLoaded = 0;
    size_t customGeometriesLoaded = 0;
    size_t scriptedBlocksDiscovered = 0;
    size_t fluidsLoaded = 0;
    size_t recipesLoaded = 0;
    size_t lootTablesLoaded = 0;
    size_t particleTypesLoaded = 0;
    size_t emitterPresetsLoaded = 0;
    size_t entityTypesLoaded = 0;
    size_t tagFilesLoaded = 0;
    size_t soundSetsLoaded = 0;
    size_t langFilesLoaded = 0;
    bool skyConfigLoaded = false;

    ContentPackLoadStats& operator+=(const ContentPackLoadStats& other) {
        blocksLoaded += other.blocksLoaded;
        customGeometriesLoaded += other.customGeometriesLoaded;
        scriptedBlocksDiscovered += other.scriptedBlocksDiscovered;
        fluidsLoaded += other.fluidsLoaded;
        recipesLoaded += other.recipesLoaded;
        lootTablesLoaded += other.lootTablesLoaded;
        particleTypesLoaded += other.particleTypesLoaded;
        emitterPresetsLoaded += other.emitterPresetsLoaded;
        entityTypesLoaded += other.entityTypesLoaded;
        tagFilesLoaded += other.tagFilesLoaded;
        soundSetsLoaded += other.soundSetsLoaded;
        langFilesLoaded += other.langFilesLoaded;
        skyConfigLoaded = skyConfigLoaded || other.skyConfigLoaded;
        return *this;
    }
};

/// Record of a block definition that specifies a script handler
struct ScriptedBlockEntry {
    std::string blockName;      ///< Bare/local name (e.g. "workbench")
    std::string qualifiedName;  ///< Namespaced name (e.g. "finevox:workbench")
    std::string scriptPath;     ///< Resolved filesystem path to the script file
};

/**
 * @brief Encapsulates loading a content directory into finevox's global registries
 *        and retains custom block geometries and SkyConfig for the renderer.
 */
class ContentPackLoader {
public:
    explicit ContentPackLoader(std::string defaultNamespace = "finevox");

    /// Load all content subdirectories and sky.conf from rootDir
    ContentPackLoadStats loadAll(const std::filesystem::path& rootDir);

    /// Load all content from rootDir and bind discovered scripts onto a GameScriptEngine
    template <typename ScriptEngineT>
    ContentPackLoadStats loadAll(const std::filesystem::path& rootDir,
                                 ScriptEngineT* scriptEngine) {
        auto stats = loadAll(rootDir);
        if (scriptEngine) {
            bindScripts(*scriptEngine);
        }
        return stats;
    }

    /// Load content from ResourceLocator's configured "engine" and "game" roots
    ContentPackLoadStats loadFromLocator();

    /// Load content from ResourceLocator's roots and bind scripts onto a GameScriptEngine
    template <typename ScriptEngineT>
    ContentPackLoadStats loadFromLocator(ScriptEngineT* scriptEngine) {
        auto stats = loadFromLocator();
        if (scriptEngine) {
            bindScripts(*scriptEngine);
        }
        return stats;
    }

    // ========================================================================
    // Individual Content Category Loaders
    // ========================================================================

    /// Load blocks/*.model (or specific blockNames if non-empty), resolving
    /// includes and shapes/*.geom + shapes/*.collision, and registering both
    /// bare ("stone") and namespaced ("finevox:stone") BlockTypeIds.
    size_t loadBlocks(const std::filesystem::path& rootDir,
                      const std::vector<std::string>& blockNames = {});

    /// Load fluids/*.fluid into FluidRegistry and FluidInteractionRegistry
    size_t loadFluids(const std::filesystem::path& rootDir);

    /// Load recipes/*.recipe into RecipeRegistry
    size_t loadRecipes(const std::filesystem::path& rootDir);

    /// Load loot/*.loot into LootRegistry
    size_t loadLootTables(const std::filesystem::path& rootDir);

    /// Load particles/*.particle into ParticleTypeRegistry
    size_t loadParticles(const std::filesystem::path& rootDir);

    /// Load emitters/*.emitter into EmitterPresetRegistry
    size_t loadEmitters(const std::filesystem::path& rootDir);

    /// Load entities/*.entity into EntityTypeRegistry
    size_t loadEntities(const std::filesystem::path& rootDir);

    /// Load tags/*.tag into TagRegistry + UnificationRegistry and rebuild
    size_t loadTags(const std::filesystem::path& rootDir);

    /// Load sounds/*.sound into SoundRegistry
    size_t loadSounds(const std::filesystem::path& rootDir);

    /// Load lang/*.lang into LabelRegistry (loads defaultLocale last so it takes precedence)
    size_t loadLocalization(const std::filesystem::path& rootDir,
                            std::string_view defaultLocale = "en");

    /// Load sky.conf into skyConfig() (falls back to SkyConfig::defaults() if missing)
    bool loadSkyConfig(const std::filesystem::path& rootDir);

    // ========================================================================
    // Renderer Callbacks & Loaded Data Accessors
    // ========================================================================

    /// Map of BlockTypeId::id -> BlockGeometry for blocks with custom meshes
    [[nodiscard]] const std::unordered_map<uint32_t, BlockGeometry>& blockGeometries() const {
        return blockGeometries_;
    }

    /// Map of BlockTypeId::id -> face textures, as declared by .model files
    [[nodiscard]] const std::unordered_map<uint32_t, BlockTextureSet>& blockTextures() const {
        return blockTextures_;
    }

    /// Look up custom geometry for a block type (returns nullptr for standard cubes)
    [[nodiscard]] const BlockGeometry* getGeometry(BlockTypeId type) const;

    /// Callback suitable for WorldRenderer::setGeometryProvider(...)
    [[nodiscard]] std::function<const BlockGeometry*(BlockTypeId)> geometryProvider() const;

    /// Callback suitable for WorldRenderer::setConnectsProvider(...). Holds `world` by reference.
    [[nodiscard]] std::function<bool(const BlockCoord&, BlockTypeId)> connectsProvider(
        const World& world) const;

    /// Callback suitable for WorldRenderer::setFaceOccludesProvider(...)
    [[nodiscard]] std::function<bool(const BlockCoord&, Face)> faceOccludesProvider(
        const World& world) const;

    /// Loaded SkyConfig (from sky.conf, or SkyConfig::defaults() if not present)
    [[nodiscard]] const SkyConfig& skyConfig() const { return skyConfig_; }

    /// Whether skyConfig() was loaded from a valid sky.conf file
    [[nodiscard]] bool hasCustomSkyConfig() const { return hasCustomSkyConfig_; }

    /// Discovered blocks that declared a `script:` field in their .model file
    [[nodiscard]] const std::vector<ScriptedBlockEntry>& scriptedBlocks() const {
        return scriptedBlocks_;
    }

    /// Statistics from the most recent loadAll / loadFromLocator call
    [[nodiscard]] const ContentPackLoadStats& lastStats() const { return lastStats_; }

    // ========================================================================
    // Script Binding
    // ========================================================================

    /// Bind discovered block scripts using a custom callback
    size_t bindScripts(
        const std::function<bool(const std::string& scriptPath,
                                 const std::string& blockName)>& blockScriptLoader) const;

    /// Bind discovered block scripts and entity scripts onto a GameScriptEngine
    template <typename ScriptEngineT>
    size_t bindScripts(ScriptEngineT& scriptEngine) const {
        size_t count = bindScripts(
            [&scriptEngine](const std::string& scriptPath, const std::string& blockName) -> bool {
                return scriptEngine.loadBlockScript(scriptPath, blockName) != nullptr;
            });
        scriptEngine.loadEntityScriptsFromRegistry();
        return count;
    }

    template <typename ScriptEngineT>
    size_t bindScripts(ScriptEngineT* scriptEngine) const {
        return scriptEngine ? bindScripts(*scriptEngine) : 0;
    }

private:
    [[nodiscard]] std::string resolveScriptPath(const std::filesystem::path& rootDir,
                                                const std::string& rawScript) const;

    std::string defaultNamespace_;
    std::unordered_map<uint32_t, BlockGeometry> blockGeometries_;
    std::unordered_map<uint32_t, uint32_t> blockConnects_;  ///< Block id to its group's interned name
    std::unordered_map<uint32_t, BlockTextureSet> blockTextures_;
    size_t customGeometryModelCount_ = 0;
    std::vector<ScriptedBlockEntry> scriptedBlocks_;
    SkyConfig skyConfig_;
    bool hasCustomSkyConfig_ = false;
    ContentPackLoadStats lastStats_;
};

}  // namespace finevox
