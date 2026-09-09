/**
 * @file test_schematic_transform.cpp
 * @brief Specification tests for rotateSchematic() and mirrorSchematic()
 *
 * Pins the rotation contract that structure templates depend on:
 *   1. result extent  = bounding box of the rotated cell-index box, min corner at origin
 *   2. cell mapping   = R.apply(p) - minCorner
 *   3. typeName       copied verbatim
 *   4. orientation    d.rotation.apply(v) == R.apply(s.rotation.apply(v)) for all v
 *   5. displacement   rotated as a vector by R's matrix (exact; entries are -1/0/+1)
 *   6. extraData      deep-copied
 *   7. name/author    preserved
 *   8. identity fast path observably identical to the general path
 *
 * And the matching contract for mirrorSchematic(), which applies the reflection
 * matrix M (identity with -1 in the mirrored slot) under the same
 * "block orientation first, then schematic transform" rule:
 *   9. extent unchanged; cell p[axis] -> size[axis] - 1 - p[axis]
 *  10. orientation    d.rotation.apply(v) == M.apply(s.rotation.apply(v)) for all v
 *  11. displacement   transformed by M (i.e. axis component negated)
 *  12. mirroring twice about the same axis is the identity
 */

#include "finevox/worldgen/schematic.hpp"
#include "finevox/worldgen/schematic_io.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

using namespace finevox;
using namespace finevox::worldgen;

