#pragma once

#include "finevox/position.hpp"
#include "finevox/string_interner.hpp"
#include "finevox/mesh.hpp"
#include "finevox/resource_locator.hpp"

#include <finevk/high/texture.hpp>
#include <finevk/command/command_pool.hpp>

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <optional>

namespace finevox {

// ============================================================================
// TextureRegion - A rectangular region within a texture/atlas
// ============================================================================

struct TextureRegion {
    glm::vec2 uvMin{0.0f, 0.0f};  // Top-left UV
    glm::vec2 uvMax{1.0f, 1.0f};  // Bottom-right UV

    // Get UV bounds as vec4 (minU, minV, maxU, maxV)
    [[nodiscard]] glm::vec4 bounds() const {
        return glm::vec4(uvMin.x, uvMin.y, uvMax.x, uvMax.y);
    }

    // Check if this is the full texture (degenerate atlas case)
    [[nodiscard]] bool isFullTexture() const {
        return uvMin.x == 0.0f && uvMin.y == 0.0f &&
               uvMax.x == 1.0f && uvMax.y == 1.0f;
    }

    // Create a region for the full texture
    static TextureRegion full() { return TextureRegion{}; }

    // Create a region from pixel coordinates
    static TextureRegion fromPixels(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    uint32_t atlasW, uint32_t atlasH) {
        TextureRegion r;
        r.uvMin.x = static_cast<float>(x) / atlasW;
        r.uvMin.y = static_cast<float>(y) / atlasH;
        r.uvMax.x = static_cast<float>(x + w) / atlasW;
        r.uvMax.y = static_cast<float>(y + h) / atlasH;
        return r;
    }

    // Create a region from grid coordinates
    static TextureRegion fromGrid(uint32_t gridX, uint32_t gridY,
                                  uint32_t gridW, uint32_t gridH) {
        TextureRegion r;
        float cellW = 1.0f / gridW;
        float cellH = 1.0f / gridH;
        r.uvMin.x = gridX * cellW;
        r.uvMin.y = gridY * cellH;
        r.uvMax.x = (gridX + 1) * cellW;
        r.uvMax.y = (gridY + 1) * cellH;
        return r;
    }
};

// ============================================================================
// TextureHandle - Opaque reference to a texture region
// ============================================================================

struct TextureHandle {
    uint32_t atlasIndex = 0;   // Which atlas/texture this refers to
    TextureRegion region;       // Region within that atlas

    bool operator==(const TextureHandle& other) const {
        return atlasIndex == other.atlasIndex &&
               region.uvMin == other.region.uvMin &&
               region.uvMax == other.region.uvMax;
    }
};

// ============================================================================
// AtlasDefinition - Describes how an atlas is organized
// ============================================================================

struct AtlasDefinition {
    std::string name;           // Atlas identifier (e.g., "blocks", "items")
    std::string imagePath;      // Path to atlas image (resolved via ResourceLocator)

    // Grid-based atlas (uniform cells)
    bool isGrid = false;
    uint32_t gridWidth = 1;     // Number of cells horizontally
    uint32_t gridHeight = 1;    // Number of cells vertically

    // Named regions within the atlas
    // Maps texture name -> region (either grid coords or pixel coords)
    struct RegionDef {
        std::string name;
        // For grid atlases: grid coordinates
        uint32_t gridX = 0;
        uint32_t gridY = 0;
        // For pixel atlases: pixel coordinates
        uint32_t pixelX = 0;
        uint32_t pixelY = 0;
        uint32_t pixelW = 0;
        uint32_t pixelH = 0;
        bool usePixels = false;
    };
    std::vector<RegionDef> regions;

    // Load from config file (human-readable format)
    static std::optional<AtlasDefinition> loadFromFile(const std::string& path);
};

// ============================================================================
// TextureManager - Manages textures and atlases with named lookups
// ============================================================================

/**
 * @brief Unified texture and atlas management with named lookups
 *
 * TextureManager provides a clean abstraction over texture atlases:
 * - Register atlases from config files or programmatically
 * - Look up textures by name, get TextureHandle
 * - Use handles for rendering (atlas index + UV region)
 * - Single textures treated as single-region atlases
 *
 * Usage:
 * @code
 * TextureManager textures(device, commandPool);
 *
 * // Load atlas definition from config
 * textures.loadAtlas("game://textures/blocks.atlas");
 *
 * // Or register individual textures
 * textures.registerTexture("logo", "game://textures/logo.png");
 *
 * // Look up by name
 * auto stone = textures.getTexture("blocks:stone");
 * auto logo = textures.getTexture("logo");
 *
 * // Use for rendering
 * finevk::Texture* atlas = textures.getAtlasTexture(stone.atlasIndex);
 * glm::vec4 uvBounds = stone.region.bounds();
 * @endcode
 *
 * Naming conventions:
 * - "atlasName:regionName" for atlas textures (e.g., "blocks:stone_top")
 * - "textureName" for standalone textures (e.g., "logo")
 * - Atlas name defaults to filename without extension if not specified
 */
class TextureManager {
public:
    TextureManager(finevk::LogicalDevice* device, finevk::CommandPool* commandPool);
    ~TextureManager() = default;

