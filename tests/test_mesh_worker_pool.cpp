#include <gtest/gtest.h>
#include "finevox/mesh_worker_pool.hpp"
#include "finevox/world.hpp"
#include "finevox/wake_signal.hpp"
#include <thread>
#include <chrono>
#include <atomic>

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

    // Helper to push a rebuild request to the input queue
    void pushRebuildRequest(ChunkPos pos, uint64_t blockVersion = 1, uint64_t lightVersion = 1) {
        queue_->push(pos, MeshRebuildRequest::normal(blockVersion, lightVersion));
    }

    // Wait for upload queue to have at least count items
    bool waitForUploads(MeshWorkerPool& pool, size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
        auto deadline = std::chrono::steady_clock::now() + timeout;
        while (pool.uploadQueueSize() < count &&
               std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return pool.uploadQueueSize() >= count;
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
    EXPECT_EQ(pool.uploadQueueSize(), 0);
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
// Push-based Mesh Building
// ============================================================================

TEST_F(MeshWorkerPoolTest, BuildsSingleMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());
    pool.setGreedyMeshing(false);  // Simpler for testing

    pool.start();

    ChunkPos pos(0, 0, 0);
    pushRebuildRequest(pos);

    // Wait for the mesh to be built (appears in upload queue)
    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    // Pop from upload queue - should have mesh data
    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    EXPECT_EQ(uploadData->pos, pos);
    EXPECT_FALSE(uploadData->mesh.isEmpty());
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

    for (const auto& pos : positions) {
        pushRebuildRequest(pos);
    }

    // Wait for all meshes in upload queue
    ASSERT_TRUE(waitForUploads(pool, 4));

    pool.stop();

    // Should have 4 meshes
    EXPECT_EQ(pool.uploadQueueSize(), 4);
}

TEST_F(MeshWorkerPoolTest, EmptySubchunkProducesEmptyMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    // Request an empty subchunk (no blocks placed there)
    ChunkPos pos(10, 10, 10);
    pushRebuildRequest(pos);

    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    // Pop from upload queue - should have empty mesh
    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    EXPECT_EQ(uploadData->pos, pos);
    EXPECT_TRUE(uploadData->mesh.isEmpty());
}

TEST_F(MeshWorkerPoolTest, MeshIncludesVersionInfo) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    pushRebuildRequest(pos);

    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    // The mesh versions come from the subchunk at build time, not the request
    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    // Versions should be non-zero (from the actual subchunk)
    // We can't predict exact values as they depend on subchunk state
    EXPECT_GT(uploadData->blockVersion, 0);
}

// ============================================================================
// Statistics
// ============================================================================

TEST_F(MeshWorkerPoolTest, StatisticsTracked) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    pushRebuildRequest(pos);

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
    pushRebuildRequest(pos);

    ASSERT_TRUE(waitForUploads(pool, 1));

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
// Upload Queue (push-based mesh updates)
// ============================================================================

TEST_F(MeshWorkerPoolTest, UploadQueueReceivesCompletedMesh) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());
    pool.setGreedyMeshing(false);

    pool.start();

    ChunkPos pos(0, 0, 0);
    pushRebuildRequest(pos);

    // Wait for mesh to appear in upload queue
    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    // Should have mesh in upload queue
    EXPECT_GE(pool.uploadQueueSize(), 1);

    // Pop from upload queue
    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    EXPECT_EQ(uploadData->pos, pos);
    EXPECT_FALSE(uploadData->mesh.isEmpty());
}

TEST_F(MeshWorkerPoolTest, UploadQueueWithWakeSignal) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    // Attach a WakeSignal to the upload queue
    WakeSignal wakeSignal;
    pool.uploadQueue().attach(&wakeSignal);

    pool.start();

    ChunkPos pos(0, 0, 0);
    std::atomic<bool> woke{false};

    // Start consumer thread that waits on the wake signal
    std::thread consumer([&]() {
        wakeSignal.waitFor(std::chrono::seconds(2));
        woke = true;
    });

    // Small delay to ensure consumer is waiting
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(woke);

    // Push request - this will trigger rebuild, which pushes to upload queue
    pushRebuildRequest(pos);

    // Consumer should wake up when mesh is pushed to upload queue
    consumer.join();

    pool.stop();

    EXPECT_TRUE(woke);
    // Should have mesh available
    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
}

