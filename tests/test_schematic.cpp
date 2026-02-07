/**
 * @file test_schematic.cpp
 * @brief Unit tests for schematic system
 *
 * Tests: Schematic creation, access, iteration, transforms,
 * CBOR serialization round-trip, file I/O, ClipboardManager.
 */

#include "finevox/worldgen/schematic.hpp"
#include "finevox/worldgen/schematic_io.hpp"
#include "finevox/worldgen/clipboard_manager.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace finevox;
using namespace finevox::worldgen;

// ============================================================================
// BlockSnapshot tests
// ============================================================================

TEST(BlockSnapshotTest, DefaultIsAir) {
    BlockSnapshot snap;
    EXPECT_TRUE(snap.isAir());
    EXPECT_FALSE(snap.hasMetadata());
}

TEST(BlockSnapshotTest, ExplicitAir) {
    BlockSnapshot snap("air");
    EXPECT_TRUE(snap.isAir());
}

TEST(BlockSnapshotTest, NamedBlock) {
    BlockSnapshot snap("stone");
    EXPECT_FALSE(snap.isAir());
    EXPECT_EQ(snap.typeName, "stone");
    EXPECT_FALSE(snap.hasMetadata());
}

TEST(BlockSnapshotTest, HasMetadataWithRotation) {
    BlockSnapshot snap("stairs");
    snap.rotation = Rotation::byIndex(1);
    EXPECT_TRUE(snap.hasMetadata());
}

TEST(BlockSnapshotTest, HasMetadataWithDisplacement) {
    BlockSnapshot snap("slab");
    snap.displacement = glm::vec3(0.0f, 0.5f, 0.0f);
    EXPECT_TRUE(snap.hasMetadata());
}

TEST(BlockSnapshotTest, HasMetadataWithExtraData) {
    BlockSnapshot snap("chest");
    snap.extraData = DataContainer();
    EXPECT_TRUE(snap.hasMetadata());
}

// ============================================================================
// Schematic basic tests
// ============================================================================

TEST(SchematicTest, Construction) {
    Schematic s(4, 8, 4);
    EXPECT_EQ(s.sizeX(), 4);
    EXPECT_EQ(s.sizeY(), 8);
    EXPECT_EQ(s.sizeZ(), 4);
    EXPECT_EQ(s.volume(), 128);
}

TEST(SchematicTest, InvalidDimensions) {
    EXPECT_THROW(Schematic(0, 1, 1), std::invalid_argument);
    EXPECT_THROW(Schematic(1, -1, 1), std::invalid_argument);
}

TEST(SchematicTest, DefaultBlocksAreAir) {
    Schematic s(2, 2, 2);
    EXPECT_TRUE(s.at(0, 0, 0).isAir());
    EXPECT_TRUE(s.at(1, 1, 1).isAir());
}

TEST(SchematicTest, SetAndGetBlock) {
    Schematic s(3, 3, 3);
    s.at(1, 2, 0).typeName = "stone";
    EXPECT_EQ(s.at(1, 2, 0).typeName, "stone");
    EXPECT_FALSE(s.at(1, 2, 0).isAir());
    EXPECT_TRUE(s.at(0, 0, 0).isAir());
}

TEST(SchematicTest, BoundsChecking) {
    Schematic s(3, 3, 3);
    EXPECT_TRUE(s.contains(0, 0, 0));
    EXPECT_TRUE(s.contains(2, 2, 2));
    EXPECT_FALSE(s.contains(-1, 0, 0));
    EXPECT_FALSE(s.contains(3, 0, 0));
    EXPECT_FALSE(s.contains(0, 3, 0));
}

TEST(SchematicTest, OutOfBoundsThrows) {
    Schematic s(3, 3, 3);
    EXPECT_THROW(s.at(3, 0, 0), std::out_of_range);
    EXPECT_THROW(s.at(-1, 0, 0), std::out_of_range);
}

TEST(SchematicTest, GlmVecAccess) {
    Schematic s(3, 3, 3);
    s.at(glm::ivec3(1, 2, 0)).typeName = "dirt";
    EXPECT_EQ(s.at(glm::ivec3(1, 2, 0)).typeName, "dirt");
}

// ============================================================================
// Iteration and statistics
// ============================================================================