namespace {

/// A rotation that is not a rotation about a single coordinate axis:
/// ROTATE_X_90 * ROTATE_Y_90 = cyclic axis permutation (120 degrees about (1,1,1)).
Rotation nonAxisAligned() {
    return Rotation::ROTATE_X_90.compose(Rotation::ROTATE_Y_90);
}

/// Expected displacement per clause 5, derived straight from R's matrix.
glm::vec3 expectedDisplacement(const Rotation& r, glm::vec3 d) {
    const auto& m = r.matrix();
    glm::vec3 out{0.0f};
    for (int row = 0; row < 3; ++row) {
        out[row] = static_cast<float>(m[row][0]) * d.x +
                   static_cast<float>(m[row][1]) * d.y +
                   static_cast<float>(m[row][2]) * d.z;
    }
    return out;
}

/// Compare two DataContainers by key set and scalar values (int64/string/double).
void expectSameData(const DataContainer& a, const DataContainer& b) {
    ASSERT_EQ(a.size(), b.size());
    a.forEach([&](DataKey key, const DataValue& value) {
        const DataValue* other = b.getRaw(key);
        ASSERT_NE(other, nullptr) << "missing key " << lookupKey(key);
        ASSERT_EQ(value.index(), other->index()) << "type mismatch for " << lookupKey(key);
        if (std::holds_alternative<int64_t>(value)) {
            EXPECT_EQ(std::get<int64_t>(value), std::get<int64_t>(*other));
        } else if (std::holds_alternative<std::string>(value)) {
            EXPECT_EQ(std::get<std::string>(value), std::get<std::string>(*other));
        } else if (std::holds_alternative<double>(value)) {
            EXPECT_EQ(std::get<double>(value), std::get<double>(*other));
        }
    });
}

void expectSameSnapshot(const BlockSnapshot& a, const BlockSnapshot& b, const char* where) {
    SCOPED_TRACE(where);
    EXPECT_EQ(a.typeName, b.typeName);
    EXPECT_EQ(a.rotation, b.rotation);
    EXPECT_EQ(a.displacement, b.displacement);
    ASSERT_EQ(a.extraData.has_value(), b.extraData.has_value());
    if (a.extraData.has_value()) {
        expectSameData(*a.extraData, *b.extraData);
    }
}

void expectSameSchematic(const Schematic& a, const Schematic& b) {
    ASSERT_EQ(a.size(), b.size());
    for (int32_t x = 0; x < a.sizeX(); ++x) {
        for (int32_t y = 0; y < a.sizeY(); ++y) {
            for (int32_t z = 0; z < a.sizeZ(); ++z) {
                std::string where = "cell (" + std::to_string(x) + "," + std::to_string(y) +
                                    "," + std::to_string(z) + ")";
                expectSameSnapshot(a.at(x, y, z), b.at(x, y, z), where.c_str());
            }
        }
    }
}

/// A small schematic exercising every per-block field.
Schematic makeRichSchematic() {
    Schematic s(2, 2, 3);
    s.setName("rich");
    s.setAuthor("tester");

    int counter = 0;
    for (int32_t x = 0; x < 2; ++x) {
        for (int32_t y = 0; y < 2; ++y) {
            for (int32_t z = 0; z < 3; ++z) {
                if ((x + y + z) % 2 != 0) continue;  // leave some air
                auto& snap = s.at(x, y, z);
                snap.typeName = "block_" + std::to_string(counter);
                snap.rotation = Rotation::byIndex(static_cast<uint8_t>(counter * 5 + 1));
                snap.displacement = glm::vec3(0.25f * static_cast<float>(counter % 3),
                                              0.5f,
                                              -0.25f);
                DataContainer data;
                data.set<int64_t>("counter", counter);
                data.set<std::string>("label", "cell" + std::to_string(counter));
                snap.extraData = std::move(data);
                ++counter;
            }
        }
    }
    EXPECT_GT(counter, 0);
    return s;
}

// ---- Transform invariants -------------------------------------------------
// Debug-only checks: they live in the test translation unit and are called from
// the tests below. Production code never invokes them, so they stay off the
// worldgen hot paths.

/// Determinant of a Rotation's 3x3 matrix. +1 for the 24 proper rotations,
/// -1 for a reflection.
int determinant(const Rotation& r) {
    const auto& m = r.matrix();
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

/// The reflection that negates `axis` -- the transform mirrorSchematic must apply.
/// Derived here independently of the production helper so the two can disagree.
Rotation reflectionMatrix(Axis axis) {
    std::array<std::array<int8_t, 3>, 3> m{{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}};
    m[static_cast<size_t>(axis)][static_cast<size_t>(axis)] = -1;
    return Rotation(m);
}

/// Invariant: applying a quarter-turn rotation four times about the same axis
/// returns the original schematic exactly -- cells, rotations and displacements.
/// Only meaningful for order-4 rotations (the ROTATE_<axis>_90 family).
void checkFourfoldRotationIsIdentity(const Schematic& s, const Rotation& quarterTurn) {
    ASSERT_EQ(quarterTurn.compose(quarterTurn).compose(quarterTurn).compose(quarterTurn),
              Rotation::IDENTITY)
        << "helper misused: rotation is not order 4";
    Schematic acc = rotateSchematic(s, quarterTurn);
    for (int i = 1; i < 4; ++i) {
        acc = rotateSchematic(acc, quarterTurn);
    }
    expectSameSchematic(s, acc);
}

/// Invariant: mirroring twice about the same axis is the identity.
void checkDoubleMirrorIsIdentity(const Schematic& s, Axis axis) {
    expectSameSchematic(s, mirrorSchematic(mirrorSchematic(s, axis), axis));
}

/// Invariant: `dst` carries `src`'s orientation transformed by `M`, checked on all
/// three basis vectors -- i.e. dst.rotation.apply(v) == M.apply(src.rotation.apply(v)).
void checkOrientationComposed(const Rotation& dst, const Rotation& src, const Rotation& M) {
    const BlockCoord basis[3] = {BlockCoord(1, 0, 0), BlockCoord(0, 1, 0), BlockCoord(0, 0, 1)};
    for (const auto& v : basis) {
        BlockCoord expected = M.apply(src.apply(v));
        BlockCoord actual = dst.apply(v);
        EXPECT_EQ(actual.x, expected.x);
        EXPECT_EQ(actual.y, expected.y);
        EXPECT_EQ(actual.z, expected.z);
    }
}

}  // namespace

// ============================================================================
// a. Cell mapping and extent
// ============================================================================

TEST(SchematicRotateTest, Y90Positions) {
    // ROTATE_Y_90 maps (x,y,z) -> (z, y, -x).
    // Index box x in [0,1], z in [0,2] rotates to x' in [0,2], z' in [-1,0],
    // so minCorner = (0,0,-1) and the new size is (3,1,2).
    Schematic s(2, 1, 3);
    s.at(0, 0, 0).typeName = "a";
    s.at(0, 0, 1).typeName = "b";
    s.at(0, 0, 2).typeName = "c";
    s.at(1, 0, 0).typeName = "d";
    s.at(1, 0, 1).typeName = "e";
    s.at(1, 0, 2).typeName = "f";

    auto r = rotateSchematic(s, Rotation::ROTATE_Y_90);
    EXPECT_EQ(r.sizeX(), 3);
    EXPECT_EQ(r.sizeY(), 1);
    EXPECT_EQ(r.sizeZ(), 2);

    // destination = (z, y, 1 - x)
    EXPECT_EQ(r.at(0, 0, 1).typeName, "a");
    EXPECT_EQ(r.at(1, 0, 1).typeName, "b");
    EXPECT_EQ(r.at(2, 0, 1).typeName, "c");
    EXPECT_EQ(r.at(0, 0, 0).typeName, "d");
    EXPECT_EQ(r.at(1, 0, 0).typeName, "e");
    EXPECT_EQ(r.at(2, 0, 0).typeName, "f");

    EXPECT_EQ(r.nonAirBlockCount(), 6u);
}

// ============================================================================
// b. Orientation composition (clause 4)
// ============================================================================

TEST(SchematicRotateTest, ComposesBlockOrientation) {
    const Rotation sourceRot = Rotation::ROTATE_X_90;   // (x,y,z) -> (x, -z, y)
    const Rotation R = Rotation::ROTATE_Y_90;           // (x,y,z) -> (z, y, -x)

    Schematic s(1, 1, 1);
    s.at(0, 0, 0).typeName = "stairs";
    s.at(0, 0, 0).rotation = sourceRot;

    auto r = rotateSchematic(s, R);
    ASSERT_EQ(r.size(), glm::ivec3(1, 1, 1));
    const Rotation& result = r.at(0, 0, 0).rotation;

    const BlockCoord basis[3] = {BlockCoord(1, 0, 0), BlockCoord(0, 1, 0), BlockCoord(0, 0, 1)};
    for (const auto& v : basis) {
        // Expectation derived from the spec: block orientation first, then schematic rotation.
        BlockCoord expected = R.apply(sourceRot.apply(v));
        BlockCoord actual = result.apply(v);
        EXPECT_EQ(actual.x, expected.x);
        EXPECT_EQ(actual.y, expected.y);
        EXPECT_EQ(actual.z, expected.z);
    }

    // Same expectation, spelled out by hand so the test does not lean on Rotation::apply:
    //   (1,0,0) -X90-> (1,0,0) -Y90-> (0,0,-1)
    //   (0,1,0) -X90-> (0,0,1)  -Y90-> (1,0,0)
    //   (0,0,1) -X90-> (0,-1,0) -Y90-> (0,-1,0)
    EXPECT_EQ(result.apply(BlockCoord(1, 0, 0)), BlockCoord(0, 0, -1));
    EXPECT_EQ(result.apply(BlockCoord(0, 1, 0)), BlockCoord(1, 0, 0));
    EXPECT_EQ(result.apply(BlockCoord(0, 0, 1)), BlockCoord(0, -1, 0));

    // A block with identity orientation ends up with exactly the schematic rotation.
    Schematic plain(1, 1, 1);
    plain.at(0, 0, 0).typeName = "stone";
    EXPECT_EQ(rotateSchematic(plain, R).at(0, 0, 0).rotation, R);
}

// ============================================================================
// c. Displacement (clause 5)
// ============================================================================

TEST(SchematicRotateTest, TransformsDisplacement) {
    const glm::vec3 disp(0.25f, 0.5f, 0.0f);

    for (const Rotation& R : {Rotation::ROTATE_Y_90, nonAxisAligned()}) {
        Schematic s(1, 1, 1);
        s.at(0, 0, 0).typeName = "slab";
        s.at(0, 0, 0).displacement = disp;

        auto r = rotateSchematic(s, R);
        glm::vec3 expected = expectedDisplacement(R, disp);
        glm::vec3 actual = r.at(0, 0, 0).displacement;
        EXPECT_EQ(actual.x, expected.x);
        EXPECT_EQ(actual.y, expected.y);
        EXPECT_EQ(actual.z, expected.z);
    }

    // Hand-computed values for both rotations (no epsilon: entries are -1/0/+1).
    {
        Schematic s(1, 1, 1);
        s.at(0, 0, 0).typeName = "slab";
        s.at(0, 0, 0).displacement = disp;
        // ROTATE_Y_90 rows: (0,0,1) (0,1,0) (-1,0,0)
        auto r = rotateSchematic(s, Rotation::ROTATE_Y_90);
        EXPECT_EQ(r.at(0, 0, 0).displacement.x, 0.0f);
        EXPECT_EQ(r.at(0, 0, 0).displacement.y, 0.5f);
        EXPECT_EQ(r.at(0, 0, 0).displacement.z, -0.25f);

        // nonAxisAligned rows: (0,0,1) (1,0,0) (0,1,0)
        auto r2 = rotateSchematic(s, nonAxisAligned());
        EXPECT_EQ(r2.at(0, 0, 0).displacement.x, 0.0f);
        EXPECT_EQ(r2.at(0, 0, 0).displacement.y, 0.25f);
        EXPECT_EQ(r2.at(0, 0, 0).displacement.z, 0.5f);
    }
}

// ============================================================================
// d. Invertibility over all 24 rotations
// ============================================================================

TEST(SchematicRotateTest, AllTwentyFourAreInvertible) {
    const Schematic original = makeRichSchematic();

    for (uint8_t i = 0; i < Rotation::count(); ++i) {
        SCOPED_TRACE("rotation index " + std::to_string(static_cast<int>(i)));
        const Rotation& R = Rotation::byIndex(i);
        auto forward = rotateSchematic(original, R);
        auto back = rotateSchematic(forward, R.inverse());
        expectSameSchematic(original, back);
    }
}

// ============================================================================
// e. Composition of rotations
// ============================================================================

TEST(SchematicRotateTest, ComposesWithItself) {
    const Schematic original = makeRichSchematic();

    for (const Rotation& R : {Rotation::ROTATE_Y_90, nonAxisAligned()}) {
        auto twice = rotateSchematic(rotateSchematic(original, R), R);
        auto squared = rotateSchematic(original, R.compose(R));
        expectSameSchematic(twice, squared);
    }
}

// ============================================================================
// f. Counts and metadata (clauses 3, 6, 7)
// ============================================================================

TEST(SchematicRotateTest, PreservesCountAndMetadata) {
    const Schematic original = makeRichSchematic();
    const size_t count = original.nonAirBlockCount();
    ASSERT_GT(count, 0u);

    auto r = rotateSchematic(original, Rotation::ROTATE_Y_90);
    EXPECT_EQ(r.nonAirBlockCount(), count);
    EXPECT_EQ(r.name(), original.name());
    EXPECT_EQ(r.author(), original.author());
    EXPECT_EQ(r.uniqueBlockTypes(), original.uniqueBlockTypes());

    // Every source block's extraData shows up at its rotated cell, deep-copied.
    // ROTATE_Y_90 on a 2x2x3 box: rotated x in [0,2], z in [-1,0] -> minCorner (0,0,-1).
    EXPECT_EQ(r.size(), glm::ivec3(3, 2, 2));
    original.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        auto [rx, ry, rz] = Rotation::ROTATE_Y_90.apply(pos.x, pos.y, pos.z);
        glm::ivec3 dst(rx, ry, rz + 1);
        ASSERT_TRUE(r.contains(dst));
        const auto& out = r.at(dst);
        EXPECT_EQ(out.typeName, snap.typeName);
        ASSERT_TRUE(snap.extraData.has_value());
        ASSERT_TRUE(out.extraData.has_value());
        EXPECT_NE(&*out.extraData, &*snap.extraData);  // deep copy, not aliased
        expectSameData(*snap.extraData, *out.extraData);
    });
}

