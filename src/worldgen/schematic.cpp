/**
 * @file schematic.cpp
 * @brief Schematic class and transformation utilities
 *
 * Design: [21-clipboard-schematic.md] Sections 21.4, 21.8
 */

#include "finevox/worldgen/schematic.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace finevox::worldgen {

namespace {

/// Deep copy a BlockSnapshot (handles move-only DataContainer via clone)
void copySnapshotTo(BlockSnapshot& dst, const BlockSnapshot& src) {
    dst.typeName = src.typeName;
    dst.rotation = src.rotation;
    dst.displacement = src.displacement;
    if (src.extraData.has_value()) {
        auto cloned = src.extraData->clone();
        dst.extraData = std::move(*cloned);
    } else {
        dst.extraData.reset();
    }
}

}  // namespace

// ============================================================================
// Schematic
// ============================================================================

Schematic::Schematic(int32_t sizeX, int32_t sizeY, int32_t sizeZ)
    : sizeX_(sizeX), sizeY_(sizeY), sizeZ_(sizeZ) {
    if (sizeX <= 0 || sizeY <= 0 || sizeZ <= 0) {
        throw std::invalid_argument("Schematic dimensions must be positive");
    }
    blocks_.resize(static_cast<size_t>(sizeX) * sizeY * sizeZ);
}

BlockSnapshot& Schematic::at(int32_t x, int32_t y, int32_t z) {
    if (!contains(x, y, z)) {
        throw std::out_of_range("Schematic::at out of bounds");
    }
    return blocks_[index(x, y, z)];
}

const BlockSnapshot& Schematic::at(int32_t x, int32_t y, int32_t z) const {
    if (!contains(x, y, z)) {
        throw std::out_of_range("Schematic::at out of bounds");
    }
    return blocks_[index(x, y, z)];
}

bool Schematic::contains(int32_t x, int32_t y, int32_t z) const {
    return x >= 0 && x < sizeX_ &&
           y >= 0 && y < sizeY_ &&
           z >= 0 && z < sizeZ_;
}

size_t Schematic::nonAirBlockCount() const {
    size_t count = 0;
    for (const auto& snap : blocks_) {
        if (!snap.isAir()) ++count;
    }
    return count;
}

std::unordered_set<std::string> Schematic::uniqueBlockTypes() const {
    std::unordered_set<std::string> types;
    for (const auto& snap : blocks_) {
        if (!snap.isAir()) {
            types.insert(snap.typeName);
        }
    }
    return types;
}

// ============================================================================
// Transformations
// ============================================================================

Schematic rotateSchematic(const Schematic& schematic, Rotation rotation) {
    if (rotation.isIdentity()) {
        // Copy without transformation
        Schematic result(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ());
        result.setName(schematic.name());
        result.setAuthor(schematic.author());
        schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
            copySnapshotTo(result.at(pos), snap);
        });
        return result;
    }

    // Determine new bounding box by rotating all 8 corners
    glm::ivec3 oldSize = schematic.size();
    glm::ivec3 corners[8] = {
        {0, 0, 0},
        {oldSize.x - 1, 0, 0},
        {0, oldSize.y - 1, 0},
        {0, 0, oldSize.z - 1},
        {oldSize.x - 1, oldSize.y - 1, 0},
        {oldSize.x - 1, 0, oldSize.z - 1},
        {0, oldSize.y - 1, oldSize.z - 1},
        {oldSize.x - 1, oldSize.y - 1, oldSize.z - 1}
    };

    glm::ivec3 minCorner(std::numeric_limits<int32_t>::max());
    glm::ivec3 maxCorner(std::numeric_limits<int32_t>::min());

    for (const auto& corner : corners) {
        auto [rx, ry, rz] = rotation.apply(corner.x, corner.y, corner.z);
        minCorner.x = std::min(minCorner.x, rx);
        minCorner.y = std::min(minCorner.y, ry);
        minCorner.z = std::min(minCorner.z, rz);
        maxCorner.x = std::max(maxCorner.x, rx);
        maxCorner.y = std::max(maxCorner.y, ry);
        maxCorner.z = std::max(maxCorner.z, rz);
    }

    glm::ivec3 newSize = maxCorner - minCorner + glm::ivec3(1);
    Schematic result(newSize.x, newSize.y, newSize.z);
    result.setName(schematic.name());
    result.setAuthor(schematic.author());

    schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        auto [rx, ry, rz] = rotation.apply(pos.x, pos.y, pos.z);
        glm::ivec3 newPos(rx - minCorner.x, ry - minCorner.y, rz - minCorner.z);

        if (result.contains(newPos)) {
            auto& dst = result.at(newPos);
            dst.typeName = snap.typeName;
            dst.rotation = snap.rotation.compose(rotation);
            dst.displacement = snap.displacement;
            if (snap.extraData.has_value()) {
                auto cloned = snap.extraData->clone();
                dst.extraData = std::move(*cloned);
            }
        }
    });

    return result;
}

Schematic mirrorSchematic(const Schematic& schematic, Axis axis) {
    Schematic result(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ());
    result.setName(schematic.name());
    result.setAuthor(schematic.author());

    schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        glm::ivec3 newPos = pos;
        switch (axis) {
            case Axis::X: newPos.x = schematic.sizeX() - 1 - pos.x; break;
            case Axis::Y: newPos.y = schematic.sizeY() - 1 - pos.y; break;
            case Axis::Z: newPos.z = schematic.sizeZ() - 1 - pos.z; break;
        }

        auto& dst = result.at(newPos);
        dst.typeName = snap.typeName;
        dst.rotation = snap.rotation;
        dst.displacement = snap.displacement;
        if (axis == Axis::X) dst.displacement.x = -dst.displacement.x;
        else if (axis == Axis::Y) dst.displacement.y = -dst.displacement.y;
        else if (axis == Axis::Z) dst.displacement.z = -dst.displacement.z;
        if (snap.extraData.has_value()) {
            auto cloned = snap.extraData->clone();
            dst.extraData = std::move(*cloned);
        }
    });

    return result;
}

Schematic cropSchematic(const Schematic& schematic) {
    glm::ivec3 minPos(std::numeric_limits<int32_t>::max());
    glm::ivec3 maxPos(std::numeric_limits<int32_t>::min());

    bool hasBlocks = false;
    schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot&) {
        hasBlocks = true;
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    });

    if (!hasBlocks) {
        return Schematic(1, 1, 1);
    }

    glm::ivec3 newSize = maxPos - minPos + glm::ivec3(1);
    Schematic result(newSize.x, newSize.y, newSize.z);
    result.setName(schematic.name());
    result.setAuthor(schematic.author());

    schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        glm::ivec3 newPos = pos - minPos;
        copySnapshotTo(result.at(newPos), snap);
    });

    return result;
}

Schematic replaceBlocks(
    const Schematic& schematic,
    const std::unordered_map<std::string, std::string>& replacements) {

    Schematic result(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ());
    result.setName(schematic.name());
    result.setAuthor(schematic.author());

    for (int32_t x = 0; x < schematic.sizeX(); ++x) {
        for (int32_t z = 0; z < schematic.sizeZ(); ++z) {
            for (int32_t y = 0; y < schematic.sizeY(); ++y) {
                const auto& snap = schematic.at(x, y, z);
                auto& dst = result.at(x, y, z);
                copySnapshotTo(dst, snap);

                if (!snap.isAir()) {
                    auto it = replacements.find(snap.typeName);
                    if (it != replacements.end()) {
                        dst.typeName = it->second;
                    }
                }
            }
        }
    }

    return result;
}

}  // namespace finevox::worldgen
