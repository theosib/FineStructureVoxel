#include <gtest/gtest.h>
#include "finevox/mesh_worker_pool.hpp"
#include "finevox/world.hpp"
#include <thread>
#include <chrono>

using namespace finevox;

// Test fixture with a simple world
class MeshWorkerPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        world_ = std::make_unique<World>();
        queue_ = std::make_unique<MeshRebuildQueue>(mergeMeshRebuildRequest);

        // Create a simple world with some blocks
        stone_ = BlockTypeId::fromName("pool_test:stone");

        // Fill a 2x2x2 region of subchunks with some blocks
        for (int cx = 0; cx < 2; ++cx) {
            for (int cy = 0; cy < 2; ++cy) {
                for (int cz = 0; cz < 2; ++cz) {
                    // Place some blocks in each subchunk
                    int baseX = cx * 16;
                    int baseY = cy * 16;
                    int baseZ = cz * 16;

                    for (int x = 0; x < 4; ++x) {
                        for (int z = 0; z < 4; ++z) {
                            world_->setBlock(baseX + x, baseY, baseZ + z, stone_);
                        }
                    }
                }
            }
        }
    }

    // Helper to push a chunk with default request
    void pushChunk(ChunkPos pos) {
        queue_->push(pos, MeshRebuildRequest::normal(1));
    }

    // Helper to create a shared_ptr<SubChunk> for testing
    // Note: This creates a separate SubChunk, not the one in the world
    std::shared_ptr<SubChunk> createTestSubChunk(ChunkPos pos) {
        auto subchunk = std::make_shared<SubChunk>();
        // Copy data from world's subchunk if it exists
        const SubChunk* worldSubChunk = world_->getSubChunk(pos);
        if (worldSubChunk) {
            for (int x = 0; x < 16; ++x) {
                for (int y = 0; y < 16; ++y) {
                    for (int z = 0; z < 16; ++z) {
                        subchunk->setBlock(x, y, z, worldSubChunk->getBlock(x, y, z));
                    }
                }
            }
        }
        return subchunk;
    }

    std::unique_ptr<World> world_;
    std::unique_ptr<MeshRebuildQueue> queue_;
    BlockTypeId stone_;
};

// ============================================================================
// Basic construction and lifecycle
// ============================================================================

TEST_F(MeshWorkerPoolTest, Construction) {
    MeshWorkerPool pool(*world_, 2);
    EXPECT_FALSE(pool.isRunning());
    EXPECT_EQ(pool.cacheSize(), 0);
}

TEST_F(MeshWorkerPoolTest, StartAndStop) {
    MeshWorkerPool pool(*world_, 2);
    pool.setInputQueue(queue_.get());

    EXPECT_FALSE(pool.isRunning());

    pool.start();
    EXPECT_TRUE(pool.isRunning());

    pool.stop();
    EXPECT_FALSE(pool.isRunning());
}

TEST_F(MeshWorkerPoolTest, StartWithoutInputQueueThrows) {
    MeshWorkerPool pool(*world_, 2);

    EXPECT_THROW(pool.start(), std::runtime_error);
}

TEST_F(MeshWorkerPoolTest, StopIdempotent) {
    MeshWorkerPool pool(*world_, 2);
    pool.setInputQueue(queue_.get());

    // Stop when not running should be safe
    pool.stop();
    pool.stop();

    pool.start();
    pool.stop();
    pool.stop();  // Double stop should be safe
}

// ============================================================================
// Mesh Cache API
// ============================================================================

TEST_F(MeshWorkerPoolTest, GetMeshCreatesEntry) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());
    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    auto result = pool.getMesh(pos, weakSubchunk, lodReq);

    // Entry should be created
    EXPECT_NE(result.entry, nullptr);
    // First call should trigger rebuild
    EXPECT_TRUE(result.rebuildTriggered);

    pool.stop();
}

TEST_F(MeshWorkerPoolTest, BuildsSingleMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());
    pool.setGreedyMeshing(false);  // Simpler for testing

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    // Request mesh - this will trigger rebuild
    auto result = pool.getMesh(pos, weakSubchunk, lodReq);
    EXPECT_TRUE(result.rebuildTriggered);

    // Wait for the mesh to be built
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    // Get mesh again - should have pending mesh
    result = pool.getMesh(pos, weakSubchunk, lodReq);
    ASSERT_NE(result.entry, nullptr);
    EXPECT_TRUE(result.entry->hasPendingMesh());
    EXPECT_FALSE(result.entry->pendingMesh->isEmpty());
}

TEST_F(MeshWorkerPoolTest, MarkUploadedClearsPending) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    // Request mesh
    pool.getMesh(pos, weakSubchunk, lodReq);

    // Wait for mesh to be built
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    // Verify pending mesh exists
    auto result = pool.getMesh(pos, weakSubchunk, lodReq);
    ASSERT_NE(result.entry, nullptr);
    ASSERT_TRUE(result.entry->hasPendingMesh());

    // Mark as uploaded
    pool.markUploaded(pos);

    // Pending should be cleared, uploaded version updated
    result = pool.getMesh(pos, weakSubchunk, lodReq);
    ASSERT_NE(result.entry, nullptr);
    EXPECT_FALSE(result.entry->hasPendingMesh());
    EXPECT_GT(result.entry->uploadedVersion, 0u);
}

