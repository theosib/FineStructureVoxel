#include <gtest/gtest.h>
#include "finevox/column_manager.hpp"
#include "finevox/io_manager.hpp"
#include <filesystem>
#include <thread>
#include <atomic>

using namespace finevox;

// ============================================================================
// Basic ColumnManager tests
// ============================================================================

TEST(ColumnManagerTest, EmptyManager) {
    ColumnManager manager;
    EXPECT_EQ(manager.activeCount(), 0);
    EXPECT_EQ(manager.saveQueueSize(), 0);
    EXPECT_EQ(manager.cacheSize(), 0);
}

TEST(ColumnManagerTest, AddColumn) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(5, 10));
    manager.add(std::move(column));

    EXPECT_EQ(manager.activeCount(), 1);

    ManagedColumn* col = manager.get(ColumnPos(5, 10));
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->column->position(), ColumnPos(5, 10));
}

TEST(ColumnManagerTest, GetNonexistent) {
    ColumnManager manager;
    EXPECT_EQ(manager.get(ColumnPos(99, 99)), nullptr);
}

// ============================================================================
// Reference counting tests
// ============================================================================

TEST(ColumnManagerTest, RefCountBasic) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    manager.addRef(ColumnPos(0, 0));

    ManagedColumn* col = manager.get(ColumnPos(0, 0));
    EXPECT_EQ(col->refCount, 1);

    manager.release(ColumnPos(0, 0));

    // After release with no dirty flag, should move to cache
    EXPECT_EQ(manager.activeCount(), 0);
    EXPECT_EQ(manager.cacheSize(), 1);
}

TEST(ColumnManagerTest, DirtyColumnGoesToSaveQueue) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    manager.addRef(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(0, 0));
    manager.release(ColumnPos(0, 0));

    // Dirty column should go to save queue, not cache
    EXPECT_EQ(manager.saveQueueSize(), 1);
    EXPECT_EQ(manager.cacheSize(), 0);
}

// ============================================================================
// Save queue tests
// ============================================================================

TEST(ColumnManagerTest, GetSaveQueue) {
    ColumnManager manager;

    auto col1 = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    auto col2 = std::make_unique<ChunkColumn>(ColumnPos(1, 0));
    manager.add(std::move(col1));
    manager.add(std::move(col2));

    manager.addRef(ColumnPos(0, 0));
    manager.addRef(ColumnPos(1, 0));
    manager.markDirty(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(1, 0));
    manager.release(ColumnPos(0, 0));
    manager.release(ColumnPos(1, 0));

    auto toSave = manager.getSaveQueue();

    EXPECT_EQ(toSave.size(), 2);
    EXPECT_TRUE(manager.isSaving(ColumnPos(0, 0)));
    EXPECT_TRUE(manager.isSaving(ColumnPos(1, 0)));
}

TEST(ColumnManagerTest, OnSaveComplete) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    manager.addRef(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(0, 0));
    manager.release(ColumnPos(0, 0));

    auto toSave = manager.getSaveQueue();
    EXPECT_TRUE(manager.isSaving(ColumnPos(0, 0)));

    manager.onSaveComplete(ColumnPos(0, 0));

    EXPECT_FALSE(manager.isSaving(ColumnPos(0, 0)));
    EXPECT_EQ(manager.cacheSize(), 1);  // Now in unload cache
}

// ============================================================================
// Cache tests
// ============================================================================

TEST(ColumnManagerTest, RetrieveFromCache) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    manager.addRef(ColumnPos(0, 0));
    manager.release(ColumnPos(0, 0));

    // Should be in cache now
    EXPECT_EQ(manager.cacheSize(), 1);
    EXPECT_EQ(manager.activeCount(), 0);

    // Get should move back to active
    ManagedColumn* col = manager.get(ColumnPos(0, 0));
    ASSERT_NE(col, nullptr);

    EXPECT_EQ(manager.cacheSize(), 0);
    EXPECT_EQ(manager.activeCount(), 1);
}

