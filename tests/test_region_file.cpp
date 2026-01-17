#include <gtest/gtest.h>
#include "finevox/region_file.hpp"
#include "finevox/config.hpp"
#include <filesystem>
#include <cstdlib>

using namespace finevox;

// Test fixture that creates a temporary directory
class RegionFileTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        // Create unique temp directory
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_region";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        // Clean up temp directory
        std::filesystem::remove_all(tempDir);
    }
};

// ============================================================================
// RegionPos Tests
// ============================================================================

TEST(RegionPosTest, FromColumnPositive) {
    // Columns 0-31 are in region (0, 0)
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{0, 0}), (RegionPos{0, 0}));
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{31, 31}), (RegionPos{0, 0}));

    // Column 32 is in region (1, 0)
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{32, 0}), (RegionPos{1, 0}));
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{63, 31}), (RegionPos{1, 0}));
}

TEST(RegionPosTest, FromColumnNegative) {
    // Column -1 is in region (-1, 0)
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{-1, 0}), (RegionPos{-1, 0}));

    // Column -32 is in region (-1, 0)
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{-32, 0}), (RegionPos{-1, 0}));

    // Column -33 is in region (-2, 0)
    EXPECT_EQ(RegionPos::fromColumn(ColumnPos{-33, 0}), (RegionPos{-2, 0}));
}

TEST(RegionPosTest, ToLocalPositive) {
    auto [lx, lz] = RegionPos::toLocal(ColumnPos{0, 0});
    EXPECT_EQ(lx, 0);
    EXPECT_EQ(lz, 0);

    auto [lx2, lz2] = RegionPos::toLocal(ColumnPos{31, 31});
    EXPECT_EQ(lx2, 31);
    EXPECT_EQ(lz2, 31);

    auto [lx3, lz3] = RegionPos::toLocal(ColumnPos{32, 33});
    EXPECT_EQ(lx3, 0);
    EXPECT_EQ(lz3, 1);
}

TEST(RegionPosTest, ToLocalNegative) {
    auto [lx, lz] = RegionPos::toLocal(ColumnPos{-1, 0});
    EXPECT_EQ(lx, 31);  // -1 mod 32 = 31
    EXPECT_EQ(lz, 0);

    auto [lx2, lz2] = RegionPos::toLocal(ColumnPos{-32, -32});
    EXPECT_EQ(lx2, 0);
    EXPECT_EQ(lz2, 0);

    auto [lx3, lz3] = RegionPos::toLocal(ColumnPos{-33, -33});
    EXPECT_EQ(lx3, 31);
    EXPECT_EQ(lz3, 31);
}

// ============================================================================
// TocEntry Tests
// ============================================================================

TEST(TocEntryTest, RoundTrip) {
    TocEntry original;
    original.localX = 15;
    original.localZ = 20;
    original.offset = 123456789;
    original.size = 4096;
    original.timestamp = 9876543210;

    auto bytes = original.toBytes();
    EXPECT_EQ(bytes.size(), TocEntry::SERIALIZED_SIZE);

    auto restored = TocEntry::fromBytes(bytes.data(), bytes.size());
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->localX, original.localX);
    EXPECT_EQ(restored->localZ, original.localZ);
    EXPECT_EQ(restored->offset, original.offset);
    EXPECT_EQ(restored->size, original.size);
    EXPECT_EQ(restored->timestamp, original.timestamp);
}

TEST(TocEntryTest, InvalidData) {
    uint8_t tooShort[10] = {0};
    auto result = TocEntry::fromBytes(tooShort, sizeof(tooShort));
    EXPECT_FALSE(result.has_value());
}

// ============================================================================
// RegionFile Basic Tests
// ============================================================================

TEST_F(RegionFileTest, CreateNew) {
    RegionFile region(tempDir, RegionPos{0, 0});

    EXPECT_EQ(region.columnCount(), 0);
    EXPECT_EQ(region.position(), (RegionPos{0, 0}));

    // Files should exist
    EXPECT_TRUE(std::filesystem::exists(tempDir / "r.0.0.dat"));
    EXPECT_TRUE(std::filesystem::exists(tempDir / "r.0.0.toc"));
}

TEST_F(RegionFileTest, SaveAndLoadSingleColumn) {
    ChunkColumn original(ColumnPos{5, 10});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Add some blocks
    for (int y = 0; y < 16; ++y) {
        original.setBlock(0, y, 0, stone);
    }

    // Save
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        EXPECT_TRUE(region.saveColumn(original, ColumnPos{5, 10}));
        EXPECT_EQ(region.columnCount(), 1);
        EXPECT_TRUE(region.hasColumn(ColumnPos{5, 10}));
    }

    // Load in new region instance (simulates restart)
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        EXPECT_EQ(region.columnCount(), 1);
        EXPECT_TRUE(region.hasColumn(ColumnPos{5, 10}));

        auto loaded = region.loadColumn(ColumnPos{5, 10});
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(loaded->nonAirCount(), 16);

        for (int y = 0; y < 16; ++y) {
            EXPECT_EQ(loaded->getBlock(0, y, 0), stone);
        }
    }
}

