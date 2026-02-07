#include <gtest/gtest.h>
#include "finevox/core/rotation.hpp"
#include <set>

using namespace finevox;

// ============================================================================
// Identity rotation tests
// ============================================================================

TEST(RotationTest, IdentityIsIdentity) {
    EXPECT_TRUE(Rotation::IDENTITY.isIdentity());
}

TEST(RotationTest, IdentityDoesNotChangePosition) {
    BlockPos pos(5, 10, 15);
    EXPECT_EQ(Rotation::IDENTITY.apply(pos), pos);
}

TEST(RotationTest, IdentityDoesNotChangeFace) {
    for (int i = 0; i < 6; ++i) {
        Face face = static_cast<Face>(i);
        EXPECT_EQ(Rotation::IDENTITY.apply(face), face);
    }
}

// ============================================================================
// Basic rotation tests
// ============================================================================

TEST(RotationTest, RotateY90) {
    // Rotating 90 degrees around Y: X+ becomes Z+, Z+ becomes X-
    BlockPos pos(1, 0, 0);  // Point on X+ axis
    auto rotated = Rotation::ROTATE_Y_90.apply(pos);
    EXPECT_EQ(rotated, BlockPos(0, 0, -1));  // Should be on Z- axis
}

TEST(RotationTest, RotateY180) {
    BlockPos pos(1, 0, 0);
    auto rotated = Rotation::ROTATE_Y_180.apply(pos);
    EXPECT_EQ(rotated, BlockPos(-1, 0, 0));
}

TEST(RotationTest, RotateY270) {
    BlockPos pos(1, 0, 0);
    auto rotated = Rotation::ROTATE_Y_270.apply(pos);
    EXPECT_EQ(rotated, BlockPos(0, 0, 1));
}

TEST(RotationTest, RotateX90) {
    // Rotating 90 degrees around X: Y+ becomes Z+
    BlockPos pos(0, 1, 0);
    auto rotated = Rotation::ROTATE_X_90.apply(pos);
    EXPECT_EQ(rotated, BlockPos(0, 0, 1));
}

TEST(RotationTest, RotateZ90) {
    // Rotating 90 degrees around Z: X+ becomes Y+
    BlockPos pos(1, 0, 0);
    auto rotated = Rotation::ROTATE_Z_90.apply(pos);
    EXPECT_EQ(rotated, BlockPos(0, 1, 0));
}

// ============================================================================
// Face rotation tests
// ============================================================================

TEST(RotationTest, RotateFaceY90) {
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::PosX), Face::NegZ);
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::NegZ), Face::NegX);
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::NegX), Face::PosZ);
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::PosZ), Face::PosX);
    // Y faces unchanged
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::PosY), Face::PosY);
    EXPECT_EQ(Rotation::ROTATE_Y_90.apply(Face::NegY), Face::NegY);
}

TEST(RotationTest, RotateFaceX90) {
    EXPECT_EQ(Rotation::ROTATE_X_90.apply(Face::PosY), Face::PosZ);
    EXPECT_EQ(Rotation::ROTATE_X_90.apply(Face::PosZ), Face::NegY);
    // X faces unchanged
    EXPECT_EQ(Rotation::ROTATE_X_90.apply(Face::PosX), Face::PosX);
    EXPECT_EQ(Rotation::ROTATE_X_90.apply(Face::NegX), Face::NegX);
}

// ============================================================================
// Composition tests
// ============================================================================

TEST(RotationTest, ComposeWithIdentity) {
    auto composed = Rotation::ROTATE_Y_90.compose(Rotation::IDENTITY);
    EXPECT_EQ(composed, Rotation::ROTATE_Y_90);

    composed = Rotation::IDENTITY.compose(Rotation::ROTATE_Y_90);
    EXPECT_EQ(composed, Rotation::ROTATE_Y_90);
}

TEST(RotationTest, ComposeY90FourTimes) {
    auto rot = Rotation::IDENTITY;
    for (int i = 0; i < 4; ++i) {
        rot = rot.compose(Rotation::ROTATE_Y_90);
    }
    EXPECT_EQ(rot, Rotation::IDENTITY);
}

TEST(RotationTest, ComposeY90AndY270) {
    auto composed = Rotation::ROTATE_Y_90.compose(Rotation::ROTATE_Y_270);
    EXPECT_EQ(composed, Rotation::IDENTITY);
}

// ============================================================================
// Inverse tests
// ============================================================================

TEST(RotationTest, InverseOfIdentity) {
    EXPECT_EQ(Rotation::IDENTITY.inverse(), Rotation::IDENTITY);
}

TEST(RotationTest, InverseOfY90) {
    auto inv = Rotation::ROTATE_Y_90.inverse();
    EXPECT_EQ(inv, Rotation::ROTATE_Y_270);
}

TEST(RotationTest, InverseUndoesRotation) {
    BlockPos pos(3, 7, 11);
    auto rotated = Rotation::ROTATE_Y_90.apply(pos);
    auto restored = Rotation::ROTATE_Y_90.inverse().apply(rotated);
    EXPECT_EQ(restored, pos);
}

TEST(RotationTest, InverseComposeIsIdentity) {
    auto rot = Rotation::ROTATE_X_90.compose(Rotation::ROTATE_Y_90);
    auto inv = rot.inverse();
    auto composed = rot.compose(inv);
    EXPECT_EQ(composed, Rotation::IDENTITY);
}