TEST(ColumnManagerTest, CacheEviction) {
    ColumnManager manager(2);  // Small cache

    // Add 3 columns
    for (int i = 0; i < 3; ++i) {
        auto column = std::make_unique<ChunkColumn>(ColumnPos(i, 0));
        manager.add(std::move(column));
        manager.addRef(ColumnPos(i, 0));
        manager.release(ColumnPos(i, 0));
    }

    // Cache capacity is 2, so one should be evicted
    EXPECT_EQ(manager.cacheSize(), 2);
}

TEST(ColumnManagerTest, EvictionCallback) {
    ColumnManager manager(2);

    int evictionCount = 0;
    manager.setEvictionCallback([&evictionCount](std::unique_ptr<ChunkColumn>) {
        ++evictionCount;
    });

    for (int i = 0; i < 3; ++i) {
        auto column = std::make_unique<ChunkColumn>(ColumnPos(i, 0));
        manager.add(std::move(column));
        manager.addRef(ColumnPos(i, 0));
        manager.release(ColumnPos(i, 0));
    }

    EXPECT_EQ(evictionCount, 1);
}

TEST(ColumnManagerTest, ChunkLoadCallback) {
    ColumnManager manager;

    std::vector<ColumnPos> loadedPositions;
    manager.setChunkLoadCallback([&loadedPositions](ColumnPos pos) {
        loadedPositions.push_back(pos);
    });

    // Add columns - callback should fire for each
    manager.add(std::make_unique<ChunkColumn>(ColumnPos(0, 0)));
    manager.add(std::make_unique<ChunkColumn>(ColumnPos(1, 1)));
    manager.add(std::make_unique<ChunkColumn>(ColumnPos(2, 3)));

    ASSERT_EQ(loadedPositions.size(), 3);
    EXPECT_EQ(loadedPositions[0], ColumnPos(0, 0));
    EXPECT_EQ(loadedPositions[1], ColumnPos(1, 1));
    EXPECT_EQ(loadedPositions[2], ColumnPos(2, 3));
}

// ============================================================================
// Currently saving protection
// ============================================================================

TEST(ColumnManagerTest, CantRetrieveWhileSaving) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    manager.addRef(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(0, 0));
    manager.release(ColumnPos(0, 0));

    auto toSave = manager.getSaveQueue();
    EXPECT_TRUE(manager.isSaving(ColumnPos(0, 0)));

    // While saving, get should return nullptr
    ManagedColumn* col = manager.get(ColumnPos(0, 0));
    EXPECT_EQ(col, nullptr);
}

// ============================================================================
// GetAllDirty tests
// ============================================================================

TEST(ColumnManagerTest, GetAllDirty) {
    ColumnManager manager;

    auto col1 = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    auto col2 = std::make_unique<ChunkColumn>(ColumnPos(1, 0));
    auto col3 = std::make_unique<ChunkColumn>(ColumnPos(2, 0));

    manager.add(std::move(col1));
    manager.add(std::move(col2));
    manager.add(std::move(col3));

    manager.markDirty(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(2, 0));

    auto dirty = manager.getAllDirty();

    EXPECT_EQ(dirty.size(), 2);
}

// ============================================================================
// State tracking tests
// ============================================================================

TEST(ColumnManagerTest, ColumnState) {
    ColumnManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(0, 0));
    manager.add(std::move(column));

    // Initially active
    ManagedColumn* col = manager.get(ColumnPos(0, 0));
    EXPECT_EQ(col->state, ColumnState::Active);

    // Mark dirty and release
    manager.addRef(ColumnPos(0, 0));
    manager.markDirty(ColumnPos(0, 0));
    manager.release(ColumnPos(0, 0));

    // Should now be in save queue (can't get direct access when queued)
    auto toSave = manager.getSaveQueue();
    col = manager.get(ColumnPos(0, 0));
    // Can't retrieve while saving
    EXPECT_EQ(col, nullptr);

    manager.onSaveComplete(ColumnPos(0, 0));

    // Now should be in unload queue, retrievable
    col = manager.get(ColumnPos(0, 0));
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->state, ColumnState::Active);  // Retrieved back to active
}

// ============================================================================
// IOManager integration tests
// ============================================================================

class ColumnManagerIOTest : public ::testing::Test {
protected:
    std::filesystem::path tempDir;

