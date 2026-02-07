/**
 * @file schematic.hpp
 * @brief Block snapshots, schematics, and transformation utilities
 *
 * Design: [21-clipboard-schematic.md] Sections 21.3-21.5, 21.8
 *
 * A Schematic stores a 3D region of BlockSnapshots for clipboard,
 * structure generation, and file-based templates.
 */

#pragma once

#include "finevox/data_container.hpp"
#include "finevox/rotation.hpp"

#include <cstdint>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace finevox {

// Forward declarations
class World;

// ============================================================================
// Block Snapshot
// ============================================================================

/// Complete snapshot of a block's state (portable format using string names)
struct BlockSnapshot {
    std::string typeName;                          ///< Block type name (e.g., "blockgame:stone")
    Rotation rotation = Rotation::IDENTITY;        ///< 24-state rotation
    glm::vec3 displacement{0.0f};                  ///< Sub-block offset
    std::optional<DataContainer> extraData;        ///< Tile entity data

    BlockSnapshot() = default;
    explicit BlockSnapshot(std::string_view type)
        : typeName(type) {}

    /// Check if this represents an air block
    [[nodiscard]] bool isAir() const {
        return typeName.empty() || typeName == "air";
    }

    /// Check if block has any non-default properties
    [[nodiscard]] bool hasMetadata() const {
        return rotation != Rotation::IDENTITY ||
               displacement != glm::vec3(0.0f) ||
               extraData.has_value();
    }
};

// ============================================================================
// Schematic
// ============================================================================

/// 3D region of block snapshots, stored in YZX order
class Schematic {
public:
    Schematic(int32_t sizeX, int32_t sizeY, int32_t sizeZ);

    // ---- Dimensions ----

    [[nodiscard]] int32_t sizeX() const { return sizeX_; }
    [[nodiscard]] int32_t sizeY() const { return sizeY_; }
    [[nodiscard]] int32_t sizeZ() const { return sizeZ_; }
    [[nodiscard]] glm::ivec3 size() const { return {sizeX_, sizeY_, sizeZ_}; }
    [[nodiscard]] int64_t volume() const {
        return static_cast<int64_t>(sizeX_) * sizeY_ * sizeZ_;
    }

    // ---- Block access ----

    [[nodiscard]] BlockSnapshot& at(int32_t x, int32_t y, int32_t z);
    [[nodiscard]] const BlockSnapshot& at(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] BlockSnapshot& at(glm::ivec3 pos) { return at(pos.x, pos.y, pos.z); }
    [[nodiscard]] const BlockSnapshot& at(glm::ivec3 pos) const { return at(pos.x, pos.y, pos.z); }

    [[nodiscard]] bool contains(int32_t x, int32_t y, int32_t z) const;
    [[nodiscard]] bool contains(glm::ivec3 pos) const {
        return contains(pos.x, pos.y, pos.z);
    }

    // ---- Iteration ----

    /// Iterate all non-air blocks. func(glm::ivec3 pos, const BlockSnapshot& snap)
    template<typename Func>
    void forEachBlock(Func&& func) const {
        for (int32_t x = 0; x < sizeX_; ++x) {
            for (int32_t z = 0; z < sizeZ_; ++z) {
                for (int32_t y = 0; y < sizeY_; ++y) {
                    const auto& snap = blocks_[index(x, y, z)];
                    if (!snap.isAir()) {
                        func(glm::ivec3(x, y, z), snap);
                    }
                }
            }
        }
    }

    // ---- Statistics ----

    [[nodiscard]] size_t nonAirBlockCount() const;
    [[nodiscard]] std::unordered_set<std::string> uniqueBlockTypes() const;

    // ---- Metadata ----

    void setName(std::string_view name) { name_ = name; }
    [[nodiscard]] std::string_view name() const { return name_; }
    void setAuthor(std::string_view author) { author_ = author; }
    [[nodiscard]] std::string_view author() const { return author_; }

private:
    int32_t sizeX_, sizeY_, sizeZ_;
    std::vector<BlockSnapshot> blocks_;
    std::string name_;
    std::string author_;

    [[nodiscard]] size_t index(int32_t x, int32_t y, int32_t z) const {
        return static_cast<size_t>(y + sizeY_ * (z + sizeZ_ * x));
    }
};

// ============================================================================
// Transformation utilities
// ============================================================================

/// Rotate schematic by a rotation (typically 90-degree Y-axis increments)
[[nodiscard]] Schematic rotateSchematic(const Schematic& schematic, Rotation rotation);

/// Mirror schematic along an axis
[[nodiscard]] Schematic mirrorSchematic(const Schematic& schematic, Axis axis);

/// Crop schematic to smallest bounding box containing non-air blocks
[[nodiscard]] Schematic cropSchematic(const Schematic& schematic);

/// Replace block types in schematic by name
[[nodiscard]] Schematic replaceBlocks(
    const Schematic& schematic,
    const std::unordered_map<std::string, std::string>& replacements);

}  // namespace finevox