TEST(SchematicRotateTest, IdentityMatchesGeneralPath) {
    // Clause 8: the identity fast path must be observably identical to the
    // general path. ROTATE_Y_90 composed four times is the identity matrix, but
    // takes the general path each step, so the two must agree.
    const Schematic original = makeRichSchematic();

    auto viaFastPath = rotateSchematic(original, Rotation::IDENTITY);
    expectSameSchematic(original, viaFastPath);
    EXPECT_EQ(viaFastPath.name(), original.name());
    EXPECT_EQ(viaFastPath.author(), original.author());

    auto viaGeneralPath = rotateSchematic(
        rotateSchematic(rotateSchematic(rotateSchematic(original, Rotation::ROTATE_Y_90),
                                        Rotation::ROTATE_Y_90),
                        Rotation::ROTATE_Y_90),
        Rotation::ROTATE_Y_90);
    expectSameSchematic(viaFastPath, viaGeneralPath);
}

// ============================================================================
// g. File round-trip after rotation
// ============================================================================

TEST(SchematicRotateTest, RotateThenFileRoundTrip) {
    Schematic s(2, 1, 2);
    s.setName("template");
    s.setAuthor("tester");
    s.at(0, 0, 0).typeName = "stairs";
    s.at(0, 0, 0).rotation = Rotation::ROTATE_X_90;
    s.at(0, 0, 0).displacement = glm::vec3(0.25f, 0.5f, 0.0f);
    s.at(1, 0, 1).typeName = "chest";
    {
        DataContainer data;
        data.set<int64_t>("slots", 27);
        s.at(1, 0, 1).extraData = std::move(data);
    }

    auto rotated = rotateSchematic(s, Rotation::ROTATE_Y_90);
    auto bytes = serializeSchematic(rotated);
    auto loaded = deserializeSchematic(bytes);

    ASSERT_EQ(loaded.size(), rotated.size());
    for (int32_t x = 0; x < rotated.sizeX(); ++x) {
        for (int32_t y = 0; y < rotated.sizeY(); ++y) {
            for (int32_t z = 0; z < rotated.sizeZ(); ++z) {
                const auto& a = rotated.at(x, y, z);
                const auto& b = loaded.at(x, y, z);
                EXPECT_EQ(a.typeName, b.typeName);
                // rotation survives as its 0..23 index
                EXPECT_EQ(a.rotation.index(), b.rotation.index());
                EXPECT_EQ(a.rotation, b.rotation);
                EXPECT_EQ(a.displacement, b.displacement);
            }
        }
    }

    // The rotated stairs block: source rotation X_90 composed under Y_90.
    const glm::ivec3 stairsPos(0, 0, 1);  // (0,0,0) -> (0,0,-0) shifted by minCorner (0,0,-1)
    const auto& stairs = loaded.at(stairsPos);
    EXPECT_EQ(stairs.typeName, "stairs");
    EXPECT_EQ(stairs.rotation, Rotation::ROTATE_Y_90.compose(Rotation::ROTATE_X_90));
    EXPECT_EQ(stairs.displacement, expectedDisplacement(Rotation::ROTATE_Y_90,
                                                        glm::vec3(0.25f, 0.5f, 0.0f)));
}

