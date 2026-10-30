#include "finevox/core/content_pack_loader.hpp"

#include "finevox/audio/sound_loader.hpp"
#include "finevox/core/block_model_loader.hpp"
#include "finevox/core/entity_type_loader.hpp"
#include "finevox/core/fluid_loader.hpp"
#include "finevox/core/label_registry.hpp"
#include "finevox/core/loot_table_loader.hpp"
#include "finevox/core/particle_registry.hpp"
#include "finevox/core/recipe_loader.hpp"
#include "finevox/core/resource_locator.hpp"
#include "finevox/core/tag_registry.hpp"
#include "finevox/core/unification.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <fstream>

namespace finevox {

namespace fs = std::filesystem;

ContentPackLoader::ContentPackLoader(std::string defaultNamespace)
    : defaultNamespace_(std::move(defaultNamespace))
    , skyConfig_(SkyConfig::defaults())
{}

ContentPackLoadStats ContentPackLoader::loadAll(const fs::path& rootDir) {
    ContentPackLoadStats stats;
    if (rootDir.empty() || !fs::exists(rootDir) || !fs::is_directory(rootDir)) {
        lastStats_ = stats;
        return stats;
    }

    // Load tags and unification first so recipes and loot tables can reference tags
    stats.tagFilesLoaded = loadTags(rootDir);

    // Load data registries referenced by blocks/entities/UI
    stats.soundSetsLoaded = loadSounds(rootDir);
    stats.particleTypesLoaded = loadParticles(rootDir);
    stats.emitterPresetsLoaded = loadEmitters(rootDir);
    stats.lootTablesLoaded = loadLootTables(rootDir);
    stats.fluidsLoaded = loadFluids(rootDir);

    // Load block definitions and custom geometries
    size_t prevGeomCount = customGeometryModelCount_;
    size_t prevScriptCount = scriptedBlocks_.size();
    stats.blocksLoaded = loadBlocks(rootDir);
    stats.customGeometriesLoaded =
        (customGeometryModelCount_ >= prevGeomCount)
            ? (customGeometryModelCount_ - prevGeomCount)
            : customGeometryModelCount_;
    stats.scriptedBlocksDiscovered =
        (scriptedBlocks_.size() >= prevScriptCount)
            ? (scriptedBlocks_.size() - prevScriptCount)
            : scriptedBlocks_.size();

    // Load entities, recipes, localization, and sky config
    stats.entityTypesLoaded = loadEntities(rootDir);
    stats.recipesLoaded = loadRecipes(rootDir);
    stats.langFilesLoaded = loadLocalization(rootDir);
    stats.skyConfigLoaded = loadSkyConfig(rootDir);

    lastStats_ = stats;
    return stats;
}

ContentPackLoadStats ContentPackLoader::loadFromLocator() {
    ContentPackLoadStats total;
    auto& locator = ResourceLocator::instance();

    auto engineRoot = locator.engineRoot();
    if (!engineRoot.empty() && fs::exists(engineRoot) && fs::is_directory(engineRoot)) {
        total += loadAll(engineRoot);
    }

    auto gameRoot = locator.gameRoot();
    if (!gameRoot.empty() && gameRoot != engineRoot &&
        fs::exists(gameRoot) && fs::is_directory(gameRoot)) {
        total += loadAll(gameRoot);
    }

    lastStats_ = total;
    return total;
}

size_t ContentPackLoader::loadBlocks(const fs::path& rootDir,
                                     const std::vector<std::string>& blockNames) {
    fs::path blocksDir = rootDir / "blocks";

    BlockModelLoader modelLoader;
    modelLoader.setFileResolver([rootDir, blocksDir](const std::string& relPath) -> std::string {
        // 1. Direct under <rootDir>/blocks/ (for includes like "base/solid_cube.model")
        fs::path candidate = blocksDir / relPath;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        // 2. Direct under <rootDir>/ (for "shapes/slab_faces.geom", "shapes/slab_collision.collision")
        candidate = rootDir / relPath;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        // 3. ResourceLocator game/ scope
        auto& locator = ResourceLocator::instance();
        candidate = locator.resolve("game/blocks/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        candidate = locator.resolve("game/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        // 4. ResourceLocator engine/ scope
        candidate = locator.resolve("engine/blocks/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        candidate = locator.resolve("engine/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return candidate.string();
        }
        return "";
    });

    struct BlockFileEntry {
        std::string name;
        fs::path path;
    };
    std::vector<BlockFileEntry> entries;

    if (!blockNames.empty()) {
        for (const auto& name : blockNames) {
            std::string localName(BlockRegistry::getLocalName(name));
            fs::path modelPath = blocksDir / (localName + ".model");
            if (!fs::exists(modelPath)) {
                auto resolved = ResourceLocator::instance().resolve(
                    "game/blocks/" + localName + ".model");
                if (!resolved.empty() && fs::exists(resolved)) {
                    modelPath = resolved;
                }
            }
            if (fs::exists(modelPath) && fs::is_regular_file(modelPath)) {
                entries.push_back({name, modelPath});
            }
        }
    } else if (fs::exists(blocksDir) && fs::is_directory(blocksDir)) {
        for (const auto& dirEntry : fs::directory_iterator(blocksDir)) {
            if (!dirEntry.is_regular_file()) continue;
            if (dirEntry.path().extension() != ".model") continue;
            entries.push_back({dirEntry.path().stem().string(), dirEntry.path()});
        }
        std::sort(entries.begin(), entries.end(),
                  [](const BlockFileEntry& a, const BlockFileEntry& b) {
                      return a.name < b.name;
                  });
    }

    auto& registry = BlockRegistry::global();
    size_t loadedCount = 0;

    for (const auto& item : entries) {
        auto model = modelLoader.loadModel(item.path.string());
        if (!model) {
            continue;
        }

        BlockType blockType;
        blockType.setHardness(model->hardness());

        if (model->hasExplicitCollision() || model->hasCustomGeometry()) {
            const auto& coll = model->resolvedCollision();
            if (coll.isEmpty()) {
                blockType.setNoCollision();
            } else {
                blockType.setCollisionShape(coll);
            }
        }

        if (model->hasExplicitHit()) {
            const auto& hit = model->resolvedHit();
            if (hit.isEmpty()) {
                blockType.setNoHit();
            } else {
                blockType.setHitShape(hit);
            }
        }

        if (model->lightEmission() > 0) {
            blockType.setLightEmission(model->lightEmission());
        }
        blockType.setLightAttenuation(model->lightAttenuation());

        blockType.setWalkSpeed(model->walkSpeed());
        if (model->hasOpaqueOverride()) {
            blockType.setOpaque(model->opaque());
            if (!model->opaque()) {
                blockType.setTransparent(true);
                blockType.setBlocksSkyLight(false);
            }
        }

        if (model->hasCustomGeometry()) {
            blockType.setHasCustomMesh(true);
        }

        if (!model->sounds().empty()) {
            blockType.setSoundSet(SoundSetId::fromName(model->sounds()));
        }

        if (model->hasLoot()) {
            blockType.setLootTable(LootTableId::fromName(model->loot()));
        }

        if (model->hasParticle()) {
            blockType.setParticleEmitter(EmitterPresetId::fromName(model->particle()));
        }

        // Determine both bare ("stone") and qualified ("finevox:stone") names
        std::string bareName = item.name;
        std::string qualifiedName = item.name;
        if (item.name.find(':') == std::string::npos) {
            if (!defaultNamespace_.empty()) {
                qualifiedName = defaultNamespace_ + ":" + item.name;
            }
        } else if (!defaultNamespace_.empty() &&
                   item.name.rfind(defaultNamespace_ + ":", 0) == 0 &&
                   item.name.size() > defaultNamespace_.size() + 1) {
            bareName = item.name.substr(defaultNamespace_.size() + 1);
        }

        BlockTypeId bareId = BlockTypeId::fromName(bareName);
        BlockTypeId qualifiedId = BlockTypeId::fromName(qualifiedName);

        registry.registerOrUpdateType(bareId, blockType);
        if (qualifiedId != bareId) {
            registry.registerOrUpdateType(qualifiedId, blockType);
        }

        if (!model->texture().empty() || !model->textureTop().empty()) {
            BlockTextureSet textures;
            textures.top = model->textureTop().empty() ? model->texture() : model->textureTop();
            textures.bottom =
                model->textureBottom().empty() ? model->texture() : model->textureBottom();
            textures.side = model->textureSide().empty() ? model->texture() : model->textureSide();
            blockTextures_[bareId.id] = textures;
            if (qualifiedId != bareId) {
                blockTextures_[qualifiedId.id] = std::move(textures);
            }
        }

        if (model->hasCustomGeometry()) {
            blockGeometries_[bareId.id] = model->geometry();
            if (qualifiedId != bareId) {
                blockGeometries_[qualifiedId.id] = model->geometry();
            }
            ++customGeometryModelCount_;
        }

        if (model->hasScript()) {
            std::string resolvedScript = resolveScriptPath(rootDir, model->script());
            auto existingIt = std::find_if(
                scriptedBlocks_.begin(), scriptedBlocks_.end(),
                [&bareName](const ScriptedBlockEntry& e) { return e.blockName == bareName; });
            if (existingIt != scriptedBlocks_.end()) {
                existingIt->qualifiedName = qualifiedName;
                existingIt->scriptPath = std::move(resolvedScript);
            } else {
                scriptedBlocks_.push_back(
                    ScriptedBlockEntry{bareName, qualifiedName, std::move(resolvedScript)});
            }
        }

        ++loadedCount;
    }

    return loadedCount;
}

size_t ContentPackLoader::loadFluids(const fs::path& rootDir) {
    fs::path dir = rootDir / "fluids";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return FluidLoader::loadDirectory(dir.string());
}

size_t ContentPackLoader::loadRecipes(const fs::path& rootDir) {
    fs::path dir = rootDir / "recipes";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return RecipeLoader::loadDirectory(dir.string());
}

size_t ContentPackLoader::loadLootTables(const fs::path& rootDir) {
    fs::path dir = rootDir / "loot";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return LootTableLoader::loadDirectory(dir.string());
}

size_t ContentPackLoader::loadParticles(const fs::path& rootDir) {
    fs::path dir = rootDir / "particles";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return ParticleLoader::loadTypesFromDirectory(dir.string());
}

size_t ContentPackLoader::loadEmitters(const fs::path& rootDir) {
    fs::path dir = rootDir / "emitters";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return ParticleLoader::loadPresetsFromDirectory(dir.string());
}

size_t ContentPackLoader::loadEntities(const fs::path& rootDir) {
    fs::path dir = rootDir / "entities";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return EntityTypeLoader::loadDirectory(dir.string());
}

size_t ContentPackLoader::loadTags(const fs::path& rootDir) {
    fs::path dir = rootDir / "tags";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }

    std::vector<fs::path> tagFiles;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".tag") continue;
        tagFiles.push_back(entry.path());
    }
    std::sort(tagFiles.begin(), tagFiles.end());

    size_t count = 0;
    for (const auto& path : tagFiles) {
        int n = loadTagFile(path.string(), TagRegistry::global(), UnificationRegistry::global());
        if (n > 0) {
            ++count;
        }
    }

    TagRegistry::global().rebuild();
    UnificationRegistry::global().autoResolve(TagRegistry::global());
    UnificationRegistry::global().propagateTags(TagRegistry::global());
    TagRegistry::global().rebuild();
    return count;
}