TEST(SchematicTest, NonAirBlockCount) {
    Schematic s(3, 3, 3);
    EXPECT_EQ(s.nonAirBlockCount(), 0u);

    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 1, 1).typeName = "dirt";
    s.at(2, 2, 2).typeName = "stone";
    EXPECT_EQ(s.nonAirBlockCount(), 3u);
}

TEST(SchematicTest, UniqueBlockTypes) {
    Schematic s(3, 3, 3);
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 1, 1).typeName = "dirt";
    s.at(2, 2, 2).typeName = "stone";

    auto types = s.uniqueBlockTypes();
    EXPECT_EQ(types.size(), 2u);
    EXPECT_TRUE(types.count("stone"));
    EXPECT_TRUE(types.count("dirt"));
}

TEST(SchematicTest, ForEachBlock) {
    Schematic s(3, 3, 3);
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 1, 1).typeName = "dirt";

    int count = 0;
    s.forEachBlock([&](glm::ivec3, const BlockSnapshot&) {
        ++count;
    });
    EXPECT_EQ(count, 2);
}

TEST(SchematicTest, Metadata) {
    Schematic s(1, 1, 1);
    s.setName("Test");
    s.setAuthor("Author");
    EXPECT_EQ(s.name(), "Test");
    EXPECT_EQ(s.author(), "Author");
}

// ============================================================================
// Transformation tests
// ============================================================================

TEST(SchematicTransformTest, MirrorX) {
    Schematic s(3, 1, 1);
    s.at(0, 0, 0).typeName = "stone";
    s.at(2, 0, 0).typeName = "dirt";

    auto mirrored = mirrorSchematic(s, Axis::X);
    EXPECT_EQ(mirrored.at(2, 0, 0).typeName, "stone");
    EXPECT_EQ(mirrored.at(0, 0, 0).typeName, "dirt");
}

TEST(SchematicTransformTest, MirrorY) {
    Schematic s(1, 3, 1);
    s.at(0, 0, 0).typeName = "stone";
    s.at(0, 2, 0).typeName = "dirt";

    auto mirrored = mirrorSchematic(s, Axis::Y);
    EXPECT_EQ(mirrored.at(0, 2, 0).typeName, "stone");
    EXPECT_EQ(mirrored.at(0, 0, 0).typeName, "dirt");
}

TEST(SchematicTransformTest, MirrorZ) {
    Schematic s(1, 1, 3);
    s.at(0, 0, 0).typeName = "stone";
    s.at(0, 0, 2).typeName = "dirt";

    auto mirrored = mirrorSchematic(s, Axis::Z);
    EXPECT_EQ(mirrored.at(0, 0, 2).typeName, "stone");
    EXPECT_EQ(mirrored.at(0, 0, 0).typeName, "dirt");
}

TEST(SchematicTransformTest, CropRemovesEmptySpace) {
    Schematic s(5, 5, 5);
    s.at(2, 2, 2).typeName = "stone";
    s.at(3, 3, 3).typeName = "dirt";

    auto cropped = cropSchematic(s);
    EXPECT_EQ(cropped.sizeX(), 2);
    EXPECT_EQ(cropped.sizeY(), 2);
    EXPECT_EQ(cropped.sizeZ(), 2);
    EXPECT_EQ(cropped.at(0, 0, 0).typeName, "stone");
    EXPECT_EQ(cropped.at(1, 1, 1).typeName, "dirt");
}

TEST(SchematicTransformTest, CropEmptySchematic) {
    Schematic s(3, 3, 3);
    auto cropped = cropSchematic(s);
    EXPECT_EQ(cropped.sizeX(), 1);
    EXPECT_EQ(cropped.sizeY(), 1);
    EXPECT_EQ(cropped.sizeZ(), 1);
}

TEST(SchematicTransformTest, ReplaceBlocks) {
    Schematic s(2, 2, 2);
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 0, 0).typeName = "dirt";
    s.at(0, 1, 0).typeName = "stone";

    auto replaced = replaceBlocks(s, {{"stone", "cobblestone"}});
    EXPECT_EQ(replaced.at(0, 0, 0).typeName, "cobblestone");
    EXPECT_EQ(replaced.at(1, 0, 0).typeName, "dirt");
    EXPECT_EQ(replaced.at(0, 1, 0).typeName, "cobblestone");
}