TEST_F(RegionFileTest, SaveMultipleColumns) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    {
        RegionFile region(tempDir, RegionPos{0, 0});

        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                ChunkColumn col(ColumnPos{x, z});
                col.setBlock(0, 0, 0, (x + z) % 2 == 0 ? stone : dirt);
                EXPECT_TRUE(region.saveColumn(col, ColumnPos{x, z}));
            }
        }

        EXPECT_EQ(region.columnCount(), 25);
    }

    // Reload and verify
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        EXPECT_EQ(region.columnCount(), 25);

        for (int x = 0; x < 5; ++x) {
            for (int z = 0; z < 5; ++z) {
                auto loaded = region.loadColumn(ColumnPos{x, z});
                ASSERT_NE(loaded, nullptr);
                BlockTypeId expected = (x + z) % 2 == 0 ? stone : dirt;
                EXPECT_EQ(loaded->getBlock(0, 0, 0), expected)
                    << "Mismatch at column (" << x << ", " << z << ")";
            }
        }
    }
}

TEST_F(RegionFileTest, OverwriteColumn) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    {
        RegionFile region(tempDir, RegionPos{0, 0});

        // Save initial version
        ChunkColumn col1(ColumnPos{0, 0});
        col1.setBlock(0, 0, 0, stone);
        EXPECT_TRUE(region.saveColumn(col1, ColumnPos{0, 0}));

        // Overwrite with different content
        ChunkColumn col2(ColumnPos{0, 0});
        col2.setBlock(0, 0, 0, dirt);
        col2.setBlock(1, 1, 1, dirt);
        EXPECT_TRUE(region.saveColumn(col2, ColumnPos{0, 0}));

        EXPECT_EQ(region.columnCount(), 1);  // Still just one column
    }

    // Reload and verify we get the latest version
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        auto loaded = region.loadColumn(ColumnPos{0, 0});
        ASSERT_NE(loaded, nullptr);

        EXPECT_EQ(loaded->getBlock(0, 0, 0), dirt);
        EXPECT_EQ(loaded->getBlock(1, 1, 1), dirt);
        EXPECT_EQ(loaded->nonAirCount(), 2);
    }
}

TEST_F(RegionFileTest, NonexistentColumn) {
    RegionFile region(tempDir, RegionPos{0, 0});

    EXPECT_FALSE(region.hasColumn(ColumnPos{5, 5}));
    EXPECT_EQ(region.loadColumn(ColumnPos{5, 5}), nullptr);
}

TEST_F(RegionFileTest, WrongRegion) {
    RegionFile region(tempDir, RegionPos{0, 0});
    ChunkColumn col(ColumnPos{100, 100});

    // Column (100, 100) is in region (3, 3), not (0, 0)
    EXPECT_FALSE(region.saveColumn(col, ColumnPos{100, 100}));
    EXPECT_FALSE(region.hasColumn(ColumnPos{100, 100}));
}

TEST_F(RegionFileTest, NegativeCoordinates) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Region (-1, -1) contains columns -32..-1
    {
        RegionFile region(tempDir, RegionPos{-1, -1});

        ChunkColumn col(ColumnPos{-1, -1});
        col.setBlock(0, 0, 0, stone);
        EXPECT_TRUE(region.saveColumn(col, ColumnPos{-1, -1}));

        ChunkColumn col2(ColumnPos{-32, -32});
        col2.setBlock(0, 0, 0, stone);
        EXPECT_TRUE(region.saveColumn(col2, ColumnPos{-32, -32}));

        EXPECT_EQ(region.columnCount(), 2);
    }

    // Reload
    {
        RegionFile region(tempDir, RegionPos{-1, -1});
        EXPECT_EQ(region.columnCount(), 2);

        auto loaded1 = region.loadColumn(ColumnPos{-1, -1});
        ASSERT_NE(loaded1, nullptr);
        EXPECT_EQ(loaded1->getBlock(0, 0, 0), stone);

        auto loaded2 = region.loadColumn(ColumnPos{-32, -32});
        ASSERT_NE(loaded2, nullptr);
        EXPECT_EQ(loaded2->getBlock(0, 0, 0), stone);
    }
}