TEST_F(MeshWorkerPoolTest, RemoveMeshClearsEntry) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    // Create entry
    pool.getMesh(pos, weakSubchunk, lodReq);
    EXPECT_EQ(pool.cacheSize(), 1);

    pool.stop();

    // Remove mesh
    pool.removeMesh(pos);
    EXPECT_EQ(pool.cacheSize(), 0);
}

TEST_F(MeshWorkerPoolTest, BuildsMultipleMeshes) {
    MeshWorkerPool pool(*world_, 2);
    pool.setInputQueue(queue_.get());

    pool.start();

    // Request several subchunks
    std::vector<ChunkPos> positions = {
        ChunkPos(0, 0, 0),
        ChunkPos(1, 0, 0),
        ChunkPos(0, 1, 0),
        ChunkPos(0, 0, 1)
    };

    // Keep shared_ptrs alive during test
    std::vector<std::shared_ptr<SubChunk>> subchunks;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    for (const auto& pos : positions) {
        auto subchunk = createTestSubChunk(pos);
        subchunks.push_back(subchunk);
        pool.getMesh(pos, std::weak_ptr<SubChunk>(subchunk), lodReq);
    }

    // Wait for all meshes
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() < 4 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    EXPECT_EQ(pool.pendingMeshCount(), 4);
    EXPECT_EQ(pool.cacheSize(), 4);
}

TEST_F(MeshWorkerPoolTest, EmptySubchunkReturnsEmptyMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    // Request an empty subchunk (no blocks placed there)
    ChunkPos pos(10, 10, 10);
    auto subchunk = std::make_shared<SubChunk>();  // Empty subchunk
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    pool.getMesh(pos, weakSubchunk, lodReq);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    auto result = pool.getMesh(pos, weakSubchunk, lodReq);
    ASSERT_NE(result.entry, nullptr);
    // Entry may or may not have pending mesh for empty subchunk
    // If it does, it should be empty
    if (result.entry->hasPendingMesh()) {
        EXPECT_TRUE(result.entry->pendingMesh->isEmpty());
    }
}

// ============================================================================
// Statistics
// ============================================================================

TEST_F(MeshWorkerPoolTest, StatisticsTracked) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    pool.getMesh(pos, weakSubchunk, lodReq);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.stats().meshesBuilt.load() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    EXPECT_GE(pool.stats().meshesBuilt.load(), 1);
    EXPECT_EQ(pool.stats().meshesFailed.load(), 0);
}

// ============================================================================
// Texture provider
// ============================================================================

TEST_F(MeshWorkerPoolTest, TextureProviderUsed) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    std::atomic<int> providerCalls{0};
    pool.setBlockTextureProvider([&](BlockTypeId, Face) {
        ++providerCalls;
        return glm::vec4(0.0f, 0.0f, 0.5f, 0.5f);  // Custom UVs
    });

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    pool.getMesh(pos, weakSubchunk, lodReq);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    // Texture provider should have been called at least once
    EXPECT_GT(providerCalls.load(), 0);
}

// ============================================================================
// Greedy meshing toggle
// ============================================================================

TEST_F(MeshWorkerPoolTest, GreedyMeshingToggle) {
    MeshWorkerPool poolGreedy(*world_, 1);
    poolGreedy.setInputQueue(queue_.get());
    poolGreedy.setGreedyMeshing(true);
    EXPECT_TRUE(poolGreedy.greedyMeshing());

    MeshWorkerPool poolSimple(*world_, 1);
    MeshRebuildQueue queue2(mergeMeshRebuildRequest);
    poolSimple.setInputQueue(&queue2);
    poolSimple.setGreedyMeshing(false);
    EXPECT_FALSE(poolSimple.greedyMeshing());
}

// ============================================================================
// Rebuild trigger
// ============================================================================

TEST_F(MeshWorkerPoolTest, RebuildNotTriggeredWhenUpToDate) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    // First call triggers rebuild
    auto result1 = pool.getMesh(pos, weakSubchunk, lodReq);
    EXPECT_TRUE(result1.rebuildTriggered);

    // Wait for mesh to be built
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Mark as uploaded
    pool.markUploaded(pos);

    pool.stop();

    // Second call should not trigger rebuild (version matches)
    auto result2 = pool.getMesh(pos, weakSubchunk, lodReq);
    EXPECT_FALSE(result2.rebuildTriggered);
}

TEST_F(MeshWorkerPoolTest, RebuildTriggeredOnVersionChange) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    auto subchunk = createTestSubChunk(pos);
    std::weak_ptr<SubChunk> weakSubchunk = subchunk;
    LODRequest lodReq = LODRequest::exact(LODLevel::LOD0);

    // First call triggers rebuild
    pool.getMesh(pos, weakSubchunk, lodReq);

    // Wait for mesh to be built
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.pendingMeshCount() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Mark as uploaded
    pool.markUploaded(pos);

    // Modify the subchunk (this increments blockVersion)
    subchunk->setBlock(5, 5, 5, stone_);

    pool.stop();

    // Should trigger rebuild because version changed
    auto result = pool.getMesh(pos, weakSubchunk, lodReq);
    EXPECT_TRUE(result.rebuildTriggered);
}