// ============================================================================
// h. Invariant checks (rotation side)
// ============================================================================

TEST(SchematicRotateTest, FourQuarterTurnsAreIdentity) {
    const Schematic original = makeRichSchematic();
    for (const Rotation& R : {Rotation::ROTATE_X_90,
                              Rotation::ROTATE_Y_90,
                              Rotation::ROTATE_Z_90}) {
        SCOPED_TRACE("quarter turn about one axis");
        checkFourfoldRotationIsIdentity(original, R);
    }
}

// ============================================================================
// i. Mirror: cell mapping and extent (clause 9)
// ============================================================================

TEST(SchematicMirrorTest, CellMappingAndExtent) {
    Schematic s(2, 1, 3);
    s.at(0, 0, 0).typeName = "a";
    s.at(1, 0, 2).typeName = "b";

    auto mx = mirrorSchematic(s, Axis::X);
    EXPECT_EQ(mx.size(), glm::ivec3(2, 1, 3));
    EXPECT_EQ(mx.at(1, 0, 0).typeName, "a");
    EXPECT_EQ(mx.at(0, 0, 2).typeName, "b");

    auto mz = mirrorSchematic(s, Axis::Z);
    EXPECT_EQ(mz.at(0, 0, 2).typeName, "a");
    EXPECT_EQ(mz.at(1, 0, 0).typeName, "b");

    EXPECT_EQ(mx.nonAirBlockCount(), 2u);
    EXPECT_EQ(mz.nonAirBlockCount(), 2u);
}

