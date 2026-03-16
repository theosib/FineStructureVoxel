#pragma once

/**
 * @file icon_atlas.hpp
 * @brief Maps item/block types to UV regions in the block texture atlas
 *
 * Design: Phase 23-C Inventory UI Icons
 *
 * IconAtlas provides a lookup from item type names to UV regions in the
 * existing block texture atlas. This enables inventory UI widgets to display
 * block/item icons without creating separate GPU textures.
 *
 * The atlas uses the "front face" (NegZ) of each block as the icon,
 * which is the face typically seen in inventory UIs.
 */

#include "finevox/render/block_atlas.hpp"
#include "finevox/core/item_type.hpp"
#include "finevox/core/item_registry.hpp"

#include <glm/glm.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace finevox::render {

/// UV region for an icon (maps directly to ImGui::Image uv0/uv1)
struct IconRegion {
    glm::vec2 uv0{0.0f, 0.0f};  ///< Top-left UV
    glm::vec2 uv1{1.0f, 1.0f};  ///< Bottom-right UV
};

/**
 * @brief Maps item/block type names to UV regions in the block atlas
 *
 * Usage:
 * @code
 * IconAtlas icons;
 * icons.buildFromBlockAtlas(atlas);
 *
 * // Look up icon for an item type
 * if (auto region = icons.getIcon("stone")) {
 *     // Use region->uv0, region->uv1 with the block atlas texture
 * }
 * @endcode
 */
class IconAtlas {
public:
    IconAtlas() = default;

    /**
     * @brief Build icon mappings from a BlockAtlas
     *
     * For each block type that has texture info in the atlas, creates
     * an icon entry using the front face (NegZ) UV coordinates.
     *
     * @param atlas The block atlas to read UV mappings from
     */
    void buildFromBlockAtlas(const BlockAtlas& atlas);

    /**
     * @brief Manually register an icon for a type name
     * @param typeName The item/block type name
     * @param uv0 Top-left UV coordinate
     * @param uv1 Bottom-right UV coordinate
     */
    void registerIcon(std::string_view typeName, glm::vec2 uv0, glm::vec2 uv1);

    /**
     * @brief Look up an icon region by type name
     * @param typeName The item/block type name
     * @return Pointer to the IconRegion, or nullptr if not found
     */
    [[nodiscard]] const IconRegion* getIcon(std::string_view typeName) const;

    /**
     * @brief Check if an icon exists for a type name
     */
    [[nodiscard]] bool hasIcon(std::string_view typeName) const;

    /**
     * @brief Get the number of registered icons
     */
    [[nodiscard]] size_t size() const { return icons_.size(); }

    /**
     * @brief Get the texture registry name used for the atlas
     *
     * This is the name to register the block atlas texture under
     * in finegui::TextureRegistry (default: "block_atlas").
     */
    [[nodiscard]] const std::string& textureName() const { return textureName_; }

    /**
     * @brief Set the texture registry name
     */
    void setTextureName(const std::string& name) { textureName_ = name; }

private:
    struct StringHash {
        using is_transparent = void;
        size_t operator()(std::string_view sv) const {
            return std::hash<std::string_view>{}(sv);
        }
        size_t operator()(const std::string& s) const {
            return std::hash<std::string>{}(s);
        }
    };

    std::unordered_map<std::string, IconRegion, StringHash, std::equal_to<>> icons_;
    std::string textureName_ = "block_atlas";
};

}  // namespace finevox::render