    // Non-copyable
    TextureManager(const TextureManager&) = delete;
    TextureManager& operator=(const TextureManager&) = delete;

    // ========================================================================
    // Atlas Registration
    // ========================================================================

    /**
     * @brief Load an atlas from a definition file
     *
     * The definition file (CBOR format) specifies the image path and
     * named regions within the atlas.
     *
     * @param definitionPath Path to .atlas definition file (via ResourceLocator)
     * @return true if loaded successfully
     */
    bool loadAtlas(const std::string& definitionPath);

    /**
     * @brief Register a grid-based atlas programmatically
     *
     * @param name Atlas name (used as prefix in texture lookups)
     * @param imagePath Path to atlas image
     * @param gridWidth Number of cells horizontally
     * @param gridHeight Number of cells vertically
     * @return true if registered successfully
     */
    bool registerGridAtlas(const std::string& name, const std::string& imagePath,
                           uint32_t gridWidth, uint32_t gridHeight);

    /**
     * @brief Register a named region within an atlas
     *
     * For grid atlases, use grid coordinates.
     *
     * @param atlasName Name of the atlas
     * @param regionName Name of the region (e.g., "stone_top")
     * @param gridX Grid X coordinate
     * @param gridY Grid Y coordinate
     */
    void registerGridRegion(const std::string& atlasName, const std::string& regionName,
                            uint32_t gridX, uint32_t gridY);

    /**
     * @brief Register a named region with pixel coordinates
     */
    void registerPixelRegion(const std::string& atlasName, const std::string& regionName,
                             uint32_t x, uint32_t y, uint32_t w, uint32_t h);

    /**
     * @brief Register a standalone texture (degenerate single-region atlas)
     *
     * @param name Texture name for lookups
     * @param imagePath Path to texture image
     * @return true if registered successfully
     */
    bool registerTexture(const std::string& name, const std::string& imagePath);

    // ========================================================================
    // Texture Lookup
    // ========================================================================

    /**
     * @brief Get texture handle by name
     *
     * @param name Texture name ("atlas:region" or "standalone")
     * @return TextureHandle if found, nullopt otherwise
     */
    [[nodiscard]] std::optional<TextureHandle> getTexture(const std::string& name) const;

    /**
     * @brief Get texture handle, with fallback
     *
     * @param name Texture name
     * @param fallback Handle to return if not found
     * @return TextureHandle for name or fallback
     */
    [[nodiscard]] TextureHandle getTextureOr(const std::string& name,
                                              const TextureHandle& fallback) const;

    /**
     * @brief Check if a texture name is registered
     */
    [[nodiscard]] bool hasTexture(const std::string& name) const;

    // ========================================================================
    // Atlas Access
    // ========================================================================

    /**
     * @brief Get the GPU texture for an atlas index
     */
    [[nodiscard]] finevk::Texture* getAtlasTexture(uint32_t atlasIndex) const;

    /**
     * @brief Get atlas count
     */
    [[nodiscard]] uint32_t atlasCount() const { return static_cast<uint32_t>(atlases_.size()); }

    // ========================================================================
    // Block Texture Integration
    // ========================================================================

    /**
     * @brief Create a BlockTextureProvider from registered textures
     *
     * Maps block type IDs to texture handles using a callback that
     * provides texture names for each block/face combination.
     *
     * @param nameProvider Function that returns texture name for (blockId, face)
     * @return BlockTextureProvider for use with MeshBuilder
     */
    [[nodiscard]] BlockTextureProvider createBlockProvider(
        std::function<std::string(BlockTypeId, Face)> nameProvider) const;

    /**
     * @brief Register block textures from a config file
     *
     * Config file maps block names to texture names for each face.
     * Format: { "stone": { "all": "blocks:stone" },
     *           "grass": { "top": "blocks:grass_top", "bottom": "blocks:dirt", "sides": "blocks:grass_side" } }
     *
     * @param configPath Path to block texture config
     * @return true if loaded successfully
     */
    bool loadBlockTextureConfig(const std::string& configPath);

    /**
     * @brief Get texture name for a block face (from loaded config)
     */
    [[nodiscard]] std::string getBlockTextureName(BlockTypeId id, Face face) const;

private:
    struct AtlasEntry {
        std::string name;
        finevk::TextureRef texture;
        uint32_t gridWidth = 1;
        uint32_t gridHeight = 1;
        bool isGrid = false;
    };

    struct TextureEntry {
        uint32_t atlasIndex;
        TextureRegion region;
    };

    // Load a texture, using ResourceLocator for path resolution
    finevk::TextureRef loadTexture(const std::string& path);

    finevk::LogicalDevice* device_;
    finevk::CommandPool* commandPool_;

    // Registered atlases
    std::vector<AtlasEntry> atlases_;
    std::unordered_map<std::string, uint32_t> atlasNameToIndex_;

    // Named texture lookups ("atlas:region" or "standalone" -> entry)
    std::unordered_map<std::string, TextureEntry> textureMap_;

    // Block texture mappings (from config)
    // Maps (blockId, face) -> texture name
    std::unordered_map<uint32_t, std::array<std::string, 6>> blockTextureNames_;

    // Default/fallback texture
    TextureHandle fallbackTexture_;
};

}  // namespace finevox
