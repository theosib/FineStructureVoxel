#pragma once

/**
 * @file block_atlas.hpp
 * @brief Block texture atlas and UV coordinate lookups
 *
 * Design: [06-rendering.md] §6.6 Block Atlas
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"
#include "finevox/core/mesh.hpp"

#include <finevk/high/texture.hpp>
#include <finevk/device/command.hpp>

#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <array>

namespace finevox::render {

// ============================================================================
// BlockFaceTexture - Texture region for a single block face
// ============================================================================

struct BlockFaceTexture {
    glm::vec2 uvMin{0.0f, 0.0f};  // Top-left UV
    glm::vec2 uvMax{1.0f, 1.0f};  // Bottom-right UV

    // Get UV bounds as vec4 (minU, minV, maxU, maxV)
    [[nodiscard]] glm::vec4 bounds() const {
        return glm::vec4(uvMin.x, uvMin.y, uvMax.x, uvMax.y);
    }
};

// ============================================================================
// BlockTextureInfo - Texture information for all faces of a block
// ============================================================================

struct BlockTextureInfo {
    // Per-face textures (indexed by Face enum)
    std::array<BlockFaceTexture, 6> faces;

    // Set all faces to the same texture
    void setAll(const BlockFaceTexture& tex) {
        for (auto& face : faces) {
            face = tex;
        }
    }

    // Set top/bottom differently from sides
    void setTopBottom(const BlockFaceTexture& top, const BlockFaceTexture& bottom,
                      const BlockFaceTexture& sides) {
        faces[static_cast<size_t>(Face::PosY)] = top;
        faces[static_cast<size_t>(Face::NegY)] = bottom;
        faces[static_cast<size_t>(Face::PosX)] = sides;
        faces[static_cast<size_t>(Face::NegX)] = sides;
        faces[static_cast<size_t>(Face::PosZ)] = sides;
        faces[static_cast<size_t>(Face::NegZ)] = sides;
    }

    // Get texture for a specific face
    [[nodiscard]] const BlockFaceTexture& get(Face face) const {
        return faces[static_cast<size_t>(face)];
    }
};

// ============================================================================
// BlockAtlas - Manages block texture atlas and UV lookups
// ============================================================================

/**
 * @brief Block texture atlas manager
 *
 * Manages a texture atlas containing block textures and provides UV coordinate
 * lookups for each block type and face. Supports both grid-based atlases
 * (where each cell is the same size) and arbitrary region definitions.
 *
 * Usage (grid atlas):
 * @code
 * BlockAtlas atlas;
 *
 * // Load a 16x16 grid atlas (each cell is one block texture)
 * atlas.loadGridAtlas(device, commandPool, "blocks.png", 16, 16);
 *
 * // Map blocks to atlas positions
 * atlas.setBlockTexture(stoneId, 0, 0);  // Stone at grid (0,0)
 * atlas.setBlockTexture(dirtId, 1, 0);   // Dirt at grid (1,0)
 * atlas.setBlockTexture(grassId,         // Grass with different top/bottom/sides
 *     2, 0,   // top
 *     1, 0,   // bottom (dirt)
 *     3, 0);  // sides
 *
 * // Use with WorldRenderer
 * renderer.setBlockAtlas(atlas.texture());
 * renderer.setTextureProvider(atlas.createProvider());
 * @endcode
 */
class BlockAtlas {
public:
    BlockAtlas() = default;

    // ========================================================================
    // Atlas Loading
    // ========================================================================

    /**
     * @brief Load a grid-based texture atlas
     *
     * @param device Vulkan logical device
     * @param commandPool Command pool for texture upload
     * @param path Path to atlas image
     * @param gridWidth Number of cells horizontally
     * @param gridHeight Number of cells vertically
     * @param srgb Use sRGB format (default true for gamma-correct rendering)
     */
    void loadGridAtlas(
        finevk::LogicalDevice* device,
        finevk::CommandPool* commandPool,
        const std::string& path,
        uint32_t gridWidth,
        uint32_t gridHeight,
        bool srgb = true
    );

    /**
     * @brief Create a placeholder atlas with solid colors
     *
     * Useful for testing without actual textures.
     *
     * @param device Vulkan logical device
     * @param commandPool Command pool for texture upload
     * @param gridWidth Number of cells horizontally
     * @param gridHeight Number of cells vertically
     */
    void createPlaceholderAtlas(
        finevk::LogicalDevice* device,
        finevk::CommandPool* commandPool,
        uint32_t gridWidth = 16,
        uint32_t gridHeight = 16
    );

    // ========================================================================
    // Texture Mapping
    // ========================================================================

    /**
     * @brief Set texture for all faces of a block using grid coordinates
     */
    void setBlockTexture(BlockTypeId id, uint32_t gridX, uint32_t gridY);

    /**
     * @brief Set different textures for top, bottom, and sides
     */
    void setBlockTexture(BlockTypeId id,
                         uint32_t topX, uint32_t topY,
                         uint32_t bottomX, uint32_t bottomY,
                         uint32_t sideX, uint32_t sideY);

    /**
     * @brief Set texture for each face individually
     */
    void setBlockTexturePerFace(BlockTypeId id,
                                uint32_t posXx, uint32_t posXy,
                                uint32_t negXx, uint32_t negXy,
                                uint32_t posYx, uint32_t posYy,
                                uint32_t negYx, uint32_t negYy,
                                uint32_t posZx, uint32_t posZy,
                                uint32_t negZx, uint32_t negZy);

    /**
     * @brief Set texture using UV coordinates directly
     */
    void setBlockTextureUV(BlockTypeId id, const BlockTextureInfo& info);

    // ========================================================================
    // UV Lookup
    // ========================================================================

    /**
     * @brief Get UV bounds for a block face
     * @return vec4(minU, minV, maxU, maxV)
     */
    [[nodiscard]] glm::vec4 getUV(BlockTypeId id, Face face) const;

    /**
     * @brief Get full texture info for a block
     */
    [[nodiscard]] const BlockTextureInfo* getTextureInfo(BlockTypeId id) const;

    /**
     * @brief Create a BlockTextureProvider for use with MeshBuilder
     */
    [[nodiscard]] BlockTextureProvider createProvider() const;

    // ========================================================================
    // Atlas Access
    // ========================================================================

    /// Get the texture atlas
    [[nodiscard]] finevk::Texture* texture() const { return texture_.get(); }

    /// Check if atlas is loaded
    [[nodiscard]] bool isLoaded() const { return texture_ != nullptr; }

    /// Get atlas dimensions in pixels
    [[nodiscard]] uint32_t width() const { return atlasWidth_; }
    [[nodiscard]] uint32_t height() const { return atlasHeight_; }

    /// Get grid dimensions
    [[nodiscard]] uint32_t gridWidth() const { return gridWidth_; }
    [[nodiscard]] uint32_t gridHeight() const { return gridHeight_; }

private:
    // Convert grid coordinates to UV bounds
    [[nodiscard]] BlockFaceTexture gridToUV(uint32_t gridX, uint32_t gridY) const;

    finevk::TextureRef texture_;
    uint32_t atlasWidth_ = 0;
    uint32_t atlasHeight_ = 0;
    uint32_t gridWidth_ = 1;
    uint32_t gridHeight_ = 1;
    float cellWidth_ = 1.0f;   // UV width of one cell
    float cellHeight_ = 1.0f;  // UV height of one cell

    // Block texture mappings
    std::unordered_map<uint32_t, BlockTextureInfo> blockTextures_;

    // Default texture for unmapped blocks
    BlockTextureInfo defaultTexture_;
};

}  // namespace finevox::render
