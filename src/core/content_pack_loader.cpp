#include "finevox/core/content_pack_loader.hpp"

#include "finevox/audio/sound_loader.hpp"
#include "finevox/core/block_model_loader.hpp"
#include "finevox/core/config_parser.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/block_model_transform.hpp"
#include "finevox/core/block_properties.hpp"
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
#include <iostream>
#include <set>
#include <sstream>
#include <optional>

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
    loadBiomeTints(rootDir);
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
    std::unordered_map<std::string, std::string> resolvedCache;
    const std::function<std::string(const std::string&)> resolveBlockFile =
        [rootDir, blocksDir, &resolvedCache](const std::string& relPath) -> std::string {
        auto found = resolvedCache.find(relPath);
        if (found != resolvedCache.end()) {
            return found->second;
        }
        auto remember = [&](std::string result) -> std::string {
            resolvedCache.emplace(relPath, result);
            return result;
        };
        // 1. Direct under <rootDir>/blocks/ (for includes like "base/solid_cube.model")
        fs::path candidate = blocksDir / relPath;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        // 2. Direct under <rootDir>/ (for "shapes/slab_faces.geom", "shapes/slab_collision.collision")
        candidate = rootDir / relPath;
        if (fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        // 3. ResourceLocator game/ scope
        auto& locator = ResourceLocator::instance();
        candidate = locator.resolve("game/blocks/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        candidate = locator.resolve("game/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        // 4. ResourceLocator engine/ scope
        candidate = locator.resolve("engine/blocks/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        candidate = locator.resolve("engine/" + relPath);
        if (!candidate.empty() && fs::exists(candidate) && fs::is_regular_file(candidate)) {
            return remember(candidate.string());
        }
        return remember("");
    };
    modelLoader.setFileResolver(resolveBlockFile);

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

    // One block type, with this name, behaving and drawn as `m`
    auto registerBlock = [&](const std::string& name, const BlockModel& m) {
            BlockType blockType;
            blockType.setHardness(m.hardness());

            if (m.hasExplicitCollision() || m.hasCustomGeometry()) {
                const auto& coll = m.resolvedCollision();
                if (coll.isEmpty()) {
                    blockType.setNoCollision();
                } else {
                    blockType.setCollisionShape(coll);
                }
            }

            if (m.hasExplicitHit()) {
                const auto& hit = m.resolvedHit();
                if (hit.isEmpty()) {
                    blockType.setNoHit();
                } else {
                    blockType.setHitShape(hit);
                }
            }

            if (m.lightEmission() > 0) {
                blockType.setLightEmission(m.lightEmission());
            }
            blockType.setLightAttenuation(m.lightAttenuation());

            blockType.setWalkSpeed(m.walkSpeed());
            if (m.hasOpaqueOverride()) {
                blockType.setOpaque(m.opaque());
                if (!m.opaque()) {
                    blockType.setTransparent(true);
                    blockType.setBlocksSkyLight(false);
                    // BlockModel defaults to blocking all light, which would turn the
                    // ground under every see-through block black. An explicit
                    // light-attenuation below 15 still applies.
                    if (m.lightAttenuation() >= 15) blockType.setLightAttenuation(1);
                }
            }
            if (m.hasBlocksSkyLightOverride()) blockType.setBlocksSkyLight(m.blocksSkyLight());

            if (m.hasCustomGeometry()) {
                blockType.setHasCustomMesh(true);
            }

            if (!m.sounds().empty()) {
                blockType.setSoundSet(SoundSetId::fromName(m.sounds()));
            }

            if (m.hasLoot()) {
                blockType.setLootTable(LootTableId::fromName(m.loot()));
            }

            if (m.hasParticle()) {
                blockType.setParticleEmitter(EmitterPresetId::fromName(m.particle()));
            }

            // Determine both bare ("stone") and qualified ("finevox:stone") names
            std::string bareName = name;
            std::string qualifiedName = name;
            if (name.find(':') == std::string::npos) {
                if (!defaultNamespace_.empty()) {
                    qualifiedName = defaultNamespace_ + ":" + name;
                }
            } else if (!defaultNamespace_.empty() &&
                       name.rfind(defaultNamespace_ + ":", 0) == 0 &&
                       name.size() > defaultNamespace_.size() + 1) {
                bareName = name.substr(defaultNamespace_.size() + 1);
            }

            BlockTypeId bareId = BlockTypeId::fromName(bareName);
            BlockTypeId qualifiedId = BlockTypeId::fromName(qualifiedName);

            registry.registerOrUpdateType(bareId, blockType);
            if (qualifiedId != bareId) {
                registry.registerOrUpdateType(qualifiedId, blockType);
            }

            BlockTextureSet textures;
            bool anyTexture = false;
            for (int f = 0; f < 6; ++f) {
                textures.faces[f] = m.cubeFaceTexture(static_cast<Face>(f));
                anyTexture = anyTexture || !textures.faces[f].empty();
            }
            if (anyTexture) {
                textures.top = textures.faces[static_cast<int>(Face::PosY)];
                textures.bottom = textures.faces[static_cast<int>(Face::NegY)];
                textures.side = textures.faces[static_cast<int>(Face::NegZ)];
                blockTextures_[bareId.id] = textures;
                if (qualifiedId != bareId) {
                    blockTextures_[qualifiedId.id] = std::move(textures);
                }
            }

            {
            uint8_t cubeMask = 0;
            for (int f = 0; f < 6; ++f) {
                if (m.faceTint(static_cast<Face>(f)) >= 0) cubeMask |= static_cast<uint8_t>(1 << f);
            }
            bool geometryTinted = false;
            for (const auto& face : m.geometry().faces()) geometryTinted = geometryTinted || face.tintSlot >= 0;
            if (cubeMask || geometryTinted || !m.tintSource().empty() || m.tintColor()) {
                BlockTint tint;
                tint.source = m.tintSource().empty() ? 0 : StringInterner::global().intern(m.tintSource());
                glm::vec3 c = glm::clamp(m.tintColor().value_or(glm::vec3(1.0f)), 0.0f, 1.0f);
                tint.color = static_cast<uint32_t>(c.r * 255.0f + 0.5f) |
                             static_cast<uint32_t>(c.g * 255.0f + 0.5f) << 8 |
                             static_cast<uint32_t>(c.b * 255.0f + 0.5f) << 16 | 0xFF000000u;
                tint.cubeMask = cubeMask;
                blockTints_[bareId.id] = tint;
                blockTints_[qualifiedId.id] = tint;
            }
        }

        if (!m.connects().empty()) {
                const uint32_t group = StringInterner::global().intern(m.connects());
                blockConnects_[bareId.id] = group;
                blockConnects_[qualifiedId.id] = group;
            }

            if (m.hasCustomGeometry()) {
                blockGeometries_[bareId.id] = m.geometry();
                if (qualifiedId != bareId) {
                    blockGeometries_[qualifiedId.id] = m.geometry();
                }
                ++customGeometryModelCount_;
            }
            if (m.hasFarGeometry()) {
                blockFarGeometries_[bareId.id] = m.farGeometry();
                blockFarGeometries_[qualifiedId.id] = m.farGeometry();
            }

            if (m.hasScript()) {
                std::string resolvedScript = resolveScriptPath(rootDir, m.script());
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

    };

    // A state's look, turned as its rule says, loaded once however many states share it
    std::unordered_map<std::string, std::optional<BlockModel>> looks;
    auto lookFor = [&](const PropertyLook& rule) -> const std::optional<BlockModel>& {
        std::string key = rule.model + " " + std::to_string(rule.x) + " " +
                          std::to_string(rule.y) + (rule.lockUV ? " u" : "");
        auto it = looks.find(key);
        if (it != looks.end()) return it->second;
        std::optional<BlockModel> look;
        std::string path = resolveBlockFile(rule.model + ".model");
        if (path.empty()) path = resolveBlockFile(rule.model);
        if (!path.empty()) {
            if (auto loaded = modelLoader.loadModel(path)) {
                look = turnedModel(*loaded, rule.x, rule.y, rule.lockUV);
            }
        }
        if (!look) std::cerr << "[ContentPackLoader] no look " << rule.model << "\n";
        return looks.emplace(key, std::move(look)).first->second;
    };

    size_t entryIndex = 0;
    if (progressCallback_) progressCallback_(0, entries.size());
    for (const auto& item : entries) {
        ++entryIndex;
        if (progressCallback_) progressCallback_(entryIndex, entries.size());
        auto model = modelLoader.loadModel(item.path.string());
        if (!model) {
            continue;
        }
        if (!model->hasProperties()) {
            BlockModel look = *model;
            drawAsCubeIfPlain(look);
            registerBlock(item.name, look);
            ++loadedCount;
            continue;
        }

        const auto& props = model->properties();
        std::vector<std::string> propNames;
        for (const auto& p : props) propNames.push_back(p.name);
        std::string family = item.name.substr(item.name.find(':') == std::string::npos ? 0 : item.name.find(':') + 1);
        BlockPropertyRegistry::global().addFamily(family, props);

        // Every combination of values, the last property changing fastest
        std::vector<size_t> at(props.size(), 0);
        while (true) {
            std::vector<std::string> values;
            for (size_t i = 0; i < props.size(); ++i) values.push_back(props[i].values[at[i]]);

            BlockModel state = *model;
            if (!model->variants().empty()) {
                for (const auto& rule : model->variants()) {
                    if (!rule.when.matches(propNames, values)) continue;
                    if (const auto& look = lookFor(rule)) state.takeAppearance(*look);
                    break;
                }
            } else if (!model->parts().empty()) {
                std::vector<BlockModel> drawn;
                for (const auto& rule : model->parts()) {
                    if (!rule.when.matches(propNames, values)) continue;
                    if (const auto& look = lookFor(rule)) drawn.push_back(*look);
                }
                if (!drawn.empty()) state.takeAppearance(combinedLook(drawn));
            }
            for (const auto& setting : model->stateSettings()) {
                if (setting.when.matches(propNames, values)) state.applySetting(setting);
            }
            state.resolveFaceTextures();
            drawAsCubeIfPlain(state);
            registerBlock(BlockPropertyRegistry::global().stateName(item.name, values), state);
            ++loadedCount;

            size_t i = props.size();
            while (i > 0 && ++at[i - 1] == props[i - 1].values.size()) at[--i] = 0;
            if (i == 0) break;
        }
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

    auto& labels = LabelRegistry::global();
    size_t count = 0;
    for (const auto& path : langFiles) {
        labels.loadLocaleFile(path.stem().string(), path.string());
        ++count;
    }
    labels.setFallbackLocale(std::string(defaultLocale));
    labels.setLocale(std::string(defaultLocale));
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

size_t ContentPackLoader::loadBiomeTints(const fs::path& rootDir) {
    fs::path dir = rootDir / "biomes";
    if (!fs::exists(dir) || !fs::is_directory(dir)) return 0;
    ConfigParser parser;
    size_t count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".biome") continue;
        auto doc = parser.parseFile(entry.path().string());
        if (!doc) continue;
        std::string stem = entry.path().stem().string();
        uint32_t biome = StringInterner::global().intern(
            defaultNamespace_.empty() ? stem : defaultNamespace_ + ":" + stem);
        for (const ConfigEntry* tint : doc->getAll("tint")) {
            if (!tint->hasSuffix()) continue;
            std::istringstream iss(tint->value.asStringOwned());
            float r = 1, g = 1, b = 1;
            iss >> r >> g >> b;
            auto byte = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
            biomeTints_[biome].emplace_back(StringInterner::global().intern(tint->suffix),
                                            byte(r) | byte(g) << 8 | byte(b) << 16 | 0xFF000000u);
            ++count;
        }
    }
    return count;
}

namespace {

// The biome at a column, from the subchunk at this height or the nearest one that knows: a
// subchunk built into after the world was made may have none
uint32_t biomeAt(const World& world, int32_t x, int32_t y, int32_t z) {
    const int32_t cx = x >> 4, cy = y >> 4, cz = z >> 4;
    for (int32_t step = 0; step < 8; ++step) {
        for (int32_t dy : {-step, step}) {
            const SubChunk* sub = world.getSubChunk(ChunkPos(cx, cy + dy, cz));
            if (sub && sub->hasBiomes()) return sub->biome(x & 15, z & 15);
            if (step == 0) break;
        }
    }
    return 0;
}

}  // namespace

std::function<uint32_t(BlockTypeId, int, int, const BlockCoord&)> ContentPackLoader::tintProvider(
    const World& world) const {
    if (blockTints_.empty()) return nullptr;
    return [this, &world](BlockTypeId type, int face, int, const BlockCoord& pos) -> uint32_t {
        auto it = blockTints_.find(type.id);
        if (it == blockTints_.end()) return 0xFFFFFFFFu;
        const BlockTint& tint = it->second;
        if (face >= 0 && !((tint.cubeMask >> face) & 1)) return 0xFFFFFFFFu;
        if (tint.source == 0) return tint.color;

        // A column's blend is the same for every face in it, and leaves have six
        thread_local std::unordered_map<uint64_t, uint32_t> cache;
        uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(pos.x)) << 40) ^
                       (static_cast<uint64_t>(static_cast<uint32_t>(pos.z)) << 16) ^
                       (static_cast<uint64_t>(static_cast<uint32_t>(pos.y >> 4)) << 8) ^ tint.source * 0x9E3779B1u;
        if (auto hit = cache.find(key); hit != cache.end()) return hit->second;
        if (cache.size() > 65536) cache.clear();

        uint32_t r = 0, g = 0, b = 0, n = 0;
        bool allKnown = true;
        for (int32_t dz = -2; dz <= 2; ++dz) {
            for (int32_t dx = -2; dx <= 2; ++dx) {
                uint32_t biome = biomeAt(world, pos.x + dx, pos.y, pos.z + dz);
                if (biome == 0) allKnown = false;
                uint32_t color = tint.color;
                if (auto b2 = biomeTints_.find(biome); b2 != biomeTints_.end()) {
                    for (const auto& [source, c] : b2->second) {
                        if (source == tint.source) color = c;
                    }
                }
                r += color & 0xFF;
                g += (color >> 8) & 0xFF;
                b += (color >> 16) & 0xFF;
                ++n;
            }
        }
        uint32_t blended = (r / n) | (g / n) << 8 | (b / n) << 16 | 0xFF000000u;
        // Nothing known here yet means the subchunks around haven't arrived: ask again later
        if (allKnown) cache[key] = blended;
        return blended;
    };
}

std::vector<std::string> ContentPackLoader::faceTextureNames() const {
    std::set<std::string> names;
    for (const auto* map : {&blockGeometries_, &blockFarGeometries_}) {
        for (const auto& [id, geometry] : *map) {
            for (const auto& face : geometry.faces()) {
                if (!face.texture.empty() && face.texture[0] != '#') names.insert(face.texture);
            }
        }
    }
    return {names.begin(), names.end()};
}

void ContentPackLoader::placeFaceTextures(
    const std::function<std::optional<glm::vec4>(const std::string&)>& uvOf) {
    for (auto* map : {&blockGeometries_, &blockFarGeometries_}) {
        for (auto& [id, geometry] : *map) {
            for (auto& face : geometry.mutableFaces()) {
                face.hasTextureUV = false;
                if (face.texture.empty()) continue;
                if (auto uv = uvOf(face.texture)) {
                    face.textureUV = *uv;
                    face.hasTextureUV = true;
                }
            }
        }
    }
}

const BlockGeometry* ContentPackLoader::getGeometry(BlockTypeId type) const {
    auto it = blockGeometries_.find(type.id);
    if (it != blockGeometries_.end()) {
        return &it->second;
    }
    return nullptr;
}

const BlockGeometry* ContentPackLoader::getFarGeometry(BlockTypeId type) const {
    auto it = blockFarGeometries_.find(type.id);
    return it != blockFarGeometries_.end() ? &it->second : nullptr;
}

std::function<const BlockGeometry*(BlockTypeId)> ContentPackLoader::farGeometryProvider() const {
    // Nothing has one in most games, and then every block skips the lookup
    if (blockFarGeometries_.empty()) return nullptr;
    return [this](BlockTypeId type) -> const BlockGeometry* { return getFarGeometry(type); };
}

std::function<const BlockGeometry*(BlockTypeId)> ContentPackLoader::geometryProvider() const {
    return [this](BlockTypeId type) -> const BlockGeometry* {
        return getGeometry(type);
    };
}

std::function<bool(const BlockCoord&, BlockTypeId)> ContentPackLoader::connectsProvider(
    const World& world) const {
    return [this, &world](const BlockCoord& pos, BlockTypeId type) -> bool {
        auto self = blockConnects_.find(type.id);
        if (self == blockConnects_.end()) return false;
        auto other = blockConnects_.find(world.getBlock(pos).id);
        return other != blockConnects_.end() && other->second == self->second;
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
