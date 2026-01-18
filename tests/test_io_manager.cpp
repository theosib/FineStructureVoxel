#include <gtest/gtest.h>
#include "finevox/io_manager.hpp"
#include <filesystem>
#include <atomic>
#include <latch>
#include <thread>

using namespace finevox;

class IOManagerTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_io";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir);
    }
};

TEST_F(IOManagerTest, CreateAndStart) {
    IOManager io(tempDir);
    io.start();
    EXPECT_EQ(io.pendingLoadCount(), 0);
    EXPECT_EQ(io.pendingSaveCount(), 0);
    io.stop();
}

TEST_F(IOManagerTest, SaveAndLoad) {
    IOManager io(tempDir);
    io.start();

    // Create a column
    ChunkColumn col(ColumnPos{5, 10});
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    col.setBlock(0, 0, 0, stone);
    col.setBlock(1, 1, 1, stone);

    // Save it
    std::atomic<bool> saveComplete{false};
    io.queueSave(ColumnPos{5, 10}, col, [&](ColumnPos pos, bool success) {
        EXPECT_EQ(pos, (ColumnPos{5, 10}));
        EXPECT_TRUE(success);
        saveComplete = true;
    });

    // Wait for save
    io.flush();
    EXPECT_TRUE(saveComplete);

    // Load it back
    std::atomic<bool> loadComplete{false};
    std::unique_ptr<ChunkColumn> loadedCol;

    io.requestLoad(ColumnPos{5, 10}, [&](ColumnPos pos, std::unique_ptr<ChunkColumn> col) {
        EXPECT_EQ(pos, (ColumnPos{5, 10}));
        loadedCol = std::move(col);
        loadComplete = true;
    });

    // Wait for load
    while (!loadComplete) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_NE(loadedCol, nullptr);
    EXPECT_EQ(loadedCol->getBlock(0, 0, 0), stone);
    EXPECT_EQ(loadedCol->getBlock(1, 1, 1), stone);

    io.stop();
}

TEST_F(IOManagerTest, LoadNonexistent) {
    IOManager io(tempDir);
    io.start();

    std::atomic<bool> loadComplete{false};
    std::unique_ptr<ChunkColumn> loadedCol;

    io.requestLoad(ColumnPos{999, 999}, [&](ColumnPos pos, std::unique_ptr<ChunkColumn> col) {
        EXPECT_EQ(pos, (ColumnPos{999, 999}));
        loadedCol = std::move(col);
        loadComplete = true;
    });

    while (!loadComplete) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(loadedCol, nullptr);

    io.stop();
}

TEST_F(IOManagerTest, MultipleSaves) {
    IOManager io(tempDir);
    io.start();

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Save multiple columns
    for (int i = 0; i < 10; ++i) {
        ChunkColumn col(ColumnPos{i, i});
        col.setBlock(0, 0, 0, stone);
        io.queueSave(ColumnPos{i, i}, col);
    }

    io.flush();

    // Verify all saved
    std::atomic<int> loadCount{0};

    for (int i = 0; i < 10; ++i) {
        io.requestLoad(ColumnPos{i, i}, [&, i](ColumnPos pos, std::unique_ptr<ChunkColumn> col) {
            EXPECT_EQ(pos, (ColumnPos{i, i}));
            EXPECT_NE(col, nullptr);
            if (col) {
                EXPECT_EQ(col->getBlock(0, 0, 0), stone);
            }
            ++loadCount;
        });
    }

    // Wait for all loads
    while (loadCount < 10) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    io.stop();
}