TEST(SchematicTransformTest, RotateIdentity) {
    Schematic s(2, 3, 4);
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 2, 3).typeName = "dirt";

    auto rotated = rotateSchematic(s, Rotation::IDENTITY);
    EXPECT_EQ(rotated.sizeX(), 2);
    EXPECT_EQ(rotated.sizeY(), 3);
    EXPECT_EQ(rotated.sizeZ(), 4);
    EXPECT_EQ(rotated.at(0, 0, 0).typeName, "stone");
    EXPECT_EQ(rotated.at(1, 2, 3).typeName, "dirt");
}

// ============================================================================
// Serialization round-trip tests
// ============================================================================

TEST(SchematicSerializationTest, EmptySchematic) {
    Schematic s(2, 2, 2);
    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.sizeX(), 2);
    EXPECT_EQ(loaded.sizeY(), 2);
    EXPECT_EQ(loaded.sizeZ(), 2);
    EXPECT_EQ(loaded.nonAirBlockCount(), 0u);
}

TEST(SchematicSerializationTest, SimpleBlocks) {
    Schematic s(3, 3, 3);
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 1, 1).typeName = "dirt";
    s.at(2, 2, 2).typeName = "stone";

    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.nonAirBlockCount(), 3u);
    EXPECT_EQ(loaded.at(0, 0, 0).typeName, "stone");
    EXPECT_EQ(loaded.at(1, 1, 1).typeName, "dirt");
    EXPECT_EQ(loaded.at(2, 2, 2).typeName, "stone");
    EXPECT_TRUE(loaded.at(0, 1, 0).isAir());
}

TEST(SchematicSerializationTest, PreservesMetadata) {
    Schematic s(2, 2, 2);
    s.setName("TestName");
    s.setAuthor("TestAuthor");
    s.at(0, 0, 0).typeName = "stone";

    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.name(), "TestName");
    EXPECT_EQ(loaded.author(), "TestAuthor");
}

TEST(SchematicSerializationTest, PreservesRotation) {
    Schematic s(1, 1, 1);
    s.at(0, 0, 0).typeName = "stairs";
    s.at(0, 0, 0).rotation = Rotation::byIndex(5);

    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.at(0, 0, 0).typeName, "stairs");
    EXPECT_EQ(loaded.at(0, 0, 0).rotation.index(), 5);
}

TEST(SchematicSerializationTest, PreservesDisplacement) {
    Schematic s(1, 1, 1);
    s.at(0, 0, 0).typeName = "slab";
    s.at(0, 0, 0).displacement = glm::vec3(0.0f, 0.5f, 0.0f);

    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.at(0, 0, 0).typeName, "slab");
    EXPECT_FLOAT_EQ(loaded.at(0, 0, 0).displacement.y, 0.5f);
}

TEST(SchematicSerializationTest, LargerSchematic) {
    Schematic s(16, 16, 16);
    int count = 0;
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            s.at(x, 0, z).typeName = "stone";
            ++count;
        }
    }

    auto bytes = serializeSchematic(s);
    auto loaded = deserializeSchematic(bytes);

    EXPECT_EQ(loaded.nonAirBlockCount(), static_cast<size_t>(count));
    EXPECT_EQ(loaded.at(0, 0, 0).typeName, "stone");
    EXPECT_EQ(loaded.at(15, 0, 15).typeName, "stone");
    EXPECT_TRUE(loaded.at(0, 1, 0).isAir());
}

// ============================================================================
// File I/O tests
// ============================================================================

TEST(SchematicFileTest, SaveAndLoad) {
    auto tempDir = std::filesystem::temp_directory_path();
    auto testFile = tempDir / "test_schematic.vxsc";

    Schematic s(4, 4, 4);
    s.setName("FileTest");
    s.at(0, 0, 0).typeName = "stone";
    s.at(1, 1, 1).typeName = "dirt";
    s.at(2, 2, 2).typeName = "cobblestone";
    s.at(2, 2, 2).rotation = Rotation::byIndex(3);

    saveSchematic(s, testFile);

    auto loaded = loadSchematic(testFile);
    EXPECT_EQ(loaded.name(), "FileTest");
    EXPECT_EQ(loaded.sizeX(), 4);
    EXPECT_EQ(loaded.nonAirBlockCount(), 3u);
    EXPECT_EQ(loaded.at(0, 0, 0).typeName, "stone");
    EXPECT_EQ(loaded.at(1, 1, 1).typeName, "dirt");
    EXPECT_EQ(loaded.at(2, 2, 2).typeName, "cobblestone");
    EXPECT_EQ(loaded.at(2, 2, 2).rotation.index(), 3);

    // Cleanup
    std::filesystem::remove(testFile);
}