TEST_F(RegionFileTest, GetExistingColumns) {
    RegionFile region(tempDir, RegionPos{0, 0});

    ChunkColumn col1(ColumnPos{0, 0});
    ChunkColumn col2(ColumnPos{5, 10});
    ChunkColumn col3(ColumnPos{31, 31});

    region.saveColumn(col1, ColumnPos{0, 0});
    region.saveColumn(col2, ColumnPos{5, 10});
    region.saveColumn(col3, ColumnPos{31, 31});

    auto existing = region.getExistingColumns();
    EXPECT_EQ(existing.size(), 3);

    // Check all expected columns are present
    std::set<std::pair<int32_t, int32_t>> found;
    for (const auto& pos : existing) {
        found.insert({pos.x, pos.z});
    }

    EXPECT_TRUE(found.count({0, 0}));
    EXPECT_TRUE(found.count({5, 10}));
    EXPECT_TRUE(found.count({31, 31}));
}

TEST_F(RegionFileTest, CompactToc) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    {
        RegionFile region(tempDir, RegionPos{0, 0});

        // Create column
        ChunkColumn col(ColumnPos{0, 0});
        col.setBlock(0, 0, 0, stone);
        region.saveColumn(col, ColumnPos{0, 0});

        // Overwrite multiple times (creates obsolete ToC entries)
        for (int i = 0; i < 10; ++i) {
            ChunkColumn newCol(ColumnPos{0, 0});
            newCol.setBlock(0, 0, 0, stone);
            newCol.setBlock(i, 0, 0, stone);
            region.saveColumn(newCol, ColumnPos{0, 0});
        }

        // Compact
        region.compactToc();
        EXPECT_EQ(region.columnCount(), 1);
    }

    // Reload and verify data is still correct
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        EXPECT_EQ(region.columnCount(), 1);

        auto loaded = region.loadColumn(ColumnPos{0, 0});
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(loaded->getBlock(0, 0, 0), stone);
        EXPECT_EQ(loaded->getBlock(9, 0, 0), stone);  // Last write
    }
}

// ============================================================================
// Large Data Tests
// ============================================================================

TEST_F(RegionFileTest, LargeColumn) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    {
        RegionFile region(tempDir, RegionPos{0, 0});

        // Create a column with lots of data across many subchunks
        ChunkColumn col(ColumnPos{0, 0});
        for (int y = -64; y < 128; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    col.setBlock(x, y, z, ((x + y + z) % 2 == 0) ? stone : dirt);
                }
            }
        }

        EXPECT_TRUE(region.saveColumn(col, ColumnPos{0, 0}));
    }

    // Reload and verify
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        auto loaded = region.loadColumn(ColumnPos{0, 0});
        ASSERT_NE(loaded, nullptr);

        for (int y = -64; y < 128; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    BlockTypeId expected = ((x + y + z) % 2 == 0) ? stone : dirt;
                    EXPECT_EQ(loaded->getBlock(x, y, z), expected)
                        << "Mismatch at (" << x << ", " << y << ", " << z << ")";
                }
            }
        }
    }
}

TEST_F(RegionFileTest, ManyColumns) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Fill entire region (32x32 = 1024 columns)
    {
        RegionFile region(tempDir, RegionPos{0, 0});

        for (int x = 0; x < REGION_SIZE; ++x) {
            for (int z = 0; z < REGION_SIZE; ++z) {
                ChunkColumn col(ColumnPos{x, z});
                col.setBlock(0, 0, 0, stone);
                col.setBlock(x % 16, 0, z % 16, stone);
                EXPECT_TRUE(region.saveColumn(col, ColumnPos{x, z}));
            }
        }

        EXPECT_EQ(region.columnCount(), COLUMNS_PER_REGION);
    }

    // Reload and sample verify
    {
        RegionFile region(tempDir, RegionPos{0, 0});
        EXPECT_EQ(region.columnCount(), COLUMNS_PER_REGION);

        // Check a few random columns
        auto loaded1 = region.loadColumn(ColumnPos{0, 0});
        ASSERT_NE(loaded1, nullptr);
        EXPECT_EQ(loaded1->getBlock(0, 0, 0), stone);

        auto loaded2 = region.loadColumn(ColumnPos{15, 15});
        ASSERT_NE(loaded2, nullptr);
        EXPECT_EQ(loaded2->getBlock(15, 0, 15), stone);

        auto loaded3 = region.loadColumn(ColumnPos{31, 31});
        ASSERT_NE(loaded3, nullptr);
        EXPECT_EQ(loaded3->getBlock(15, 0, 15), stone);
    }
}

// ============================================================================
// LZ4 Compression Tests
// ============================================================================