// ============================================================================
// j. Mirror: orientation composition (clause 10) -- the defect
// ============================================================================

TEST(SchematicMirrorTest, TransformsBlockOrientation) {
    const Rotation sourceRot = Rotation::ROTATE_Y_90;   // rows (0,0,1) (0,1,0) (-1,0,0)
    const Rotation M = reflectionMatrix(Axis::X);       // diag(-1, 1, 1)

    Schematic s(1, 1, 1);
    s.at(0, 0, 0).typeName = "stairs";
    s.at(0, 0, 0).rotation = sourceRot;

    auto m = mirrorSchematic(s, Axis::X);
    const Rotation& result = m.at(0, 0, 0).rotation;

    checkOrientationComposed(result, sourceRot, M);

    // Hand-computed: M negates row 0 of sourceRot, giving rows
    // (0,0,-1) (0,1,0) (-1,0,0).
    EXPECT_EQ(result.apply(BlockCoord(1, 0, 0)), BlockCoord(0, 0, -1));
    EXPECT_EQ(result.apply(BlockCoord(0, 1, 0)), BlockCoord(0, 1, 0));
    EXPECT_EQ(result.apply(BlockCoord(0, 0, 1)), BlockCoord(-1, 0, 0));

    // The orientation must actually change -- copying it through untransformed
    // (the old behaviour) leaves mirrored stairs facing the original way.
    EXPECT_NE(result, sourceRot);

    // A block with identity orientation ends up with exactly the reflection.
    Schematic plain(1, 1, 1);
    plain.at(0, 0, 0).typeName = "stone";
    EXPECT_EQ(mirrorSchematic(plain, Axis::X).at(0, 0, 0).rotation, M);
}

