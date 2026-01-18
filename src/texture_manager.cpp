#include "finevox/texture_manager.hpp"
#include "finevox/config_parser.hpp"

#include <finevk/device/logical_device.hpp>

#include <fstream>
#include <sstream>

namespace finevox {

// ============================================================================
// AtlasDefinition
// ============================================================================

std::optional<AtlasDefinition> AtlasDefinition::loadFromFile(const std::string& path) {
    // Parse using ConfigParser
    auto doc = parseConfig(path);
    if (!doc) {
        return std::nullopt;
    }

    AtlasDefinition def;

    // Parse name
    def.name = std::string(doc->getString("name"));

    // Parse image path
    def.imagePath = std::string(doc->getString("image"));

    // Parse grid settings
    // Format: grid: 16 16  (width height)
    if (auto* gridEntry = doc->get("grid")) {
        def.isGrid = true;
        const auto& nums = gridEntry->value.asNumbers();
        if (nums.size() >= 2) {
            def.gridWidth = static_cast<uint32_t>(nums[0]);
            def.gridHeight = static_cast<uint32_t>(nums[1]);
        } else {
            // Try parsing as "16x16" or just "16" (square)
            auto str = gridEntry->value.asString();
            auto xPos = str.find('x');
            if (xPos != std::string_view::npos) {
                def.gridWidth = static_cast<uint32_t>(std::atoi(std::string(str.substr(0, xPos)).c_str()));
                def.gridHeight = static_cast<uint32_t>(std::atoi(std::string(str.substr(xPos + 1)).c_str()));
            } else {
                def.gridWidth = def.gridHeight = static_cast<uint32_t>(gridEntry->value.asInt(16));
            }
        }
    }

    // Parse regions
    // Format:
    //   region:name: x y          (grid coordinates)
    //   region:name: x y w h      (pixel coordinates if 4 values)
    for (const auto* entry : doc->getAll("region")) {
        if (!entry->hasSuffix()) continue;

        RegionDef region;
        region.name = entry->suffix;

        // Check if value has numbers or if data lines have numbers
        std::vector<float> coords;
        if (entry->value.hasNumbers()) {
            coords = entry->value.asNumbers();
        } else if (!entry->dataLines.empty() && !entry->dataLines[0].empty()) {
            coords = entry->dataLines[0];
        }

        if (coords.size() >= 4) {
            // Pixel coordinates: x y w h
            region.usePixels = true;
            region.pixelX = static_cast<uint32_t>(coords[0]);
            region.pixelY = static_cast<uint32_t>(coords[1]);
            region.pixelW = static_cast<uint32_t>(coords[2]);
            region.pixelH = static_cast<uint32_t>(coords[3]);
        } else if (coords.size() >= 2) {
            // Grid coordinates: x y
            region.usePixels = false;
            region.gridX = static_cast<uint32_t>(coords[0]);
            region.gridY = static_cast<uint32_t>(coords[1]);
        }

        def.regions.push_back(std::move(region));
    }

    return def;
}

// ============================================================================
// TextureManager
// ============================================================================

TextureManager::TextureManager(finevk::LogicalDevice* device, finevk::CommandPool* commandPool)
    : device_(device)
    , commandPool_(commandPool)
{
}

finevk::TextureRef TextureManager::loadTexture(const std::string& path) {
    // Resolve path via ResourceLocator
    auto resolvedPath = ResourceLocator::instance().resolve(path);
    if (resolvedPath.empty()) {
        return nullptr;
    }

    // Load texture with mipmaps and sRGB
    return finevk::Texture::load(device_, commandPool_, resolvedPath.string())
        .generateMipmaps()
        .srgb()
        .build();
}

bool TextureManager::loadAtlas(const std::string& definitionPath) {
    auto defOpt = AtlasDefinition::loadFromFile(definitionPath);
    if (!defOpt) {
        return false;
    }

    const auto& def = *defOpt;

    // Load the atlas texture
    auto texture = loadTexture(def.imagePath);
    if (!texture) {
        return false;
    }

    // Register the atlas
    uint32_t atlasIndex = static_cast<uint32_t>(atlases_.size());
    AtlasEntry entry;
    entry.name = def.name;
    entry.texture = std::move(texture);
    entry.gridWidth = def.gridWidth;
    entry.gridHeight = def.gridHeight;
    entry.isGrid = def.isGrid;

    atlases_.push_back(std::move(entry));
    atlasNameToIndex_[def.name] = atlasIndex;

    // Register all named regions
    for (const auto& region : def.regions) {
        TextureEntry texEntry;
        texEntry.atlasIndex = atlasIndex;

        if (region.usePixels) {
            texEntry.region = TextureRegion::fromPixels(
                region.pixelX, region.pixelY, region.pixelW, region.pixelH,
                atlases_[atlasIndex].texture->width(),
                atlases_[atlasIndex].texture->height());
        } else {
            texEntry.region = TextureRegion::fromGrid(
                region.gridX, region.gridY, def.gridWidth, def.gridHeight);
        }

        std::string fullName = def.name + ":" + region.name;
        textureMap_[fullName] = texEntry;
    }

    return true;
}

bool TextureManager::registerGridAtlas(const std::string& name, const std::string& imagePath,
                                        uint32_t gridWidth, uint32_t gridHeight) {
    auto texture = loadTexture(imagePath);
    if (!texture) {
        return false;
    }

    uint32_t atlasIndex = static_cast<uint32_t>(atlases_.size());
    AtlasEntry entry;
    entry.name = name;
    entry.texture = std::move(texture);
    entry.gridWidth = gridWidth;
    entry.gridHeight = gridHeight;
    entry.isGrid = true;

    atlases_.push_back(std::move(entry));
    atlasNameToIndex_[name] = atlasIndex;

    return true;
}

void TextureManager::registerGridRegion(const std::string& atlasName, const std::string& regionName,
                                         uint32_t gridX, uint32_t gridY) {
    auto it = atlasNameToIndex_.find(atlasName);
    if (it == atlasNameToIndex_.end()) return;

    uint32_t atlasIndex = it->second;
    const auto& atlas = atlases_[atlasIndex];

    TextureEntry entry;
    entry.atlasIndex = atlasIndex;
    entry.region = TextureRegion::fromGrid(gridX, gridY, atlas.gridWidth, atlas.gridHeight);

    std::string fullName = atlasName + ":" + regionName;
    textureMap_[fullName] = entry;
}

void TextureManager::registerPixelRegion(const std::string& atlasName, const std::string& regionName,
                                          uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    auto it = atlasNameToIndex_.find(atlasName);
    if (it == atlasNameToIndex_.end()) return;

    uint32_t atlasIndex = it->second;
    const auto& atlas = atlases_[atlasIndex];

    TextureEntry entry;
    entry.atlasIndex = atlasIndex;
    entry.region = TextureRegion::fromPixels(x, y, w, h,
                                              atlas.texture->width(),
                                              atlas.texture->height());

    std::string fullName = atlasName + ":" + regionName;
    textureMap_[fullName] = entry;
}

bool TextureManager::registerTexture(const std::string& name, const std::string& imagePath) {
    auto texture = loadTexture(imagePath);
    if (!texture) {
        return false;
    }

    // Register as a single-region atlas
    uint32_t atlasIndex = static_cast<uint32_t>(atlases_.size());
    AtlasEntry entry;
    entry.name = name;
    entry.texture = std::move(texture);
    entry.gridWidth = 1;
    entry.gridHeight = 1;
    entry.isGrid = false;

    atlases_.push_back(std::move(entry));
    atlasNameToIndex_[name] = atlasIndex;

    // Also register in texture map with full region
    TextureEntry texEntry;
    texEntry.atlasIndex = atlasIndex;
    texEntry.region = TextureRegion::full();
    textureMap_[name] = texEntry;

    return true;
}

std::optional<TextureHandle> TextureManager::getTexture(const std::string& name) const {
    auto it = textureMap_.find(name);
    if (it == textureMap_.end()) {
        return std::nullopt;
    }

    TextureHandle handle;
    handle.atlasIndex = it->second.atlasIndex;
    handle.region = it->second.region;
    return handle;
}

TextureHandle TextureManager::getTextureOr(const std::string& name,
                                            const TextureHandle& fallback) const {
    auto result = getTexture(name);
    return result.value_or(fallback);
}

bool TextureManager::hasTexture(const std::string& name) const {
    return textureMap_.find(name) != textureMap_.end();
}

finevk::Texture* TextureManager::getAtlasTexture(uint32_t atlasIndex) const {
    if (atlasIndex >= atlases_.size()) {
        return nullptr;
    }
    return atlases_[atlasIndex].texture.get();
}

BlockTextureProvider TextureManager::createBlockProvider(
    std::function<std::string(BlockTypeId, Face)> nameProvider) const {

    return [this, nameProvider = std::move(nameProvider)](BlockTypeId id, Face face) -> glm::vec4 {
        std::string texName = nameProvider(id, face);
        auto handle = this->getTexture(texName);
        if (handle) {
            return handle->region.bounds();
        }
        // Fallback: full texture
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };
}

bool TextureManager::loadBlockTextureConfig(const std::string& configPath) {
    auto doc = parseConfig(configPath);
    if (!doc) {
        return false;
    }

    // Parse block texture mappings
    // Each entry is a block name with per-face texture names
    for (const auto& entry : *doc) {
        // Skip non-block entries (like includes or metadata)
        if (entry.key.empty() || entry.key[0] == '#') continue;

        std::string blockName = entry.key;

        // Get or create block type ID
        auto& interner = StringInterner::global();
        BlockTypeId blockId = interner.intern(blockName);

        std::array<std::string, 6> faceTextures;

        // Check for suffix-based entries (block:top, block:bottom, etc.)
        if (entry.hasSuffix()) {
            // This is a face-specific entry like "grass:top: blocks:grass_top"
            Face face = Face::PosY;  // Default
            if (entry.suffix == "top") face = Face::PosY;
            else if (entry.suffix == "bottom") face = Face::NegY;
            else if (entry.suffix == "north") face = Face::NegZ;
            else if (entry.suffix == "south") face = Face::PosZ;
            else if (entry.suffix == "east") face = Face::PosX;
            else if (entry.suffix == "west") face = Face::NegX;
            else if (entry.suffix == "sides") {
                // Apply to all side faces
                std::string texName(entry.value.asString());
                auto& existing = blockTextureNames_[blockId.value()];
                existing[static_cast<size_t>(Face::PosX)] = texName;
                existing[static_cast<size_t>(Face::NegX)] = texName;
                existing[static_cast<size_t>(Face::PosZ)] = texName;
                existing[static_cast<size_t>(Face::NegZ)] = texName;
                continue;
            } else if (entry.suffix == "all") {
                // Apply to all faces
                std::string texName(entry.value.asString());
                auto& existing = blockTextureNames_[blockId.value()];
                for (auto& tex : existing) {
                    tex = texName;
                }
                continue;
            }

            auto& existing = blockTextureNames_[blockId.value()];
            existing[static_cast<size_t>(face)] = std::string(entry.value.asString());
        } else {
            // Simple entry like "stone: blocks:stone" - applies to all faces
            std::string texName(entry.value.asString());
            for (auto& tex : faceTextures) {
                tex = texName;
            }
            blockTextureNames_[blockId.value()] = faceTextures;
        }
    }

    return true;
}

std::string TextureManager::getBlockTextureName(BlockTypeId id, Face face) const {
    auto it = blockTextureNames_.find(id.value());
    if (it == blockTextureNames_.end()) {
        return "";
    }
    return it->second[static_cast<size_t>(face)];
}

}  // namespace finevox
