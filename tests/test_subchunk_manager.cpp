#include <gtest/gtest.h>
#include "finevox/subchunk_manager.hpp"

using namespace finevox;

// ============================================================================
// Basic SubChunkManager tests
// ============================================================================

TEST(SubChunkManagerTest, EmptyManager) {
    SubChunkManager manager;
    EXPECT_EQ(manager.activeCount(), 0);
    EXPECT_EQ(manager.saveQueueSize(), 0);
    EXPECT_EQ(manager.cacheSize(), 0);
}

TEST(SubChunkManagerTest, AddColumn) {
    SubChunkManager manager;

    auto column = std::make_unique<ChunkColumn>(ColumnPos(5, 10));
    manager.add(std::move(column));

    EXPECT_EQ(manager.activeCount(), 1);

    ManagedColumn* col = manager.get(ColumnPos(5, 10));
    ASSERT_NE(col, nullptr);
    EXPECT_EQ(col->column->position(), ColumnPos(5, 10));
}

TEST(SubChunkManagerTest, GetNonexistent) {
    SubChunkManager manager;
    EXPECT_EQ(manager.get(ColumnPos(99, 99)), nullptr);
}

// ============================================================================
// Reference counting tests
// ============================================================================

TEST(SubChunkManagerTest, RefCountBasic) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, DirtyColumnGoesToSaveQueue) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, GetSaveQueue) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, OnSaveComplete) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, RetrieveFromCache) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, CacheEviction) {
    SubChunkManager manager(2);  // Small cache

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

TEST(SubChunkManagerTest, EvictionCallback) {
    SubChunkManager manager(2);

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

// ============================================================================
// Currently saving protection
// ============================================================================

TEST(SubChunkManagerTest, CantRetrieveWhileSaving) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, GetAllDirty) {
    SubChunkManager manager;

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

TEST(SubChunkManagerTest, ColumnState) {
    SubChunkManager manager;

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