TEST(SchematicMirrorTest, ComposesOrientationForEveryAxisAndRotation) {
    // Sweep the whole parameter space: 3 mirror axes x all 24 block orientations.
    for (Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
        const Rotation M = reflectionMatrix(axis);
        for (uint8_t i = 0; i < Rotation::count(); ++i) {
            SCOPED_TRACE("axis " + std::to_string(static_cast<int>(axis)) +
                         " rotation index " + std::to_string(static_cast<int>(i)));
            const Rotation& sourceRot = Rotation::byIndex(i);

            Schematic s(1, 1, 1);
            s.at(0, 0, 0).typeName = "block";
            s.at(0, 0, 0).rotation = sourceRot;

            const Schematic mirrored = mirrorSchematic(s, axis);
            const Rotation& result = mirrored.at(0, 0, 0).rotation;
            checkOrientationComposed(result, sourceRot, M);
            EXPECT_EQ(result, M.compose(sourceRot));
        }
    }
}

// ============================================================================
// k. Mirror: displacement (clause 11)
// ============================================================================

TEST(SchematicMirrorTest, TransformsDisplacement) {
    const glm::vec3 disp(0.25f, 0.5f, -0.125f);

    for (Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
        SCOPED_TRACE("axis " + std::to_string(static_cast<int>(axis)));
        Schematic s(1, 1, 1);
        s.at(0, 0, 0).typeName = "slab";
        s.at(0, 0, 0).displacement = disp;

        glm::vec3 actual = mirrorSchematic(s, axis).at(0, 0, 0).displacement;
        glm::vec3 expected = expectedDisplacement(reflectionMatrix(axis), disp);
        EXPECT_EQ(actual.x, expected.x);
        EXPECT_EQ(actual.y, expected.y);
        EXPECT_EQ(actual.z, expected.z);

        // Spelled out: only the mirrored component flips, exactly (no epsilon).
        for (int c = 0; c < 3; ++c) {
            float want = (c == static_cast<int>(axis)) ? -disp[c] : disp[c];
            EXPECT_EQ(actual[c], want);
        }
    }
}