TEST_F(RegionFileTest, CompressionEnabledByDefault) {
    // Without ConfigManager initialized, compression should default to enabled
    RegionFile region(tempDir, RegionPos{0, 0});

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Create a column with repetitive data (compresses well)
    ChunkColumn col(ColumnPos{0, 0});
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(x, y, z, stone);
            }
        }
    }

    EXPECT_TRUE(region.saveColumn(col, ColumnPos{0, 0}));

    // Load it back - should decompress automatically
    auto loaded = region.loadColumn(ColumnPos{0, 0});
    ASSERT_NE(loaded, nullptr);

    // Verify data integrity
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                EXPECT_EQ(loaded->getBlock(x, y, z), stone);
            }
        }
    }
}

TEST_F(RegionFileTest, CompressionCanBeDisabled) {
    // Initialize ConfigManager with compression disabled
    auto configPath = tempDir / "config.cbor";
    ConfigManager::instance().init(configPath);
    ConfigManager::instance().setCompressionEnabled(false);

    RegionFile region(tempDir, RegionPos{0, 0});

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    ChunkColumn col(ColumnPos{0, 0});
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(x, y, z, stone);
            }
        }
    }

    EXPECT_TRUE(region.saveColumn(col, ColumnPos{0, 0}));

    // Load it back
    auto loaded = region.loadColumn(ColumnPos{0, 0});
    ASSERT_NE(loaded, nullptr);

    // Verify data integrity
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                EXPECT_EQ(loaded->getBlock(x, y, z), stone);
            }
        }
    }

    ConfigManager::instance().reset();
}

TEST_F(RegionFileTest, MixedCompressionRoundTrip) {
    // Save some columns with compression, some without, then load all
    auto configPath = tempDir / "config.cbor";
    ConfigManager::instance().init(configPath);

    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    // Save with compression enabled
    ConfigManager::instance().setCompressionEnabled(true);
    {
        RegionFile region(tempDir, RegionPos{0, 0});

        ChunkColumn col1(ColumnPos{0, 0});
        col1.setBlock(0, 0, 0, stone);
        EXPECT_TRUE(region.saveColumn(col1, ColumnPos{0, 0}));

        ChunkColumn col2(ColumnPos{1, 0});
        col2.setBlock(0, 0, 0, dirt);
        EXPECT_TRUE(region.saveColumn(col2, ColumnPos{1, 0}));
    }

    // Save more columns with compression disabled
    ConfigManager::instance().setCompressionEnabled(false);
    {
        RegionFile region(tempDir, RegionPos{0, 0});

        ChunkColumn col3(ColumnPos{2, 0});
        col3.setBlock(0, 0, 0, stone);
        EXPECT_TRUE(region.saveColumn(col3, ColumnPos{2, 0}));
    }

    // Load all columns (should handle mixed compression transparently)
    {
        RegionFile region(tempDir, RegionPos{0, 0});

        auto loaded1 = region.loadColumn(ColumnPos{0, 0});
        ASSERT_NE(loaded1, nullptr);
        EXPECT_EQ(loaded1->getBlock(0, 0, 0), stone);

        auto loaded2 = region.loadColumn(ColumnPos{1, 0});
        ASSERT_NE(loaded2, nullptr);
        EXPECT_EQ(loaded2->getBlock(0, 0, 0), dirt);

        auto loaded3 = region.loadColumn(ColumnPos{2, 0});
        ASSERT_NE(loaded3, nullptr);
        EXPECT_EQ(loaded3->getBlock(0, 0, 0), stone);
    }

    ConfigManager::instance().reset();
}

TEST_F(RegionFileTest, CompressionReducesFileSize) {
    auto configPath = tempDir / "config.cbor";
    ConfigManager::instance().init(configPath);

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Create highly repetitive data
    ChunkColumn col(ColumnPos{0, 0});
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(x, y, z, stone);
            }
        }
    }

    // Save with compression
    ConfigManager::instance().setCompressionEnabled(true);
    {
        RegionFile region(tempDir / "compressed", RegionPos{0, 0});
        EXPECT_TRUE(region.saveColumn(col, ColumnPos{0, 0}));
    }

    // Save without compression
    ConfigManager::instance().setCompressionEnabled(false);
    {
        RegionFile region(tempDir / "uncompressed", RegionPos{0, 0});
        EXPECT_TRUE(region.saveColumn(col, ColumnPos{0, 0}));
    }

    // Compare file sizes
    auto compressedSize = std::filesystem::file_size(tempDir / "compressed" / "r.0.0.dat");
    auto uncompressedSize = std::filesystem::file_size(tempDir / "uncompressed" / "r.0.0.dat");

    // Compressed should be significantly smaller for repetitive data
    EXPECT_LT(compressedSize, uncompressedSize);

    ConfigManager::instance().reset();
}
