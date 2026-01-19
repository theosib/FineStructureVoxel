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
        queue_ = std::make_unique<MeshRebuildQueue>();

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
    EXPECT_EQ(pool.resultQueueSize(), 0);
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
// Mesh building
// ============================================================================

TEST_F(MeshWorkerPoolTest, BuildsSingleMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());
    pool.setGreedyMeshing(false);  // Simpler for testing

    // Queue a subchunk for rebuild
    queue_->push(ChunkPos(0, 0, 0));

    pool.start();

    // Wait for the mesh to be built
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.resultQueueSize() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    ASSERT_EQ(pool.resultQueueSize(), 1);

    auto result = pool.popResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->pos, ChunkPos(0, 0, 0));
    EXPECT_TRUE(result->success);
    EXPECT_FALSE(result->meshData.isEmpty());
}

TEST_F(MeshWorkerPoolTest, BuildsMultipleMeshes) {
    MeshWorkerPool pool(*world_, 2);
    pool.setInputQueue(queue_.get());

    // Queue several subchunks
    queue_->push(ChunkPos(0, 0, 0));
    queue_->push(ChunkPos(1, 0, 0));
    queue_->push(ChunkPos(0, 1, 0));
    queue_->push(ChunkPos(0, 0, 1));

    pool.start();

    // Wait for all meshes
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.resultQueueSize() < 4 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    EXPECT_EQ(pool.resultQueueSize(), 4);

    // All should be successful
    auto results = pool.popResultBatch(10);
    EXPECT_EQ(results.size(), 4);
    for (const auto& result : results) {
        EXPECT_TRUE(result.success);
    }
}

TEST_F(MeshWorkerPoolTest, EmptySubchunkReturnsEmptyMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    // Queue an empty subchunk (no blocks placed there)
    queue_->push(ChunkPos(10, 10, 10));

    pool.start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.resultQueueSize() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    auto result = pool.popResult();
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->success);
    EXPECT_TRUE(result->meshData.isEmpty());  // Empty subchunk = empty mesh
}

// ============================================================================
// Statistics
// ============================================================================

TEST_F(MeshWorkerPoolTest, StatisticsTracked) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    queue_->push(ChunkPos(0, 0, 0));

    pool.start();

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

    queue_->push(ChunkPos(0, 0, 0));

    pool.start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.resultQueueSize() == 0 &&
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
    MeshRebuildQueue queue2;
    poolSimple.setInputQueue(&queue2);
    poolSimple.setGreedyMeshing(false);
    EXPECT_FALSE(poolSimple.greedyMeshing());
}

// ============================================================================
// Pop batch
// ============================================================================

TEST_F(MeshWorkerPoolTest, PopResultBatch) {
    MeshWorkerPool pool(*world_, 2);
    pool.setInputQueue(queue_.get());

    // Queue many subchunks
    for (int i = 0; i < 8; ++i) {
        queue_->push(ChunkPos(i, 0, 0));
    }

    pool.start();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (pool.resultQueueSize() < 8 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    pool.stop();

    // Pop in batches
    auto batch1 = pool.popResultBatch(3);
    EXPECT_EQ(batch1.size(), 3);

    auto batch2 = pool.popResultBatch(3);
    EXPECT_EQ(batch2.size(), 3);

    auto batch3 = pool.popResultBatch(10);  // Request more than available
    EXPECT_EQ(batch3.size(), 2);

    EXPECT_EQ(pool.resultQueueSize(), 0);
}

// ============================================================================
// No result when empty
// ============================================================================

TEST_F(MeshWorkerPoolTest, PopResultWhenEmpty) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    auto result = pool.popResult();
    EXPECT_FALSE(result.has_value());

    auto batch = pool.popResultBatch(10);
    EXPECT_TRUE(batch.empty());
}