// ============================================================================
// l. Mirror: determinant / representability gap
// ============================================================================

TEST(SchematicMirrorTest, MirroredOrientationIsAReflection) {
    // Pins the documented limitation: the mirrored orientation has determinant -1,
    // so it is not one of the 24 proper rotations and has no valid index().
    // compose/apply/inverse still behave correctly on it.
    Schematic s(1, 1, 1);
    s.at(0, 0, 0).typeName = "stairs";
    s.at(0, 0, 0).rotation = Rotation::ROTATE_Y_90;

    const Schematic mirrored = mirrorSchematic(s, Axis::X);
    const Rotation& result = mirrored.at(0, 0, 0).rotation;
    EXPECT_EQ(determinant(result), -1);
    EXPECT_EQ(determinant(Rotation::ROTATE_Y_90), 1);

    // inverse() is the transpose, which is a genuine inverse for any orthogonal
    // matrix, reflections included.
    EXPECT_EQ(result.compose(result.inverse()), Rotation::IDENTITY);

    // index() has no entry for a reflection and reports 0 (identity) -- this is why
    // a mirrored schematic cannot currently be stamped into the world or saved
    // without losing orientation. See mirrorSchematic()'s @warning.
    EXPECT_EQ(result.index(), 0);
    EXPECT_NE(Rotation::byIndex(result.index()), result);
}

// ============================================================================
// m. Mirror: invariants and metadata (clause 12)
// ============================================================================

TEST(SchematicMirrorTest, MirrorTwiceIsIdentity) {
    const Schematic original = makeRichSchematic();
    for (Axis axis : {Axis::X, Axis::Y, Axis::Z}) {
        SCOPED_TRACE("axis " + std::to_string(static_cast<int>(axis)));
        checkDoubleMirrorIsIdentity(original, axis);
    }
}

TEST(SchematicMirrorTest, PreservesCountAndMetadata) {
    const Schematic original = makeRichSchematic();
    const size_t count = original.nonAirBlockCount();
    ASSERT_GT(count, 0u);

    auto m = mirrorSchematic(original, Axis::X);
    EXPECT_EQ(m.nonAirBlockCount(), count);
    EXPECT_EQ(m.size(), original.size());
    EXPECT_EQ(m.name(), original.name());
    EXPECT_EQ(m.author(), original.author());
    EXPECT_EQ(m.uniqueBlockTypes(), original.uniqueBlockTypes());

    original.forEachBlock([&](glm::ivec3 pos, const BlockSnapshot& snap) {
        glm::ivec3 dst(original.sizeX() - 1 - pos.x, pos.y, pos.z);
        ASSERT_TRUE(m.contains(dst));
        const auto& out = m.at(dst);
        EXPECT_EQ(out.typeName, snap.typeName);
        ASSERT_TRUE(snap.extraData.has_value());
        ASSERT_TRUE(out.extraData.has_value());
        EXPECT_NE(&*out.extraData, &*snap.extraData);  // deep copy, not aliased
        expectSameData(*snap.extraData, *out.extraData);
    });
}