// ============================================================================
// Request Coalescing via MeshRebuildQueue
// ============================================================================

TEST_F(MeshWorkerPoolTest, RequestCoalescingPreventsDuplicateBuilds) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    // Don't start pool yet - let requests coalesce in the queue

    ChunkPos pos(0, 0, 0);

    // Push multiple requests for the same position
    pushRebuildRequest(pos, 1, 1);
    pushRebuildRequest(pos, 2, 2);  // Should overwrite the first
    pushRebuildRequest(pos, 3, 3);  // Should overwrite again

    // Now start - only one item should be in queue due to coalescing
    pool.start();

    // Wait for one mesh
    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    // Should have exactly one mesh (the coalesced result)
    // The coalescing merged 3 requests into 1 build
    EXPECT_EQ(pool.uploadQueueSize(), 1);

    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    // Versions come from subchunk at build time, not from request
    // The key test is that only ONE mesh was built despite 3 requests
    EXPECT_GT(uploadData->blockVersion, 0);
}

// ============================================================================
// LOD Support
// ============================================================================

TEST_F(MeshWorkerPoolTest, LODMergeModeConfigurable) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.setLODMergeMode(LODMergeMode::FullHeight);
    EXPECT_EQ(pool.lodMergeMode(), LODMergeMode::FullHeight);

    pool.setLODMergeMode(LODMergeMode::HeightLimited);
    EXPECT_EQ(pool.lodMergeMode(), LODMergeMode::HeightLimited);
}

TEST_F(MeshWorkerPoolTest, MeshIncludesLODLevel) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    ChunkPos pos(0, 0, 0);
    queue_->push(pos, MeshRebuildRequest::normal(1, 1, LODLevel::LOD2));

    ASSERT_TRUE(waitForUploads(pool, 1));

    pool.stop();

    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
    EXPECT_EQ(uploadData->lodLevel, LODLevel::LOD2);
}

// ============================================================================
// Alarm-based Wake Support
// ============================================================================

TEST_F(MeshWorkerPoolTest, AlarmWakesWorkers) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    pool.start();

    // Set an alarm for 50ms from now
    auto alarmTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    pool.setAlarm(alarmTime);

    // Push a request after a delay
    std::thread delayedPush([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        pushRebuildRequest(ChunkPos(0, 0, 0));
    });

    // Wait for mesh
    ASSERT_TRUE(waitForUploads(pool, 1));

    delayedPush.join();
    pool.stop();

    auto uploadData = pool.tryPopUpload();
    ASSERT_TRUE(uploadData.has_value());
}

TEST_F(MeshWorkerPoolTest, ClearAlarm) {
    MeshWorkerPool pool(*world_, 1);
    pool.setInputQueue(queue_.get());

    // Should not throw
    pool.clearAlarm();
    pool.setAlarm(std::chrono::steady_clock::now() + std::chrono::hours(1));
    pool.clearAlarm();
}

// ============================================================================
// Thread Count
// ============================================================================

TEST_F(MeshWorkerPoolTest, ThreadCountReported) {
    MeshWorkerPool pool2(*world_, 2);
    pool2.setInputQueue(queue_.get());
    // Threads are created when start() is called
    EXPECT_EQ(pool2.threadCount(), 0);  // Not started yet
    pool2.start();
    EXPECT_EQ(pool2.threadCount(), 2);
    pool2.stop();

    MeshWorkerPool pool4(*world_, 4);
    MeshRebuildQueue queue2(mergeMeshRebuildRequest);
    pool4.setInputQueue(&queue2);
    pool4.start();
    EXPECT_EQ(pool4.threadCount(), 4);
    pool4.stop();
}
