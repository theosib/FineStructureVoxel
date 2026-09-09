/**
 * @file schematic.cpp
 * @brief Schematic class and transformation utilities
 *
 * Design: [21-clipboard-schematic.md] Sections 21.4, 21.8
 */

#include "finevox/worldgen/schematic.hpp"

#include <algorithm>
#include <array>
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

/// Rotate a sub-block displacement as a vector in block-local axes.
/// Each output component is the corresponding row of the rotation matrix dotted
/// with the input. Matrix entries are only -1/0/+1, so this is exact in float.
glm::vec3 rotateDisplacement(const Rotation& rotation, glm::vec3 displacement) {
    const auto& m = rotation.matrix();
    glm::vec3 out{0.0f};
    for (int row = 0; row < 3; ++row) {
        out[row] = static_cast<float>(m[row][0]) * displacement.x +
                   static_cast<float>(m[row][1]) * displacement.y +
                   static_cast<float>(m[row][2]) * displacement.z;
    }
    return out;
}

/// Deep copy a BlockSnapshot, transforming its orientation and displacement
/// by `rotation`. The block's own orientation applies first, then the schematic
/// rotation, i.e. for every vector v:
///     dst.rotation.apply(v) == rotation.apply(src.rotation.apply(v))
/// Rotation::compose is a matrix product (`this * other`), so the schematic
/// rotation must be the left-hand operand.
void rotateSnapshotTo(BlockSnapshot& dst, const BlockSnapshot& src, const Rotation& rotation) {
    copySnapshotTo(dst, src);
    dst.rotation = rotation.compose(src.rotation);
    dst.displacement = rotateDisplacement(rotation, src.displacement);
}

/// The reflection that negates `axis` and leaves the other two axes alone.
/// `Rotation` stores a general 3x3 matrix of -1/0/+1 and its constructor accepts
/// any such matrix, so a determinant -1 transform is representable even though it
/// is not one of the 24 proper rotations. See the note in schematic.hpp: such a
/// value has no index() and therefore cannot survive the index-based world and
/// file encodings.
Rotation reflectionFor(Axis axis) {
    std::array<std::array<int8_t, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    m[static_cast<size_t>(axis)][static_cast<size_t>(axis)] = -1;
    return Rotation(m);
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
        // Fast path: cell mapping is the identity, so skip the bounding-box math.
        // Snapshots still go through rotateSnapshotTo so this stays observably
        // identical to the general path below.
        Schematic result(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ());
        result.setName(schematic.name());
        result.setAuthor(schematic.author());
        schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
            rotateSnapshotTo(result.at(pos), snap, rotation);
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
            rotateSnapshotTo(result.at(newPos), snap, rotation);
        }
    });

    return result;
}

Schematic mirrorSchematic(const Schematic& schematic, Axis axis) {
    Schematic result(schematic.sizeX(), schematic.sizeY(), schematic.sizeZ());
    result.setName(schematic.name());
    result.setAuthor(schematic.author());

    const Rotation mirror = reflectionFor(axis);

    schematic.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        glm::ivec3 newPos = pos;
        switch (axis) {
            case Axis::X: newPos.x = schematic.sizeX() - 1 - pos.x; break;
            case Axis::Y: newPos.y = schematic.sizeY() - 1 - pos.y; break;
            case Axis::Z: newPos.z = schematic.sizeZ() - 1 - pos.z; break;
        }

        // Same composition rule as rotateSchematic: the block's own orientation
        // applies first, then the schematic transform. Reusing rotateSnapshotTo
        // keeps the two transforms on one code path -- for a pure axis reflection
        // rotateDisplacement(M, d) reduces to negating d's component along `axis`,
        // which is what the previous hand-rolled path did, but the orientation was
        // being copied through untransformed.
        rotateSnapshotTo(result.at(newPos), snap, mirror);
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