// ============================================================================
// All 24 rotations tests
// ============================================================================

TEST(RotationTest, Exactly24UniqueRotations) {
    std::set<std::array<std::array<int8_t, 3>, 3>> matrices;
    for (uint8_t i = 0; i < 24; ++i) {
        matrices.insert(Rotation::byIndex(i).matrix());
    }
    EXPECT_EQ(matrices.size(), 24);
}

TEST(RotationTest, AllRotationsAreValid) {
    for (uint8_t i = 0; i < 24; ++i) {
        const auto& rot = Rotation::byIndex(i);

        // Check that it's an orthogonal matrix (columns are orthonormal)
        auto& m = rot.matrix();

        // Each row and column should have exactly one non-zero entry
        for (int r = 0; r < 3; ++r) {
            int nonZero = 0;
            for (int c = 0; c < 3; ++c) {
                if (m[r][c] != 0) {
                    EXPECT_TRUE(m[r][c] == 1 || m[r][c] == -1);
                    ++nonZero;
                }
            }
            EXPECT_EQ(nonZero, 1) << "Row " << r << " of rotation " << static_cast<int>(i);
        }

        for (int c = 0; c < 3; ++c) {
            int nonZero = 0;
            for (int r = 0; r < 3; ++r) {
                if (m[r][c] != 0) ++nonZero;
            }
            EXPECT_EQ(nonZero, 1) << "Column " << c << " of rotation " << static_cast<int>(i);
        }
    }
}

TEST(RotationTest, IndexRoundTrip) {
    for (uint8_t i = 0; i < 24; ++i) {
        const auto& rot = Rotation::byIndex(i);
        EXPECT_EQ(rot.index(), i);
    }
}

TEST(RotationTest, AllRotationsHaveInverse) {
    for (uint8_t i = 0; i < 24; ++i) {
        const auto& rot = Rotation::byIndex(i);
        auto inv = rot.inverse();
        auto composed = rot.compose(inv);
        EXPECT_EQ(composed, Rotation::IDENTITY)
            << "Rotation " << static_cast<int>(i) << " inverse failed";
    }
}

// ============================================================================
// AxisRotation tests
// ============================================================================

TEST(AxisRotationTest, FromQuarterTurns) {
    EXPECT_EQ(axisRotationFromQuarterTurns(0), AxisRotation::None);
    EXPECT_EQ(axisRotationFromQuarterTurns(1), AxisRotation::CW_90);
    EXPECT_EQ(axisRotationFromQuarterTurns(2), AxisRotation::CW_180);
    EXPECT_EQ(axisRotationFromQuarterTurns(3), AxisRotation::CCW_90);
    EXPECT_EQ(axisRotationFromQuarterTurns(4), AxisRotation::None);  // Wrap
    EXPECT_EQ(axisRotationFromQuarterTurns(-1), AxisRotation::CCW_90);  // Negative
}

TEST(AxisRotationTest, Compose) {
    EXPECT_EQ(compose(AxisRotation::CW_90, AxisRotation::CW_90), AxisRotation::CW_180);
    EXPECT_EQ(compose(AxisRotation::CW_90, AxisRotation::CCW_90), AxisRotation::None);
    EXPECT_EQ(compose(AxisRotation::CW_180, AxisRotation::CW_180), AxisRotation::None);
}

TEST(AxisRotationTest, Invert) {
    EXPECT_EQ(invert(AxisRotation::None), AxisRotation::None);
    EXPECT_EQ(invert(AxisRotation::CW_90), AxisRotation::CCW_90);
    EXPECT_EQ(invert(AxisRotation::CW_180), AxisRotation::CW_180);
    EXPECT_EQ(invert(AxisRotation::CCW_90), AxisRotation::CW_90);
}

// ============================================================================
// Horizontal rotation tests
// ============================================================================

TEST(HorizontalRotationTest, ApplyToCoordinates) {
    int32_t x = 5, z = 3;

    auto [x1, z1] = applyHorizontalRotation(AxisRotation::None, x, z);
    EXPECT_EQ(x1, 5);
    EXPECT_EQ(z1, 3);

    auto [x2, z2] = applyHorizontalRotation(AxisRotation::CW_90, x, z);
    EXPECT_EQ(x2, -3);
    EXPECT_EQ(z2, 5);

    auto [x3, z3] = applyHorizontalRotation(AxisRotation::CW_180, x, z);
    EXPECT_EQ(x3, -5);
    EXPECT_EQ(z3, -3);

    auto [x4, z4] = applyHorizontalRotation(AxisRotation::CCW_90, x, z);
    EXPECT_EQ(x4, 3);
    EXPECT_EQ(z4, -5);
}

TEST(HorizontalRotationTest, ApplyToFace) {
    // Horizontal faces should rotate
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::NegX), Face::PosZ);
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::PosZ), Face::PosX);
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::PosX), Face::NegZ);
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::NegZ), Face::NegX);

    // Vertical faces unchanged
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::PosY), Face::PosY);
    EXPECT_EQ(applyHorizontalRotation(AxisRotation::CW_90, Face::NegY), Face::NegY);
}

TEST(HorizontalRotationTest, FullRotation) {
    Face face = Face::PosX;
    for (int i = 0; i < 4; ++i) {
        face = applyHorizontalRotation(AxisRotation::CW_90, face);
    }
    EXPECT_EQ(face, Face::PosX);  // Back to original
}