size_t ContentPackLoader::loadSounds(const fs::path& rootDir) {
    fs::path dir = rootDir / "sounds";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }
    return finevox::audio::SoundLoader::loadDirectory(dir.string());
}

size_t ContentPackLoader::loadLocalization(const fs::path& rootDir,
                                           std::string_view defaultLocale) {
    fs::path dir = rootDir / "lang";
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return 0;
    }

    std::vector<fs::path> langFiles;
    fs::path preferredFile;
    std::string preferredName = std::string(defaultLocale) + ".lang";

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".lang") continue;
        if (entry.path().filename() == preferredName) {
            preferredFile = entry.path();
        } else {
            langFiles.push_back(entry.path());
        }
    }
    std::sort(langFiles.begin(), langFiles.end());
    if (!preferredFile.empty()) {
        langFiles.push_back(preferredFile);
    }

    size_t count = 0;
    for (const auto& path : langFiles) {
        LabelRegistry::global().loadFile(path.string());
        ++count;
    }
    return count;
}

bool ContentPackLoader::loadSkyConfig(const fs::path& rootDir) {
    fs::path skyPath = rootDir / "sky.conf";
    if (!fs::exists(skyPath) || !fs::is_regular_file(skyPath)) {
        return false;
    }

    std::ifstream check(skyPath);
    if (!check.is_open()) {
        return false;
    }

    skyConfig_ = SkyConfig::fromFile(skyPath.string());
    hasCustomSkyConfig_ = !skyConfig_.colorKeyframes.empty() &&
                          !skyConfig_.brightnessKeyframes.empty();
    return hasCustomSkyConfig_;
}