TEST_F(IOManagerTest, MultipleRegions) {
    IOManager io(tempDir);
    io.start();

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Save columns in different regions
    std::vector<ColumnPos> positions = {
        {0, 0},     // Region (0, 0)
        {32, 0},    // Region (1, 0)
        {0, 32},    // Region (0, 1)
        {-1, 0},    // Region (-1, 0)
        {-33, -33}, // Region (-2, -2)
    };

    for (const auto& pos : positions) {
        ChunkColumn col(pos);
        col.setBlock(0, 0, 0, stone);
        io.queueSave(pos, col);
    }

    io.flush();

    // Should have opened multiple region files
    EXPECT_GT(io.regionFileCount(), 1);

    // Verify all can be loaded
    std::atomic<int> loadCount{0};

    for (const auto& pos : positions) {
        io.requestLoad(pos, [&](ColumnPos, std::unique_ptr<ChunkColumn> col) {
            EXPECT_NE(col, nullptr);
            ++loadCount;
        });
    }

    while (loadCount < static_cast<int>(positions.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    io.stop();
}

TEST_F(IOManagerTest, OverwriteColumn) {
    IOManager io(tempDir);
    io.start();

    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");

    // Save initial version
    {
        ChunkColumn col(ColumnPos{0, 0});
        col.setBlock(0, 0, 0, stone);
        io.queueSave(ColumnPos{0, 0}, col);
    }
    io.flush();

    // Overwrite with different data
    {
        ChunkColumn col(ColumnPos{0, 0});
        col.setBlock(0, 0, 0, dirt);
        col.setBlock(1, 1, 1, dirt);
        io.queueSave(ColumnPos{0, 0}, col);
    }
    io.flush();

    // Load and verify we get the newer version
    std::atomic<bool> loadComplete{false};
    std::unique_ptr<ChunkColumn> loadedCol;

    io.requestLoad(ColumnPos{0, 0}, [&](ColumnPos, std::unique_ptr<ChunkColumn> col) {
        loadedCol = std::move(col);
        loadComplete = true;
    });

    while (!loadComplete) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ASSERT_NE(loadedCol, nullptr);
    EXPECT_EQ(loadedCol->getBlock(0, 0, 0), dirt);
    EXPECT_EQ(loadedCol->getBlock(1, 1, 1), dirt);
    EXPECT_EQ(loadedCol->nonAirCount(), 2);

    io.stop();
}

TEST_F(IOManagerTest, ConcurrentOperations) {
    IOManager io(tempDir);
    io.start();

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    std::atomic<int> saveCount{0};
    std::atomic<int> loadCount{0};
    const int numOps = 50;

    // Start saves from multiple threads
    std::thread saver1([&] {
        for (int i = 0; i < numOps / 2; ++i) {
            ChunkColumn col(ColumnPos{i, 0});
            col.setBlock(0, 0, 0, stone);
            io.queueSave(ColumnPos{i, 0}, col, [&](ColumnPos, bool success) {
                EXPECT_TRUE(success);
                ++saveCount;
            });
        }
    });

    std::thread saver2([&] {
        for (int i = numOps / 2; i < numOps; ++i) {
            ChunkColumn col(ColumnPos{i, 0});
            col.setBlock(0, 0, 0, stone);
            io.queueSave(ColumnPos{i, 0}, col, [&](ColumnPos, bool success) {
                EXPECT_TRUE(success);
                ++saveCount;
            });
        }
    });

    saver1.join();
    saver2.join();

    // Wait for all save callbacks to complete
    while (saveCount < numOps) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(saveCount, numOps);

    // Load them all back
    for (int i = 0; i < numOps; ++i) {
        io.requestLoad(ColumnPos{i, 0}, [&](ColumnPos, std::unique_ptr<ChunkColumn> col) {
            EXPECT_NE(col, nullptr);
            ++loadCount;
        });
    }

    while (loadCount < numOps) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    io.stop();
}

TEST_F(IOManagerTest, RegionEviction) {
    IOManager io(tempDir);
    io.setMaxOpenRegions(2);
    io.start();

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Save to many different regions
    for (int i = 0; i < 10; ++i) {
        ColumnPos pos{i * 32, 0};  // Each in a different region
        ChunkColumn col(pos);
        col.setBlock(0, 0, 0, stone);
        io.queueSave(pos, col);
    }

    io.flush();

    // Should only keep 2 regions open
    EXPECT_LE(io.regionFileCount(), 2);

    io.stop();
}

// ============================================================================
// Round-trip test: create world -> save -> load -> verify identical
// ============================================================================

TEST_F(IOManagerTest, RoundTripSaveLoad) {
    IOManager io(tempDir);
    io.start();

    // Create various block types
    BlockTypeId stone = BlockTypeId::fromName("test:stone");
    BlockTypeId dirt = BlockTypeId::fromName("test:dirt");
    BlockTypeId grass = BlockTypeId::fromName("test:grass");
    BlockTypeId water = BlockTypeId::fromName("test:water");
    BlockTypeId ore = BlockTypeId::fromName("test:diamond_ore");

    // Store original block data for verification
    // Map: ColumnPos -> Map: local BlockPos -> BlockTypeId
    struct ColumnData {
        ColumnPos pos;
        std::unordered_map<uint64_t, BlockTypeId> blocks;  // packed local pos -> type
    };
    std::vector<ColumnData> originalData;

    // Create multiple columns with various patterns
    std::vector<ColumnPos> positions = {
        {0, 0},     // Origin
        {1, 0},     // Adjacent to origin
        {0, 1},     // Adjacent to origin
        {-1, -1},   // Negative coordinates
        {32, 32},   // Different region
        {-32, 0},   // Negative region
    };

    for (const auto& colPos : positions) {
        ChunkColumn col(colPos);
        ColumnData data;
        data.pos = colPos;

        // Fill base layer with stone
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < 5; ++y) {
                    col.setBlock(x, y, z, stone);
                    uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                     (static_cast<uint64_t>(y) << 16) |
                                     static_cast<uint64_t>(z);
                    data.blocks[packed] = stone;
                }
            }
        }

        // Add dirt layer
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(x, 5, z, dirt);
                uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                 (static_cast<uint64_t>(5) << 16) |
                                 static_cast<uint64_t>(z);
                data.blocks[packed] = dirt;
            }
        }

        // Add grass on top
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                col.setBlock(x, 6, z, grass);
                uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                 (static_cast<uint64_t>(6) << 16) |
                                 static_cast<uint64_t>(z);
                data.blocks[packed] = grass;
            }
        }

        // Scatter some ore randomly
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    // Deterministic pattern based on position
                    if ((x + y + z + colPos.x + colPos.z) % 17 == 0) {
                        col.setBlock(x, y, z, ore);
                        uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                         (static_cast<uint64_t>(y) << 16) |
                                         static_cast<uint64_t>(z);
                        data.blocks[packed] = ore;
                    }
                }
            }
        }

        // Add water pool at surface
        for (int x = 5; x < 10; ++x) {
            for (int z = 5; z < 10; ++z) {
                col.setBlock(x, 6, z, water);  // Replace grass with water
                uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                 (static_cast<uint64_t>(6) << 16) |
                                 static_cast<uint64_t>(z);
                data.blocks[packed] = water;
            }
        }

        // Add tall structure in one column
        if (colPos == ColumnPos{0, 0}) {
            for (int y = 0; y < 100; ++y) {
                col.setBlock(8, y, 8, stone);
                uint64_t packed = (static_cast<uint64_t>(8) << 32) |
                                 (static_cast<uint64_t>(y) << 16) |
                                 static_cast<uint64_t>(8);
                data.blocks[packed] = stone;
            }
        }

        originalData.push_back(std::move(data));

        // Queue save
        io.queueSave(colPos, col);
    }

    // Wait for all saves to complete
    io.flush();

    // Stop and restart IO manager to ensure data is persisted
    io.stop();

    // Recreate IOManager (simulates fresh load after program restart)
    IOManager io2(tempDir);
    io2.start();

    // Load all columns and verify
    std::atomic<int> verifiedCount{0};
    std::atomic<int> failureCount{0};

    for (const auto& data : originalData) {
        io2.requestLoad(data.pos, [&data, &verifiedCount, &failureCount](
            ColumnPos pos, std::unique_ptr<ChunkColumn> col) {

            if (!col) {
                ++failureCount;
                ADD_FAILURE() << "Failed to load column at (" << pos.x << ", " << pos.z << ")";
                ++verifiedCount;
                return;
            }

            // Verify position
            EXPECT_EQ(col->position(), data.pos);

            // Verify all blocks we set
            for (const auto& [packed, expectedType] : data.blocks) {
                int x = static_cast<int>((packed >> 32) & 0xFFFF);
                int y = static_cast<int>((packed >> 16) & 0xFFFF);
                int z = static_cast<int>(packed & 0xFFFF);

                BlockTypeId actual = col->getBlock(x, y, z);
                if (actual != expectedType) {
                    ++failureCount;
                    ADD_FAILURE() << "Block mismatch at (" << x << ", " << y << ", " << z
                                  << ") in column (" << pos.x << ", " << pos.z << ")"
                                  << " - expected " << expectedType.name()
                                  << " but got " << actual.name();
                }
            }

            // Verify blocks we didn't set are air
            // Check a sampling of positions
            for (int x = 0; x < 16; x += 4) {
                for (int z = 0; z < 16; z += 4) {
                    for (int y = 50; y < 60; ++y) {  // Above our structures (except tower)
                        if (pos == ColumnPos{0, 0} && x == 8 && z == 8 && y < 100) {
                            // This is part of the tower
                            continue;
                        }

                        uint64_t packed = (static_cast<uint64_t>(x) << 32) |
                                         (static_cast<uint64_t>(y) << 16) |
                                         static_cast<uint64_t>(z);

                        if (data.blocks.find(packed) == data.blocks.end()) {
                            BlockTypeId actual = col->getBlock(x, y, z);
                            if (!actual.isAir()) {
                                ++failureCount;
                                ADD_FAILURE() << "Expected air at (" << x << ", " << y << ", " << z
                                              << ") in column (" << pos.x << ", " << pos.z << ")"
                                              << " but got " << actual.name();
                            }
                        }
                    }
                }
            }

            ++verifiedCount;
        });
    }

    // Wait for all verifications to complete
    while (verifiedCount < static_cast<int>(originalData.size())) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    EXPECT_EQ(failureCount, 0) << "There were " << failureCount << " block mismatches";
    EXPECT_EQ(verifiedCount, static_cast<int>(originalData.size()));

    io2.stop();
}

// Note: RoundTripWithDataContainer test is pending - requires DataContainer
// integration with ChunkColumn (data() accessor). DataContainer serialization
// is tested separately in test_data_container.cpp.