TEST(SchematicFileTest, InvalidMagicThrows) {
    auto tempDir = std::filesystem::temp_directory_path();
    auto testFile = tempDir / "bad_schematic.vxsc";

    // Write garbage
    std::ofstream f(testFile, std::ios::binary);
    uint32_t bad = 0x12345678;
    f.write(reinterpret_cast<const char*>(&bad), 4);
    f.write(reinterpret_cast<const char*>(&bad), 4);
    f.write(reinterpret_cast<const char*>(&bad), 4);
    f.close();

    EXPECT_THROW(loadSchematic(testFile), std::runtime_error);

    std::filesystem::remove(testFile);
}

// ============================================================================
// ClipboardManager tests
// ============================================================================

TEST(ClipboardManagerTest, InitiallyEmpty) {
    auto& mgr = ClipboardManager::instance();
    mgr.clearAll();
    EXPECT_EQ(mgr.clipboard(), nullptr);
    EXPECT_EQ(mgr.historySize(), 0u);
}

TEST(ClipboardManagerTest, SetAndGetClipboard) {
    auto& mgr = ClipboardManager::instance();
    mgr.clearAll();

    Schematic s(2, 2, 2);
    s.at(0, 0, 0).typeName = "stone";
    mgr.setClipboard(std::move(s));

    const Schematic* clip = mgr.clipboard();
    ASSERT_NE(clip, nullptr);
    EXPECT_EQ(clip->sizeX(), 2);
    EXPECT_EQ(clip->at(0, 0, 0).typeName, "stone");
}

TEST(ClipboardManagerTest, ClearClipboard) {
    auto& mgr = ClipboardManager::instance();
    mgr.clearAll();

    Schematic s(1, 1, 1);
    mgr.setClipboard(std::move(s));
    EXPECT_NE(mgr.clipboard(), nullptr);

    mgr.clearClipboard();
    EXPECT_EQ(mgr.clipboard(), nullptr);
}

TEST(ClipboardManagerTest, NamedClipboards) {
    auto& mgr = ClipboardManager::instance();
    mgr.clearAll();

    Schematic s1(1, 1, 1);
    s1.at(0, 0, 0).typeName = "stone";
    mgr.setNamed("test1", std::move(s1));

    Schematic s2(2, 2, 2);
    s2.at(0, 0, 0).typeName = "dirt";
    mgr.setNamed("test2", std::move(s2));

    EXPECT_NE(mgr.getNamed("test1"), nullptr);
    EXPECT_EQ(mgr.getNamed("test1")->at(0, 0, 0).typeName, "stone");
    EXPECT_NE(mgr.getNamed("test2"), nullptr);
    EXPECT_EQ(mgr.getNamed("test2")->sizeX(), 2);
    EXPECT_EQ(mgr.getNamed("nonexistent"), nullptr);
}

TEST(ClipboardManagerTest, History) {
    auto& mgr = ClipboardManager::instance();
    mgr.clearAll();
    mgr.setMaxHistorySize(3);

    for (int i = 0; i < 5; ++i) {
        Schematic s(1, 1, 1);
        s.at(0, 0, 0).typeName = "block" + std::to_string(i);
        mgr.pushHistory(std::move(s));
    }

    // Should have max 3 entries (newest first)
    EXPECT_EQ(mgr.historySize(), 3u);
    EXPECT_EQ(mgr.historyAt(0)->at(0, 0, 0).typeName, "block4");
    EXPECT_EQ(mgr.historyAt(1)->at(0, 0, 0).typeName, "block3");
    EXPECT_EQ(mgr.historyAt(2)->at(0, 0, 0).typeName, "block2");
    EXPECT_EQ(mgr.historyAt(3), nullptr);  // Out of bounds
}
