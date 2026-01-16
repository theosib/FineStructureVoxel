#pragma once

#include <cstdint>
#include <array>
#include "finevox/position.hpp"

namespace finevox {

// Block rotation utilities
// A cube has 24 possible orientations (the rotation group of a cube)
// We represent rotations as which face points "up" (Y+) and which points "forward" (Z+)
//
// For voxel games, rotations are used for:
// - Block orientation (stairs, logs, pistons)
// - Structure rotation during copy/paste
// - Transform local coordinates to world coordinates

// Axis identifier
enum class Axis : uint8_t {
    X = 0,
    Y = 1,
    Z = 2
};

// Represents one of 24 cube rotations
// Stored as a 3x3 rotation matrix with values -1, 0, or 1
class Rotation {
public:
    // Identity rotation (no change)
    static const Rotation IDENTITY;

    // Common rotations around each axis (90 degree increments)
    static const Rotation ROTATE_X_90;
    static const Rotation ROTATE_X_180;
    static const Rotation ROTATE_X_270;
    static const Rotation ROTATE_Y_90;
    static const Rotation ROTATE_Y_180;
    static const Rotation ROTATE_Y_270;
    static const Rotation ROTATE_Z_90;
    static const Rotation ROTATE_Z_180;
    static const Rotation ROTATE_Z_270;

    // Get rotation by index (0-23)
    static const Rotation& byIndex(uint8_t index);

    // Get number of rotations
    static constexpr uint8_t count() { return 24; }

    // Get the index of this rotation (0-23)
    [[nodiscard]] uint8_t index() const;

    // Construct identity rotation
    constexpr Rotation() : matrix_{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}} {}

    // Construct from matrix
    constexpr Rotation(std::array<std::array<int8_t, 3>, 3> m) : matrix_(m) {}

    // Apply rotation to a position (relative to origin)
    [[nodiscard]] constexpr BlockPos apply(BlockPos pos) const {
        return BlockPos(
            matrix_[0][0] * pos.x + matrix_[0][1] * pos.y + matrix_[0][2] * pos.z,
            matrix_[1][0] * pos.x + matrix_[1][1] * pos.y + matrix_[1][2] * pos.z,
            matrix_[2][0] * pos.x + matrix_[2][1] * pos.y + matrix_[2][2] * pos.z
        );
    }

    // Apply rotation to coordinates
    [[nodiscard]] constexpr std::array<int32_t, 3> apply(int32_t x, int32_t y, int32_t z) const {
        return {
            matrix_[0][0] * x + matrix_[0][1] * y + matrix_[0][2] * z,
            matrix_[1][0] * x + matrix_[1][1] * y + matrix_[1][2] * z,
            matrix_[2][0] * x + matrix_[2][1] * y + matrix_[2][2] * z
        };
    }

    // Apply rotation to a face
    [[nodiscard]] Face apply(Face face) const;

    // Compose two rotations (this * other)
    [[nodiscard]] Rotation compose(const Rotation& other) const;

    // Get the inverse rotation
    [[nodiscard]] Rotation inverse() const;

    // Check if this is the identity rotation
    [[nodiscard]] constexpr bool isIdentity() const {
        return matrix_[0][0] == 1 && matrix_[0][1] == 0 && matrix_[0][2] == 0 &&
               matrix_[1][0] == 0 && matrix_[1][1] == 1 && matrix_[1][2] == 0 &&
               matrix_[2][0] == 0 && matrix_[2][1] == 0 && matrix_[2][2] == 1;
    }

    // Get raw matrix
    [[nodiscard]] const std::array<std::array<int8_t, 3>, 3>& matrix() const { return matrix_; }

    // Equality comparison
    [[nodiscard]] bool operator==(const Rotation& other) const {
        return matrix_ == other.matrix_;
    }

    [[nodiscard]] bool operator!=(const Rotation& other) const {
        return !(*this == other);
    }

private:
    // 3x3 rotation matrix
    // matrix_[row][col], row = output axis, col = input axis
    std::array<std::array<int8_t, 3>, 3> matrix_;
};

// Axis-aligned rotation (4 rotations around a single axis)
// This is simpler than full Rotation and useful for 2D/horizontal rotations
enum class AxisRotation : uint8_t {
    None = 0,      // 0 degrees
    CW_90 = 1,     // 90 degrees clockwise (looking down axis)
    CW_180 = 2,    // 180 degrees
    CCW_90 = 3     // 90 degrees counter-clockwise (= 270 CW)
};

// Get AxisRotation by quarter turns
[[nodiscard]] constexpr AxisRotation axisRotationFromQuarterTurns(int turns) {
    return static_cast<AxisRotation>((turns % 4 + 4) % 4);
}

// Compose two AxisRotations
[[nodiscard]] constexpr AxisRotation compose(AxisRotation a, AxisRotation b) {
    return static_cast<AxisRotation>((static_cast<int>(a) + static_cast<int>(b)) % 4);
}

// Invert an AxisRotation
[[nodiscard]] constexpr AxisRotation invert(AxisRotation r) {
    return static_cast<AxisRotation>((4 - static_cast<int>(r)) % 4);
}

// Apply horizontal (Y-axis) rotation to coordinates
// Rotates around the Y axis (useful for block placement orientation)
[[nodiscard]] constexpr std::array<int32_t, 2> applyHorizontalRotation(
    AxisRotation rotation, int32_t x, int32_t z
) {
    switch (rotation) {
        case AxisRotation::None:   return {x, z};
        case AxisRotation::CW_90:  return {-z, x};
        case AxisRotation::CW_180: return {-x, -z};
        case AxisRotation::CCW_90: return {z, -x};
        default: return {x, z};
    }
}

// Apply horizontal rotation to a face
[[nodiscard]] constexpr Face applyHorizontalRotation(AxisRotation rotation, Face face) {
    // Only horizontal faces are affected
    if (face == Face::PosY || face == Face::NegY) {
        return face;
    }

    // Map faces to indices: NegX=0, PosX=1, NegZ=2, PosZ=3
    constexpr Face horizontalFaces[] = {Face::NegX, Face::PosZ, Face::PosX, Face::NegZ};

    int idx = 0;
    switch (face) {
        case Face::NegX: idx = 0; break;
        case Face::PosZ: idx = 1; break;
        case Face::PosX: idx = 2; break;
        case Face::NegZ: idx = 3; break;
        default: return face;
    }

    int newIdx = (idx + static_cast<int>(rotation)) % 4;
    return horizontalFaces[newIdx];
}

}  // namespace finevox