const BlockGeometry* ContentPackLoader::getGeometry(BlockTypeId type) const {
    auto it = blockGeometries_.find(type.id);
    if (it != blockGeometries_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::function<const BlockGeometry*(BlockTypeId)> ContentPackLoader::geometryProvider() const {
    return [this](BlockTypeId type) -> const BlockGeometry* {
        return getGeometry(type);
    };
}

std::function<bool(const BlockCoord&, Face)> ContentPackLoader::faceOccludesProvider(
    const World& world) const {
    return [this, &world](const BlockCoord& pos, Face face) -> bool {
        BlockTypeId blockType = world.getBlock(pos);
        if (blockType.isAir()) {
            return false;
        }
        if (const BlockGeometry* geom = getGeometry(blockType)) {
            uint8_t solidMask = geom->solidFacesMask();
            return (solidMask & (1 << static_cast<int>(face))) != 0;
        }
        return BlockRegistry::global().getType(blockType).isOpaque();
    };
}

size_t ContentPackLoader::bindScripts(
    const std::function<bool(const std::string& scriptPath,
                             const std::string& blockName)>& blockScriptLoader) const {
    if (!blockScriptLoader) {
        return 0;
    }

    size_t bound = 0;
    for (const auto& entry : scriptedBlocks_) {
        bool ok = blockScriptLoader(entry.scriptPath, entry.blockName);
        if (!entry.qualifiedName.empty() && entry.qualifiedName != entry.blockName) {
            ok = blockScriptLoader(entry.scriptPath, entry.qualifiedName) || ok;
        }
        if (ok) {
            ++bound;
        }
    }
    return bound;
}

std::string ContentPackLoader::resolveScriptPath(const fs::path& rootDir,
                                                 const std::string& rawScript) const {
    auto tryWithExtensions = [](const fs::path& base) -> std::string {
        if (fs::exists(base) && fs::is_regular_file(base)) {
            return base.string();
        }
        fs::path withFsc = base;
        withFsc += ".fsc";
        if (fs::exists(withFsc) && fs::is_regular_file(withFsc)) {
            return withFsc.string();
        }
        fs::path withFs = base;
        withFs += ".fs";
        if (fs::exists(withFs) && fs::is_regular_file(withFs)) {
            return withFs.string();
        }
        return "";
    };

    // 1. Direct path
    if (std::string found = tryWithExtensions(fs::path(rawScript)); !found.empty()) {
        return found;
    }
    // 2. Relative to rootDir
    if (std::string found = tryWithExtensions(rootDir / rawScript); !found.empty()) {
        return found;
    }
    // 3. Relative to rootDir/scripts
    if (std::string found = tryWithExtensions(rootDir / "scripts" / rawScript); !found.empty()) {
        return found;
    }
    // 4. Relative to rootDir/blocks
    if (std::string found = tryWithExtensions(rootDir / "blocks" / rawScript); !found.empty()) {
        return found;
    }
    // 5. ResourceLocator game/ and engine/
    auto& locator = ResourceLocator::instance();
    for (const char* prefix : {"game/", "game/scripts/", "engine/", "engine/scripts/"}) {
        auto resolved = locator.resolve(std::string(prefix) + rawScript);
        if (!resolved.empty()) {
            if (std::string found = tryWithExtensions(resolved); !found.empty()) {
                return found;
            }
        }
    }

    return (rootDir / rawScript).string();
}

}  // namespace finevox