    void SetUp() override {
        tempDir = std::filesystem::temp_directory_path() / "finevox_test_scm_io";
        std::filesystem::create_directories(tempDir);
    }

    void TearDown() override {
        std::filesystem::remove_all(tempDir);
    }
};

TEST_F(ColumnManagerIOTest, BindUnbindIOManager) {
    ColumnManager manager;
    IOManager io(tempDir);
    io.start();

    manager.bindIOManager(&io);
    // Should not crash
    manager.unbindIOManager();

    io.stop();
}

TEST_F(ColumnManagerIOTest, SaveViaIOManager) {
    ColumnManager manager;
    IOManager io(tempDir);
    io.start();

    manager.bindIOManager(&io);

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Add a column
    auto column = std::make_unique<ChunkColumn>(ColumnPos{0, 0});
    column->setBlock(0, 0, 0, stone);
    manager.add(std::move(column));

    // Mark dirty and release to queue for save
    manager.addRef(ColumnPos{0, 0});
    manager.markDirty(ColumnPos{0, 0});
    manager.release(ColumnPos{0, 0});

    // Process the save queue
    manager.processSaveQueue();

    // Wait for save to complete
    io.flush();

    // Wait a bit for callbacks to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    manager.unbindIOManager();
    io.stop();

    // Verify file was created
    EXPECT_TRUE(std::filesystem::exists(tempDir / "r.0.0.dat"));
}

TEST_F(ColumnManagerIOTest, LoadViaIOManager) {
    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // First, save a column directly via IOManager
    {
        IOManager io(tempDir);
        io.start();

        ChunkColumn col(ColumnPos{5, 10});
        col.setBlock(1, 2, 3, stone);
        io.queueSave(ColumnPos{5, 10}, col);
        io.flush();
        io.stop();
    }

    // Now use ColumnManager to load it
    {
        ColumnManager manager;
        IOManager io(tempDir);
        io.start();

        manager.bindIOManager(&io);

        std::atomic<bool> loadComplete{false};
        bool loadSuccess = manager.requestLoad(ColumnPos{5, 10}, [&](ColumnPos pos, std::unique_ptr<ChunkColumn>) {
            EXPECT_EQ(pos, (ColumnPos{5, 10}));
            loadComplete = true;
        });

        EXPECT_TRUE(loadSuccess);

        // Wait for load to complete
        while (!loadComplete) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Column should now be in manager
        ManagedColumn* col = manager.get(ColumnPos{5, 10});
        ASSERT_NE(col, nullptr);
        EXPECT_EQ(col->column->getBlock(1, 2, 3), stone);

        manager.unbindIOManager();
        io.stop();
    }
}

TEST_F(ColumnManagerIOTest, RoundTripWithCompression) {
    ColumnManager manager;
    IOManager io(tempDir);
    io.start();

    manager.bindIOManager(&io);

    BlockTypeId stone = BlockTypeId::fromName("test:stone");

    // Create a larger column with repetitive data (compresses well)
    auto column = std::make_unique<ChunkColumn>(ColumnPos{0, 0});
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                column->setBlock(x, y, z, stone);
            }
        }
    }
    manager.add(std::move(column));

    // Save via manager
    manager.addRef(ColumnPos{0, 0});
    manager.markDirty(ColumnPos{0, 0});
    manager.release(ColumnPos{0, 0});
    manager.processSaveQueue();
    io.flush();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    manager.unbindIOManager();
    io.stop();

    // Create new manager and load back
    ColumnManager manager2;
    IOManager io2(tempDir);
    io2.start();

    manager2.bindIOManager(&io2);

    std::atomic<bool> loaded{false};
    manager2.requestLoad(ColumnPos{0, 0}, [&](ColumnPos, std::unique_ptr<ChunkColumn>) {
        loaded = true;
    });

    while (!loaded) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    ManagedColumn* col = manager2.get(ColumnPos{0, 0});
    ASSERT_NE(col, nullptr);

    // Verify all blocks
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                EXPECT_EQ(col->column->getBlock(x, y, z), stone);
            }
        }
    }

    manager2.unbindIOManager();
    io2.stop();
}
