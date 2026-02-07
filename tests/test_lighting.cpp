#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <cmath>
#include "finevox/core/light_data.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/chunk_column.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/mesh.hpp"

using namespace finevox;

// ============================================================================
// LightData Tests (standalone class - may be deprecated)
// ============================================================================

TEST(LightDataTest, InitiallyDark) {
    LightData data;
    EXPECT_TRUE(data.isDark());
    EXPECT_EQ(data.getSkyLight(0, 0, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, 0, 0), 0);
}

TEST(LightDataTest, SetGetSkyLight) {
    LightData data;

    data.setSkyLight(5, 5, 5, 15);
    EXPECT_EQ(data.getSkyLight(5, 5, 5), 15);
    EXPECT_EQ(data.getBlockLight(5, 5, 5), 0);  // Block light unchanged

    data.setSkyLight(5, 5, 5, 8);
    EXPECT_EQ(data.getSkyLight(5, 5, 5), 8);
}

TEST(LightDataTest, SetGetBlockLight) {
    LightData data;

    data.setBlockLight(3, 7, 11, 12);
    EXPECT_EQ(data.getBlockLight(3, 7, 11), 12);
    EXPECT_EQ(data.getSkyLight(3, 7, 11), 0);  // Sky light unchanged
}

TEST(LightDataTest, CombinedLight) {
    LightData data;

    data.setSkyLight(0, 0, 0, 10);
    data.setBlockLight(0, 0, 0, 5);
    EXPECT_EQ(data.getCombinedLight(0, 0, 0), 10);  // Max of sky and block

    data.setBlockLight(0, 0, 0, 15);
    EXPECT_EQ(data.getCombinedLight(0, 0, 0), 15);  // Now block is higher
}

TEST(LightDataTest, PackedLight) {
    LightData data;

    data.setLight(1, 2, 3, 12, 7);  // Sky=12, Block=7
    EXPECT_EQ(data.getSkyLight(1, 2, 3), 12);
    EXPECT_EQ(data.getBlockLight(1, 2, 3), 7);

    uint8_t packed = data.getPackedLight(1, 2, 3);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

TEST(LightDataTest, FillSkyLight) {
    LightData data;

    data.fillSkyLight(15);
    EXPECT_TRUE(data.isFullSkyLight());
    EXPECT_EQ(data.getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(data.getSkyLight(15, 15, 15), 15);
}

TEST(LightDataTest, Clear) {
    LightData data;

    data.setLight(5, 5, 5, 10, 10);
    EXPECT_FALSE(data.isDark());

    data.clear();
    EXPECT_TRUE(data.isDark());
}

TEST(LightDataTest, VersionIncrement) {
    LightData data;

    uint64_t v1 = data.version();
    data.setSkyLight(0, 0, 0, 5);
    uint64_t v2 = data.version();
    EXPECT_GT(v2, v1);

    // Setting to same value shouldn't increment
    data.setSkyLight(0, 0, 0, 5);
    uint64_t v3 = data.version();
    EXPECT_EQ(v3, v2);
}

TEST(LightDataTest, OutOfBoundsReturnsZero) {
    LightData data;

    EXPECT_EQ(data.getSkyLight(-1, 0, 0), 0);
    EXPECT_EQ(data.getSkyLight(16, 0, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, -1, 0), 0);
    EXPECT_EQ(data.getBlockLight(0, 16, 0), 0);
}

// ============================================================================
// SubChunk Light Storage Tests
// ============================================================================

TEST(SubChunkLightTest, InitiallyDark) {
    SubChunk subChunk;
    EXPECT_TRUE(subChunk.isLightDark());
    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 0);
    EXPECT_EQ(subChunk.getBlockLight(0, 0, 0), 0);
}

TEST(SubChunkLightTest, SetGetSkyLight) {
    SubChunk subChunk;

    subChunk.setSkyLight(5, 5, 5, 15);
    EXPECT_EQ(subChunk.getSkyLight(5, 5, 5), 15);
    EXPECT_EQ(subChunk.getBlockLight(5, 5, 5), 0);  // Block light unchanged

    subChunk.setSkyLight(5, 5, 5, 8);
    EXPECT_EQ(subChunk.getSkyLight(5, 5, 5), 8);
}

TEST(SubChunkLightTest, SetGetBlockLight) {
    SubChunk subChunk;

    subChunk.setBlockLight(3, 7, 11, 12);
    EXPECT_EQ(subChunk.getBlockLight(3, 7, 11), 12);
    EXPECT_EQ(subChunk.getSkyLight(3, 7, 11), 0);  // Sky light unchanged
}

TEST(SubChunkLightTest, CombinedLight) {
    SubChunk subChunk;

    subChunk.setSkyLight(0, 0, 0, 10);
    subChunk.setBlockLight(0, 0, 0, 5);
    EXPECT_EQ(subChunk.getCombinedLight(0, 0, 0), 10);  // Max of sky and block

    subChunk.setBlockLight(0, 0, 0, 15);
    EXPECT_EQ(subChunk.getCombinedLight(0, 0, 0), 15);  // Now block is higher
}

TEST(SubChunkLightTest, PackedLight) {
    SubChunk subChunk;

    subChunk.setLight(1, 2, 3, 12, 7);  // Sky=12, Block=7
    EXPECT_EQ(subChunk.getSkyLight(1, 2, 3), 12);
    EXPECT_EQ(subChunk.getBlockLight(1, 2, 3), 7);

    uint8_t packed = subChunk.getPackedLight(1, 2, 3);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

TEST(SubChunkLightTest, FillSkyLight) {
    SubChunk subChunk;

    subChunk.fillSkyLight(15);
    EXPECT_TRUE(subChunk.isFullSkyLight());
    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(subChunk.getSkyLight(15, 15, 15), 15);
}

TEST(SubChunkLightTest, ClearLight) {
    SubChunk subChunk;

    subChunk.setLight(5, 5, 5, 10, 10);
    EXPECT_FALSE(subChunk.isLightDark());

    subChunk.clearLight();
    EXPECT_TRUE(subChunk.isLightDark());
}

TEST(SubChunkLightTest, LightVersionIncrement) {
    SubChunk subChunk;

    uint64_t v1 = subChunk.lightVersion();
    subChunk.setSkyLight(0, 0, 0, 5);
    uint64_t v2 = subChunk.lightVersion();
    EXPECT_GT(v2, v1);

    // Setting to same value shouldn't increment
    subChunk.setSkyLight(0, 0, 0, 5);
    uint64_t v3 = subChunk.lightVersion();
    EXPECT_EQ(v3, v2);
}

TEST(SubChunkLightTest, OutOfBoundsReturnsZero) {
    SubChunk subChunk;

    // Use index-based access for out-of-bounds testing
    EXPECT_EQ(subChunk.getSkyLight(-1), 0);
    EXPECT_EQ(subChunk.getSkyLight(4096), 0);
    EXPECT_EQ(subChunk.getBlockLight(-1), 0);
    EXPECT_EQ(subChunk.getBlockLight(4096), 0);
}

TEST(SubChunkLightTest, SetLightData) {
    SubChunk subChunk;

    std::array<uint8_t, 4096> data;
    data.fill(packLightValue(10, 5));

    subChunk.setLightData(data);

    EXPECT_EQ(subChunk.getSkyLight(0, 0, 0), 10);
    EXPECT_EQ(subChunk.getBlockLight(0, 0, 0), 5);
    EXPECT_EQ(subChunk.getSkyLight(15, 15, 15), 10);
}

TEST(SubChunkLightTest, GetLightData) {
    SubChunk subChunk;

    subChunk.setSkyLight(0, 0, 0, 15);
    subChunk.setBlockLight(0, 0, 0, 7);

    const auto& data = subChunk.lightData();
    uint8_t packed = data[0];  // Index 0 = position (0,0,0)
    EXPECT_EQ(unpackSkyLightValue(packed), 15);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);
}

// ============================================================================
// BlockType Lighting Properties Tests
// ============================================================================

TEST(BlockTypeLightTest, DefaultProperties) {
    BlockType type;

    EXPECT_EQ(type.lightEmission(), 0);
    EXPECT_EQ(type.lightAttenuation(), 15);  // Opaque by default
    EXPECT_TRUE(type.blocksSkyLight());
}

TEST(BlockTypeLightTest, SetLightEmission) {
    BlockType type;
    type.setLightEmission(14);

    EXPECT_EQ(type.lightEmission(), 14);

    // Clamp to max
    type.setLightEmission(20);
    EXPECT_EQ(type.lightEmission(), 15);
}

TEST(BlockTypeLightTest, SetLightAttenuation) {
    BlockType type;
    type.setLightAttenuation(1);

    EXPECT_EQ(type.lightAttenuation(), 1);

    // Clamp to valid range
    type.setLightAttenuation(0);  // Should become 1
    EXPECT_EQ(type.lightAttenuation(), 1);

    type.setLightAttenuation(20);  // Should become 15
    EXPECT_EQ(type.lightAttenuation(), 15);
}

TEST(BlockTypeLightTest, SetBlocksSkyLight) {
    BlockType type;
    EXPECT_TRUE(type.blocksSkyLight());

    type.setBlocksSkyLight(false);
    EXPECT_FALSE(type.blocksSkyLight());
}

TEST(BlockTypeLightTest, TransparentBlockProperties) {
    BlockType glass;
    glass.setOpaque(false)
         .setTransparent(true)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    EXPECT_FALSE(glass.isOpaque());
    EXPECT_TRUE(glass.isTransparent());
    EXPECT_EQ(glass.lightAttenuation(), 1);
    EXPECT_FALSE(glass.blocksSkyLight());
}

TEST(BlockTypeLightTest, TorchProperties) {
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    EXPECT_FALSE(torch.hasCollision());
    EXPECT_FALSE(torch.isOpaque());
    EXPECT_EQ(torch.lightEmission(), 14);
    EXPECT_EQ(torch.lightAttenuation(), 1);
    EXPECT_FALSE(torch.blocksSkyLight());
}

// ============================================================================
// ChunkColumn Heightmap Tests
// ============================================================================

TEST(HeightmapTest, InitiallyNoHeight) {
    ChunkColumn column(ColumnPos{0, 0});

    // No blocks placed, heightmap should indicate no opaque blocks
    EXPECT_EQ(column.getHeight(0, 0), std::numeric_limits<int32_t>::min());
    EXPECT_TRUE(column.heightmapDirty());
}

TEST(HeightmapTest, UpdateHeightOnBlockPlace) {
    ChunkColumn column(ColumnPos{0, 0});

    // Place a block at y=10
    column.updateHeight(5, 5, 10, true);
    EXPECT_EQ(column.getHeight(5, 5), 11);  // Height is top of block (y + 1)

    // Place a higher block
    column.updateHeight(5, 5, 20, true);
    EXPECT_EQ(column.getHeight(5, 5), 21);
}

TEST(HeightmapTest, SetHeightmapData) {
    ChunkColumn column(ColumnPos{0, 0});

    std::array<int32_t, 256> data;
    data.fill(100);
    data[0] = 50;

    column.setHeightmapData(data);

    EXPECT_EQ(column.getHeight(0, 0), 50);
    EXPECT_EQ(column.getHeight(1, 0), 100);
    EXPECT_FALSE(column.heightmapDirty());
}

// ============================================================================
// LightEngine Basic Tests
// ============================================================================

TEST(LightEngineTest, InitiallyDark) {
    World world;
    LightEngine engine(world);

    EXPECT_EQ(engine.getSkyLight(BlockPos{0, 0, 0}), 0);
    EXPECT_EQ(engine.getBlockLight(BlockPos{0, 0, 0}), 0);
}

TEST(LightEngineTest, RegisterBlockType) {
    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);

    BlockRegistry::global().registerType("test:torch", torch);

    const BlockType& retrieved = BlockRegistry::global().getType("test:torch");
    EXPECT_EQ(retrieved.lightEmission(), 14);
}

TEST(LightEngineTest, PropagateBlockLight) {
    World world;
    LightEngine engine(world);

    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("lighttest:torch", torch);

    // Place a torch
    BlockPos torchPos{8, 8, 8};
    BlockTypeId torchId = BlockTypeId::fromName("lighttest:torch");
    world.setBlock(torchPos, torchId);

    // Propagate light
    engine.propagateBlockLight(torchPos, 14);

    // Check light at torch position
    EXPECT_EQ(engine.getBlockLight(torchPos), 14);

    // Check light decreases with distance
    EXPECT_LT(engine.getBlockLight(BlockPos{9, 8, 8}), 14);
}

TEST(LightEngineTest, SubChunkCreatedOnDemand) {
    World world;
    LightEngine engine(world);

    ChunkPos chunkPos{0, 0, 0};

    // No subchunk initially
    EXPECT_EQ(world.getSubChunk(chunkPos), nullptr);

    // Propagate some light - this should create the subchunk
    engine.propagateBlockLight(BlockPos{8, 8, 8}, 10);

    // Now subchunk should exist with light data
    SubChunk* subChunk = world.getSubChunk(chunkPos);
    EXPECT_NE(subChunk, nullptr);
    EXPECT_EQ(subChunk->getBlockLight(8, 8, 8), 10);
}

TEST(LightEngineTest, LightStoredInSubChunk) {
    World world;
    LightEngine engine(world);

    // Place a block first to create the subchunk
    BlockPos pos{4, 4, 4};
    world.setBlock(pos, BlockTypeId::fromName("minecraft:stone"));

    // Propagate light
    engine.propagateBlockLight(pos, 12);

    // Verify light is stored in the subchunk
    SubChunk* subChunk = world.getSubChunk(ChunkPos{0, 0, 0});
    ASSERT_NE(subChunk, nullptr);
    EXPECT_EQ(subChunk->getBlockLight(4, 4, 4), 12);

    // LightEngine should return the same value
    EXPECT_EQ(engine.getBlockLight(pos), 12);
}

// ============================================================================
// Utility Function Tests
// ============================================================================

TEST(LightUtilsTest, PackUnpackLight) {
    uint8_t packed = packLightValue(12, 7);
    EXPECT_EQ(unpackSkyLightValue(packed), 12);
    EXPECT_EQ(unpackBlockLightValue(packed), 7);

    // Edge cases
    packed = packLightValue(0, 0);
    EXPECT_EQ(unpackSkyLightValue(packed), 0);
    EXPECT_EQ(unpackBlockLightValue(packed), 0);

    packed = packLightValue(15, 15);
    EXPECT_EQ(unpackSkyLightValue(packed), 15);
    EXPECT_EQ(unpackBlockLightValue(packed), 15);
}

TEST(LightUtilsTest, CombinedLightValue) {
    uint8_t packed = packLightValue(10, 5);
    EXPECT_EQ(combinedLightValue(packed), 10);

    packed = packLightValue(5, 12);
    EXPECT_EQ(combinedLightValue(packed), 12);

    packed = packLightValue(8, 8);
    EXPECT_EQ(combinedLightValue(packed), 8);
}

// ============================================================================
// Lighting Deferral Tests
// ============================================================================

TEST(LightingDeferralTest, TriggerMeshRebuildFlag) {
    // Test that LightingUpdate has the triggerMeshRebuild flag
    LightingUpdate update;
    update.pos = BlockPos{0, 0, 0};
    update.oldType = AIR_BLOCK_TYPE;
    update.newType = BlockTypeId::fromName("minecraft:stone");
    update.triggerMeshRebuild = true;

    EXPECT_TRUE(update.triggerMeshRebuild);

    // Default should be false
    LightingUpdate defaultUpdate;
    EXPECT_FALSE(defaultUpdate.triggerMeshRebuild);
}

TEST(LightingDeferralTest, MeshRebuildQueueIntegration) {
    World world;
    LightEngine engine(world);

    // Create a mesh rebuild queue
    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Create a subchunk with a block
    BlockPos pos{8, 8, 8};
    world.setBlock(pos, BlockTypeId::fromName("minecraft:stone"));

    // Get the subchunk to verify it exists
    ChunkPos chunkPos{0, 0, 0};
    SubChunk* subChunk = world.getSubChunk(chunkPos);
    ASSERT_NE(subChunk, nullptr);

    // Register a torch block for light emission
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("defertest:torch", torch);

    // Enqueue a lighting update with triggerMeshRebuild=true
    LightingUpdate update;
    update.pos = pos;
    update.oldType = BlockTypeId::fromName("minecraft:stone");
    update.newType = BlockTypeId::fromName("defertest:torch");
    update.triggerMeshRebuild = true;

    engine.enqueue(update);

    // Start the lighting thread
    engine.start();

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stop the engine
    engine.stop();

    // Verify that a mesh rebuild request was pushed
    auto request = meshQueue.tryPop();
    EXPECT_TRUE(request.has_value());
    if (request) {
        EXPECT_EQ(request->first, chunkPos);
    }
}

TEST(LightingDeferralTest, NoMeshRebuildWhenFlagFalse) {
    World world;
    LightEngine engine(world);

    // Create a mesh rebuild queue
    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Create a subchunk with a block
    BlockPos pos{8, 8, 8};
    world.setBlock(pos, BlockTypeId::fromName("minecraft:stone"));

    // Enqueue a lighting update WITHOUT triggerMeshRebuild
    LightingUpdate update;
    update.pos = pos;
    update.oldType = BlockTypeId::fromName("minecraft:stone");
    update.newType = AIR_BLOCK_TYPE;
    update.triggerMeshRebuild = false;  // Explicitly false

    engine.enqueue(update);

    // Start the lighting thread
    engine.start();

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Stop the engine
    engine.stop();

    // Verify that NO mesh rebuild request was pushed
    auto request = meshQueue.tryPop();
    EXPECT_FALSE(request.has_value());
}

// ============================================================================
// Lighting Correctness Tests - Reference Implementation Comparison
// ============================================================================

class LightingCorrectnessTest : public ::testing::Test {
protected:
    void SetUp() override {
        world_ = std::make_unique<World>();
        engine_ = std::make_unique<LightEngine>(*world_);

        // Increase max propagation distance for full light propagation
        // Default 256 is too low for a torch (light=14 affects ~2744+ blocks)
        engine_->setMaxPropagationDistance(16000);

        // Register test block types
        BlockType torch;
        torch.setNoCollision()
             .setOpaque(false)
             .setLightEmission(14)
             .setLightAttenuation(1)
             .setBlocksSkyLight(false);
        BlockRegistry::global().registerType("lighttest:torch", torch);

        BlockType stone;
        stone.setOpaque(true)
             .setLightEmission(0)
             .setLightAttenuation(15)
             .setBlocksSkyLight(true);
        BlockRegistry::global().registerType("lighttest:stone", stone);

        torch_ = BlockTypeId::fromName("lighttest:torch");
        stone_ = BlockTypeId::fromName("lighttest:stone");
    }

    // Reference implementation: compute expected block light using BFS from scratch
    // This is the "ground truth" - simple but correct
    std::unordered_map<BlockPos, uint8_t> computeExpectedBlockLight(
        const std::vector<std::pair<BlockPos, uint8_t>>& lightSources,
        const std::unordered_set<BlockPos>& opaqueBlocks,
        int32_t maxRange = 16
    ) {
        std::unordered_map<BlockPos, uint8_t> result;

        // BFS from each light source
        for (const auto& [sourcePos, emission] : lightSources) {
            std::queue<std::pair<BlockPos, uint8_t>> queue;
            queue.push({sourcePos, emission});

            while (!queue.empty()) {
                auto [pos, light] = queue.front();
                queue.pop();

                if (light == 0) continue;

                // Skip opaque blocks (light can't enter them)
                if (opaqueBlocks.count(pos) && pos != sourcePos) continue;

                // Update if this is higher than existing
                if (result[pos] < light) {
                    result[pos] = light;

                    // Propagate to neighbors (with attenuation of 1)
                    if (light > 1) {
                        static const BlockPos offsets[] = {
                            {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
                        };
                        for (const auto& offset : offsets) {
                            BlockPos neighbor{pos.x + offset.x, pos.y + offset.y, pos.z + offset.z};
                            // Don't propagate into opaque blocks
                            if (!opaqueBlocks.count(neighbor)) {
                                queue.push({neighbor, static_cast<uint8_t>(light - 1)});
                            }
                        }
                    }
                }
            }
        }

        return result;
    }

    // Get actual light from engine for a region
    std::unordered_map<BlockPos, uint8_t> getActualBlockLight(
        const BlockPos& center, int32_t radius
    ) {
        std::unordered_map<BlockPos, uint8_t> result;
        for (int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            for (int32_t y = center.y - radius; y <= center.y + radius; ++y) {
                for (int32_t z = center.z - radius; z <= center.z + radius; ++z) {
                    BlockPos pos{x, y, z};
                    uint8_t light = engine_->getBlockLight(pos);
                    if (light > 0) {
                        result[pos] = light;
                    }
                }
            }
        }
        return result;
    }

    // Compare expected vs actual, return list of mismatches
    std::vector<std::string> compareLighting(
        const std::unordered_map<BlockPos, uint8_t>& expected,
        const std::unordered_map<BlockPos, uint8_t>& actual,
        const BlockPos& center, int32_t radius
    ) {
        std::vector<std::string> mismatches;

        // Check all positions in range
        for (int32_t x = center.x - radius; x <= center.x + radius; ++x) {
            for (int32_t y = center.y - radius; y <= center.y + radius; ++y) {
                for (int32_t z = center.z - radius; z <= center.z + radius; ++z) {
                    BlockPos pos{x, y, z};
                    uint8_t exp = expected.count(pos) ? expected.at(pos) : 0;
                    uint8_t act = actual.count(pos) ? actual.at(pos) : 0;

                    if (exp != act) {
                        std::ostringstream ss;
                        ss << "At (" << x << "," << y << "," << z << "): "
                           << "expected=" << (int)exp << " actual=" << (int)act;
                        mismatches.push_back(ss.str());
                    }
                }
            }
        }

        return mismatches;
    }

    std::unique_ptr<World> world_;
    std::unique_ptr<LightEngine> engine_;
    BlockTypeId torch_;
    BlockTypeId stone_;
};

TEST_F(LightingCorrectnessTest, SingleTorchPropagation) {
    // Place a torch
    BlockPos torchPos{8, 8, 8};
    world_->setBlock(torchPos, torch_);
    engine_->onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torch_);

    // Debug: Check light at a few key positions
    std::cout << "=== DEBUG: Light values ===\n";
    std::cout << "At torch (8,8,8): " << (int)engine_->getBlockLight(torchPos) << "\n";
    std::cout << "At (9,8,8): " << (int)engine_->getBlockLight({9,8,8}) << "\n";
    std::cout << "At (7,8,8): " << (int)engine_->getBlockLight({7,8,8}) << "\n";
    std::cout << "At (0,8,8): " << (int)engine_->getBlockLight({0,8,8}) << "\n";
    std::cout << "At (-1,8,8): " << (int)engine_->getBlockLight({-1,8,8}) << "\n";

    // Check what blocks are at these positions
    std::cout << "Block at (9,8,8) isAir: " << world_->getBlock({9,8,8}).isAir() << "\n";
    std::cout << "Block at (-1,8,8) isAir: " << world_->getBlock({-1,8,8}).isAir() << "\n";

    // Compute expected
    std::vector<std::pair<BlockPos, uint8_t>> sources = {{torchPos, 14}};
    std::unordered_set<BlockPos> opaque;
    auto expected = computeExpectedBlockLight(sources, opaque);

    // Get actual
    auto actual = getActualBlockLight(torchPos, 15);

    // Compare
    auto mismatches = compareLighting(expected, actual, torchPos, 15);

    if (!mismatches.empty()) {
        std::cout << "SingleTorchPropagation mismatches:\n";
        for (size_t i = 0; i < std::min(mismatches.size(), size_t(10)); ++i) {
            std::cout << "  " << mismatches[i] << "\n";
        }
        if (mismatches.size() > 10) {
            std::cout << "  ... and " << (mismatches.size() - 10) << " more\n";
        }
    }
    EXPECT_TRUE(mismatches.empty());
}

TEST_F(LightingCorrectnessTest, TorchWithOneOpaqueBlock) {
    // Place a torch
    BlockPos torchPos{8, 8, 8};
    world_->setBlock(torchPos, torch_);
    engine_->onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torch_);

    // Place one opaque block next to it
    BlockPos stonePos{9, 8, 8};
    world_->setBlock(stonePos, stone_);
    engine_->onBlockPlaced(stonePos, AIR_BLOCK_TYPE, stone_);

    // Compute expected
    std::vector<std::pair<BlockPos, uint8_t>> sources = {{torchPos, 14}};
    std::unordered_set<BlockPos> opaque = {stonePos};
    auto expected = computeExpectedBlockLight(sources, opaque);

    // Get actual
    auto actual = getActualBlockLight(torchPos, 15);

    // Compare
    auto mismatches = compareLighting(expected, actual, torchPos, 15);

    if (!mismatches.empty()) {
        std::cout << "TorchWithOneOpaqueBlock mismatches:\n";
        for (const auto& m : mismatches) {
            std::cout << "  " << m << "\n";
        }
    }
    EXPECT_TRUE(mismatches.empty());
}

TEST_F(LightingCorrectnessTest, FullySurroundedTorch) {
    // Place a torch
    BlockPos torchPos{8, 8, 8};
    world_->setBlock(torchPos, torch_);
    engine_->onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torch_);

    // Surround with opaque blocks
    std::unordered_set<BlockPos> opaque;
    static const BlockPos offsets[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    for (const auto& offset : offsets) {
        BlockPos stonePos{torchPos.x + offset.x, torchPos.y + offset.y, torchPos.z + offset.z};
        world_->setBlock(stonePos, stone_);
        engine_->onBlockPlaced(stonePos, AIR_BLOCK_TYPE, stone_);
        opaque.insert(stonePos);
    }

    // Compute expected - torch is surrounded, no light escapes
    std::vector<std::pair<BlockPos, uint8_t>> sources = {{torchPos, 14}};
    auto expected = computeExpectedBlockLight(sources, opaque);

    // Get actual
    auto actual = getActualBlockLight(torchPos, 15);

    // Compare
    auto mismatches = compareLighting(expected, actual, torchPos, 15);

    if (!mismatches.empty()) {
        std::cout << "FullySurroundedTorch mismatches:\n";
        for (size_t i = 0; i < std::min(mismatches.size(), size_t(20)); ++i) {
            std::cout << "  " << mismatches[i] << "\n";
        }
        if (mismatches.size() > 20) {
            std::cout << "  ... and " << (mismatches.size() - 20) << " more\n";
        }
    }
    EXPECT_TRUE(mismatches.empty());
}

TEST_F(LightingCorrectnessTest, RemoveOpaqueBlockRestoresLight) {
    // Place a torch
    BlockPos torchPos{8, 8, 8};
    world_->setBlock(torchPos, torch_);
    engine_->onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torch_);

    // Place one opaque block
    BlockPos stonePos{9, 8, 8};
    world_->setBlock(stonePos, stone_);
    engine_->onBlockPlaced(stonePos, AIR_BLOCK_TYPE, stone_);

    // Now remove the opaque block
    world_->setBlock(stonePos, AIR_BLOCK_TYPE);
    engine_->onBlockRemoved(stonePos, stone_);

    // Compute expected - should be same as torch with no obstacles
    std::vector<std::pair<BlockPos, uint8_t>> sources = {{torchPos, 14}};
    std::unordered_set<BlockPos> opaque;  // No opaque blocks now
    auto expected = computeExpectedBlockLight(sources, opaque);

    // Get actual
    auto actual = getActualBlockLight(torchPos, 15);

    // Compare
    auto mismatches = compareLighting(expected, actual, torchPos, 15);

    if (!mismatches.empty()) {
        std::cout << "RemoveOpaqueBlockRestoresLight mismatches:\n";
        for (size_t i = 0; i < std::min(mismatches.size(), size_t(20)); ++i) {
            std::cout << "  " << mismatches[i] << "\n";
        }
        if (mismatches.size() > 20) {
            std::cout << "  ... and " << (mismatches.size() - 20) << " more\n";
        }
    }
    EXPECT_TRUE(mismatches.empty());
}

TEST_F(LightingCorrectnessTest, SurroundThenRemoveOneBlock) {
    // Place a torch
    BlockPos torchPos{8, 8, 8};
    world_->setBlock(torchPos, torch_);
    engine_->onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torch_);

    // Surround with opaque blocks
    std::vector<BlockPos> stonePositions;
    static const BlockPos offsets[] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
    };
    for (const auto& offset : offsets) {
        BlockPos stonePos{torchPos.x + offset.x, torchPos.y + offset.y, torchPos.z + offset.z};
        world_->setBlock(stonePos, stone_);
        engine_->onBlockPlaced(stonePos, AIR_BLOCK_TYPE, stone_);
        stonePositions.push_back(stonePos);
    }

    // Remove one block (the +X one)
    BlockPos removedPos = stonePositions[0];  // {9, 8, 8}
    world_->setBlock(removedPos, AIR_BLOCK_TYPE);
    engine_->onBlockRemoved(removedPos, stone_);

    // Compute expected - torch with 5 surrounding opaque blocks, one opening
    std::vector<std::pair<BlockPos, uint8_t>> sources = {{torchPos, 14}};
    std::unordered_set<BlockPos> opaque;
    for (size_t i = 1; i < stonePositions.size(); ++i) {
        opaque.insert(stonePositions[i]);
    }
    auto expected = computeExpectedBlockLight(sources, opaque);

    // Get actual
    auto actual = getActualBlockLight(torchPos, 15);

    // Compare
    auto mismatches = compareLighting(expected, actual, torchPos, 15);

    if (!mismatches.empty()) {
        std::cout << "SurroundThenRemoveOneBlock mismatches:\n";
        for (size_t i = 0; i < std::min(mismatches.size(), size_t(20)); ++i) {
            std::cout << "  " << mismatches[i] << "\n";
        }
        if (mismatches.size() > 20) {
            std::cout << "  ... and " << (mismatches.size() - 20) << " more\n";
        }
    }
    EXPECT_TRUE(mismatches.empty());
}

// ============================================================================
// Cross-Subchunk Boundary Mesh Rebuild Tests
// ============================================================================

// Test that when light changes at a subchunk boundary, both adjacent subchunks
// are marked for mesh rebuild (since faces in one subchunk may sample light
// from the other subchunk).
TEST(CrossSubchunkBoundaryTest, LightChangeAtYBoundaryMarksBothSubchunks) {
    World world;
    LightEngine engine(world);

    // Create a mesh rebuild queue to capture which chunks get marked
    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("boundary_test:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("boundary_test:torch");

    // Register a stone block type
    BlockType stone;
    stone.setOpaque(true)
         .setLightEmission(0)
         .setLightAttenuation(15)
         .setBlocksSkyLight(true);
    BlockRegistry::global().registerType("boundary_test:stone", stone);
    BlockTypeId stoneId = BlockTypeId::fromName("boundary_test:stone");

    // Setup: Place a light source at y=18 (in subchunk y=1, local y=2)
    // Use synchronous calls for initial setup (before starting thread)
    BlockPos torchPos{8, 18, 8};
    world.setBlock(torchPos, torchId);
    engine.onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torchId);

    // Place a floor of stone blocks at y=15 (top of subchunk y=0)
    // This blocks light from propagating further down
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            BlockPos floorPos{x, 15, z};
            world.setBlock(floorPos, stoneId);
        }
    }

    // Start the lighting thread
    engine.start();

    // Now break a block in the floor at y=15, exposing y=16
    // Use enqueue() to go through the async path which calls flushAffectedChunks
    BlockPos breakPos{8, 15, 8};  // At local y=15 in subchunk 0
    world.setBlock(breakPos, AIR_BLOCK_TYPE);

    // Enqueue via async path with triggerMeshRebuild=true
    LightingUpdate update;
    update.pos = breakPos;
    update.oldType = stoneId;
    update.newType = AIR_BLOCK_TYPE;
    update.triggerMeshRebuild = true;
    engine.enqueue(update);

    // Wait for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();

    // Collect all chunks that were marked for rebuild
    std::unordered_set<ChunkPos> rebuiltChunks;
    while (auto req = meshQueue.tryPop()) {
        rebuiltChunks.insert(req->first);
        std::cout << "Chunk marked for rebuild: ("
                  << req->first.x << ", " << req->first.y << ", " << req->first.z << ")\n";
    }

    // Check that subchunk at y=0 (containing the floor) is marked
    ChunkPos subchunk0{0, 0, 0};  // y=0-15
    ChunkPos subchunk1{0, 1, 0};  // y=16-31

    bool has_subchunk0 = rebuiltChunks.count(subchunk0) > 0;
    bool has_subchunk1 = rebuiltChunks.count(subchunk1) > 0;

    std::cout << "Subchunk 0 (y=0-15) marked: " << (has_subchunk0 ? "YES" : "NO") << "\n";
    std::cout << "Subchunk 1 (y=16-31) marked: " << (has_subchunk1 ? "YES" : "NO") << "\n";

    // The critical assertion: when breaking a block at the boundary (y=15),
    // the subchunk BELOW (y=0) needs to be rebuilt because its faces (the floor's
    // top faces, which are now exposed) sample light from y=16.
    // Similarly, subchunk 1 should be rebuilt because light propagates into it.
    EXPECT_TRUE(has_subchunk0) << "Subchunk 0 should be marked for rebuild (floor faces sample light from y=16)";
    EXPECT_TRUE(has_subchunk1) << "Subchunk 1 should be marked for rebuild (light propagates there)";
}

// Test light change at X boundary
TEST(CrossSubchunkBoundaryTest, LightChangeAtXBoundaryMarksBothSubchunks) {
    World world;
    LightEngine engine(world);

    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Register torch
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("boundary_test_x:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("boundary_test_x:torch");

    // Place torch at x=16, which is local x=0 in chunk (1, 0, 0)
    // Light will propagate to x=15 (local x=15 in chunk (0, 0, 0))
    BlockPos torchPos{16, 8, 8};
    world.setBlock(torchPos, torchId);

    // Use async path
    engine.start();

    LightingUpdate update;
    update.pos = torchPos;
    update.oldType = AIR_BLOCK_TYPE;
    update.newType = torchId;
    update.triggerMeshRebuild = true;
    engine.enqueue(update);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();

    // Collect marked chunks
    std::unordered_set<ChunkPos> rebuiltChunks;
    while (auto req = meshQueue.tryPop()) {
        rebuiltChunks.insert(req->first);
    }

    ChunkPos chunk0{0, 0, 0};  // x=0-15
    ChunkPos chunk1{1, 0, 0};  // x=16-31

    bool has_chunk0 = rebuiltChunks.count(chunk0) > 0;
    bool has_chunk1 = rebuiltChunks.count(chunk1) > 0;

    std::cout << "X boundary test:\n";
    std::cout << "  Chunk (0,0,0) marked: " << (has_chunk0 ? "YES" : "NO") << "\n";
    std::cout << "  Chunk (1,0,0) marked: " << (has_chunk1 ? "YES" : "NO") << "\n";

    // Chunk 1 should definitely be marked (torch is there)
    EXPECT_TRUE(has_chunk1) << "Chunk (1,0,0) should be marked (contains torch)";

    // Chunk 0 should be marked because light at x=16 affects faces at x=15
    // which are in chunk 0 but sample light from x=16
    EXPECT_TRUE(has_chunk0) << "Chunk (0,0,0) should be marked (faces at x=15 sample light from x=16)";
}

// Test the specific scenario: break block at y=16 (boundary), verify floor in subchunk 0 gets rebuilt
TEST(CrossSubchunkBoundaryTest, BreakBlockAtSubchunkBoundary) {
    World world;
    LightEngine engine(world);

    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Register blocks
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("boundary_test_break:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("boundary_test_break:torch");

    BlockType stone;
    stone.setOpaque(true)
         .setLightEmission(0)
         .setLightAttenuation(15);
    BlockRegistry::global().registerType("boundary_test_break:stone", stone);
    BlockTypeId stoneId = BlockTypeId::fromName("boundary_test_break:stone");

    // Place torch at y=20 (in subchunk 1)
    BlockPos torchPos{8, 20, 8};
    world.setBlock(torchPos, torchId);

    // Place stone at y=16 (local y=0 in subchunk 1 - at the boundary)
    // This is blocking light from reaching subchunk 0
    BlockPos stonePos{8, 16, 8};
    world.setBlock(stonePos, stoneId);

    // Initial light propagation (synchronous, before thread starts)
    engine.onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torchId);
    engine.onBlockPlaced(stonePos, AIR_BLOCK_TYPE, stoneId);

    // Verify light doesn't reach y=15 (blocked by stone)
    EXPECT_EQ(engine.getBlockLight({8, 15, 8}), 0) << "Light should be blocked at y=15";

    // Start the lighting thread
    engine.start();

    // Now break the stone at y=16 - light should flood down
    // Use async path via enqueue
    world.setBlock(stonePos, AIR_BLOCK_TYPE);

    LightingUpdate update;
    update.pos = stonePos;
    update.oldType = stoneId;
    update.newType = AIR_BLOCK_TYPE;
    update.triggerMeshRebuild = true;
    engine.enqueue(update);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();

    // Light should now reach y=15
    uint8_t lightAtY15 = engine.getBlockLight({8, 15, 8});
    std::cout << "Light at y=15 after breaking stone: " << (int)lightAtY15 << "\n";
    EXPECT_GT(lightAtY15, 0) << "Light should propagate to y=15 after breaking stone";

    // Check which subchunks were marked
    std::unordered_set<ChunkPos> rebuiltChunks;
    while (auto req = meshQueue.tryPop()) {
        rebuiltChunks.insert(req->first);
        std::cout << "Chunk marked: (" << req->first.x << ", " << req->first.y << ", " << req->first.z << ")\n";
    }

    ChunkPos subchunk0{0, 0, 0};  // y=0-15
    ChunkPos subchunk1{0, 1, 0};  // y=16-31

    // The key test: subchunk 0 must be marked even though we broke a block in subchunk 1
    // because faces in subchunk 0 (e.g., top face of block at y=15) sample light from y=16
    EXPECT_TRUE(rebuiltChunks.count(subchunk0) > 0)
        << "Subchunk 0 MUST be marked when light changes at y=16 (faces at y=15 sample from y=16)";
    EXPECT_TRUE(rebuiltChunks.count(subchunk1) > 0)
        << "Subchunk 1 should be marked (stone was removed there)";
}

// Test the exact demo scenario: break block in floor (same subchunk, not at boundary)
// This tests the case where the broken block and the floor below are in the SAME subchunk
TEST(CrossSubchunkBoundaryTest, BreakBlockInFloorSameSubchunk) {
    World world;
    LightEngine engine(world);

    MeshRebuildQueue meshQueue(mergeMeshRebuildRequest);
    engine.setMeshRebuildQueue(&meshQueue);

    // Register a torch block type
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("floor_test:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("floor_test:torch");

    BlockType stone;
    stone.setOpaque(true)
         .setLightEmission(0)
         .setLightAttenuation(15);
    BlockRegistry::global().registerType("floor_test:stone", stone);
    BlockTypeId stoneId = BlockTypeId::fromName("floor_test:stone");

    // Create a floor at y=4 and y=5 (both in subchunk 0)
    // Place stone floor blocks
    for (int x = 0; x < 10; ++x) {
        for (int z = 0; z < 10; ++z) {
            world.setBlock({x, 4, z}, stoneId);  // Bottom of floor
            world.setBlock({x, 5, z}, stoneId);  // Top layer of floor (will break one)
        }
    }

    // Place torch at y=6, near where we'll break the block
    // This is in the air just above the floor
    BlockPos torchPos{5, 6, 5};
    world.setBlock(torchPos, torchId);

    // Initial light propagation (synchronous)
    engine.onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torchId);

    // Verify light is above the floor (y=6) but blocked by floor (y=5 is solid)
    uint8_t lightAtY6 = engine.getBlockLight({5, 6, 5});  // Should be 14
    uint8_t lightAtY5Block = engine.getBlockLight({4, 5, 4});  // Inside stone, should be 0
    std::cout << "Light at y=6 (torch position): " << (int)lightAtY6 << "\n";
    std::cout << "Light inside floor block (y=5): " << (int)lightAtY5Block << "\n";

    EXPECT_EQ(lightAtY6, 14) << "Torch should emit light level 14";

    // Start the lighting thread
    engine.start();

    // Now break a floor block at y=5 (NOT at a subchunk boundary)
    // The block below at y=4 should have its top face (PosY) exposed
    BlockPos breakPos{4, 5, 4};
    world.setBlock(breakPos, AIR_BLOCK_TYPE);

    LightingUpdate update;
    update.pos = breakPos;
    update.oldType = stoneId;
    update.newType = AIR_BLOCK_TYPE;
    // Match the demo's behavior: shouldDefer = false means triggerMeshRebuild = false
    // The lighting thread should still mark chunks via recordAffectedChunk during propagation
    update.triggerMeshRebuild = false;
    engine.enqueue(update);

    // Wait for lighting to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    engine.stop();

    // Check light at the broken position - should have light from the torch
    uint8_t lightAtBroken = engine.getBlockLight(breakPos);
    std::cout << "Light at broken block (y=5): " << (int)lightAtBroken << "\n";
    EXPECT_GT(lightAtBroken, 0) << "Light should propagate into the hole";

    // The floor at y=4 below should sample light from y=5 (where we broke the block)
    // When we rebuild the mesh, the face at y=4 looking up should use light from y=5

    // Drain the mesh queue and check which chunks were marked
    std::unordered_set<ChunkPos> rebuiltChunks;
    std::vector<std::pair<ChunkPos, uint64_t>> rebuiltChunksWithLightVersion;
    while (auto req = meshQueue.tryPop()) {
        rebuiltChunks.insert(req->first);
        rebuiltChunksWithLightVersion.push_back({req->first, req->second.targetLightVersion});
        std::cout << "Chunk marked: (" << req->first.x << ", " << req->first.y << ", " << req->first.z
                  << ") lightVersion=" << req->second.targetLightVersion << "\n";
    }

    ChunkPos subchunk0{0, 0, 0};  // y=0-15 (contains both floor at y=4 and broken block at y=5)

    // The subchunk must be marked for rebuild (light changed inside it)
    EXPECT_TRUE(rebuiltChunks.count(subchunk0) > 0)
        << "Subchunk 0 should be marked (light changed at y=5 inside this subchunk)";

    // Verify the light version in the request is AFTER lighting was updated
    SubChunk* subchunk = world.getSubChunk(subchunk0);
    if (subchunk) {
        uint64_t currentLightVersion = subchunk->lightVersion();
        std::cout << "Current light version of subchunk 0: " << currentLightVersion << "\n";

        // Check that we got a rebuild request with the updated light version
        bool hasUpdatedRequest = false;
        for (const auto& [pos, lightVer] : rebuiltChunksWithLightVersion) {
            if (pos == subchunk0 && lightVer >= currentLightVersion) {
                hasUpdatedRequest = true;
                break;
            }
        }
        EXPECT_TRUE(hasUpdatedRequest)
            << "Should have a rebuild request with light version >= " << currentLightVersion;
    }
}

// Test that mesh building actually uses the correct light values
// This simulates the exact scenario where floor face should be lit after breaking a block
TEST(CrossSubchunkBoundaryTest, MeshBuildsWithCorrectLightValues) {
    World world;
    LightEngine engine(world);

    // Register block types
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("mesh_light_test:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("mesh_light_test:torch");

    BlockType stone;
    stone.setOpaque(true)
         .setLightEmission(0)
         .setLightAttenuation(15);
    BlockRegistry::global().registerType("mesh_light_test:stone", stone);
    BlockTypeId stoneId = BlockTypeId::fromName("mesh_light_test:stone");

    // Create a simple floor at y=4 and y=5
    for (int x = 3; x <= 7; ++x) {
        for (int z = 3; z <= 7; ++z) {
            world.setBlock({x, 4, z}, stoneId);  // Bottom of floor
            world.setBlock({x, 5, z}, stoneId);  // Top layer of floor (will break one)
        }
    }

    // Place torch above floor
    BlockPos torchPos{5, 6, 5};
    world.setBlock(torchPos, torchId);

    // Synchronous light propagation for torch
    engine.onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torchId);

    // Break the floor block at (5, 5, 5)
    BlockPos breakPos{5, 5, 5};
    world.setBlock(breakPos, AIR_BLOCK_TYPE);

    // Process light update synchronously (simulate what the lighting thread does)
    engine.onBlockRemoved(breakPos, stoneId);

    // Now check the light at the broken position
    uint8_t lightAtBroken = engine.getBlockLight(breakPos);
    std::cout << "Light at broken block (5, 5, 5): " << (int)lightAtBroken << "\n";
    EXPECT_GT(lightAtBroken, 10) << "Light should propagate from torch into the hole";

    // Create a light provider that uses the engine
    BlockLightProvider lightProvider = [&engine](const BlockPos& pos) -> uint8_t {
        return engine.getCombinedLight(pos);
    };

    // Build the mesh for subchunk 0,0,0
    ChunkPos chunkPos{0, 0, 0};
    SubChunk* subchunk = world.getSubChunk(chunkPos);
    ASSERT_NE(subchunk, nullptr);

    // Build mesh with smooth lighting
    MeshBuilder builder;
    builder.setSmoothLighting(true);
    builder.setLightProvider(lightProvider);

    BlockOpaqueProvider opaqueProvider = [&world](const BlockPos& pos) -> bool {
        BlockTypeId type = world.getBlock(pos);
        return type != AIR_BLOCK_TYPE;
    };
    BlockTextureProvider textureProvider = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);  // Default UVs
    };

    MeshData mesh = builder.buildSubChunkMesh(*subchunk, chunkPos, opaqueProvider, textureProvider);

    // Find the PosY face of the block at (5, 4, 5) - this is the floor face below the hole
    // It should sample light from (5, 5, 5) which is now lit
    //
    // In mesh coordinates, the face is at local position (5, 4, 5) with +Y normal
    // The vertex positions would be:
    //   (5, 5, 5), (6, 5, 5), (6, 5, 6), (5, 5, 6) (corners of top face of block at y=4)
    //
    // Look for vertices with position.y = 5.0 (top face of block at y=4) and normal.y = 1.0

    bool foundFloorFace = false;
    float floorFaceLight = 0.0f;

    for (const auto& vertex : mesh.vertices) {
        // Check for PosY face at (5, 4, 5)
        // Vertex positions for top face are at y = 4 + 1 = 5.0
        if (std::abs(vertex.position.y - 5.0f) < 0.1f &&
            std::abs(vertex.normal.y - 1.0f) < 0.1f &&
            vertex.position.x >= 4.9f && vertex.position.x <= 6.1f &&
            vertex.position.z >= 4.9f && vertex.position.z <= 6.1f) {
            foundFloorFace = true;
            floorFaceLight = std::max(floorFaceLight, vertex.light);
            std::cout << "Floor face vertex at (" << vertex.position.x << ", "
                      << vertex.position.y << ", " << vertex.position.z
                      << ") light=" << vertex.light << "\n";
        }
    }

    EXPECT_TRUE(foundFloorFace) << "Should find the floor face at (5, 4, 5)";
    EXPECT_GT(floorFaceLight, 0.1f) << "Floor face should have light > 0.1 (was " << floorFaceLight << ")";

    std::cout << "Floor face max light: " << floorFaceLight << "\n";
}

// Test that simulates the exact demo scenario: two mesh builds, one before and one after light propagation
TEST(CrossSubchunkBoundaryTest, MeshBeforeAndAfterLightPropagation) {
    World world;
    LightEngine engine(world);

    // Register block types
    BlockType torch;
    torch.setNoCollision()
         .setOpaque(false)
         .setLightEmission(14)
         .setLightAttenuation(1)
         .setBlocksSkyLight(false);
    BlockRegistry::global().registerType("timing_test:torch", torch);
    BlockTypeId torchId = BlockTypeId::fromName("timing_test:torch");

    BlockType stone;
    stone.setOpaque(true)
         .setLightEmission(0)
         .setLightAttenuation(15);
    BlockRegistry::global().registerType("timing_test:stone", stone);
    BlockTypeId stoneId = BlockTypeId::fromName("timing_test:stone");

    // Create a floor
    for (int x = 3; x <= 7; ++x) {
        for (int z = 3; z <= 7; ++z) {
            world.setBlock({x, 4, z}, stoneId);
            world.setBlock({x, 5, z}, stoneId);
        }
    }

    // Place torch above floor
    BlockPos torchPos{5, 6, 5};
    world.setBlock(torchPos, torchId);
    engine.onBlockPlaced(torchPos, AIR_BLOCK_TYPE, torchId);

    // Break the floor block at (5, 5, 5) - this removes the block from the world
    BlockPos breakPos{5, 5, 5};
    world.setBlock(breakPos, AIR_BLOCK_TYPE);

    // FIRST MESH BUILD: Before onBlockRemoved is called (light not yet propagated)
    // This simulates what happens when the world setBlock pushes a rebuild before lighting
    BlockLightProvider lightProvider = [&engine](const BlockPos& pos) -> uint8_t {
        return engine.getCombinedLight(pos);
    };

    ChunkPos chunkPos{0, 0, 0};
    SubChunk* subchunk = world.getSubChunk(chunkPos);
    ASSERT_NE(subchunk, nullptr);

    MeshBuilder builder;
    builder.setSmoothLighting(true);
    builder.setLightProvider(lightProvider);

    BlockOpaqueProvider opaqueProvider = [&world](const BlockPos& pos) -> bool {
        BlockTypeId type = world.getBlock(pos);
        return type != AIR_BLOCK_TYPE;
    };
    BlockTextureProvider textureProvider = [](BlockTypeId, Face) {
        return glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };

    // Check light BEFORE onBlockRemoved
    uint8_t lightBeforePropagation = engine.getBlockLight(breakPos);
    std::cout << "Light at (5,5,5) BEFORE onBlockRemoved: " << (int)lightBeforePropagation << "\n";

    MeshData mesh1 = builder.buildSubChunkMesh(*subchunk, chunkPos, opaqueProvider, textureProvider);

    float mesh1FloorLight = 0.0f;
    for (const auto& vertex : mesh1.vertices) {
        if (std::abs(vertex.position.y - 5.0f) < 0.1f &&
            std::abs(vertex.normal.y - 1.0f) < 0.1f &&
            vertex.position.x >= 4.9f && vertex.position.x <= 6.1f &&
            vertex.position.z >= 4.9f && vertex.position.z <= 6.1f) {
            mesh1FloorLight = std::max(mesh1FloorLight, vertex.light);
        }
    }
    std::cout << "FIRST mesh floor face light: " << mesh1FloorLight << "\n";

    // NOW propagate light (simulate lighting thread processing)
    engine.onBlockRemoved(breakPos, stoneId);

    // Check light AFTER onBlockRemoved
    uint8_t lightAfterPropagation = engine.getBlockLight(breakPos);
    std::cout << "Light at (5,5,5) AFTER onBlockRemoved: " << (int)lightAfterPropagation << "\n";

    // SECOND MESH BUILD: After light propagation
    MeshData mesh2 = builder.buildSubChunkMesh(*subchunk, chunkPos, opaqueProvider, textureProvider);

    float mesh2FloorLight = 0.0f;
    for (const auto& vertex : mesh2.vertices) {
        if (std::abs(vertex.position.y - 5.0f) < 0.1f &&
            std::abs(vertex.normal.y - 1.0f) < 0.1f &&
            vertex.position.x >= 4.9f && vertex.position.x <= 6.1f &&
            vertex.position.z >= 4.9f && vertex.position.z <= 6.1f) {
            mesh2FloorLight = std::max(mesh2FloorLight, vertex.light);
        }
    }
    std::cout << "SECOND mesh floor face light: " << mesh2FloorLight << "\n";

    // The first mesh should have light=0 (or very low) because light hasn't propagated
    EXPECT_LT(mesh1FloorLight, 0.1f) << "First mesh should have low light (before propagation)";

    // The second mesh should have light > 0.1
    EXPECT_GT(mesh2FloorLight, 0.1f) << "Second mesh should have light (after propagation)";

    // Verify the difference
    EXPECT_GT(mesh2FloorLight, mesh1FloorLight + 0.1f)
        << "Second mesh should be significantly brighter than first";

    // Compare floor lighting to side face lighting
    // Check the NegX face of block at (6, 5, 5) - this is a "side" face of the hole
    // Vertex positions would have x=6.0, y=5-6, z=5-6, normal.x=-1

    float sideFaceLight = 0.0f;
    int sideFaceCount = 0;
    for (const auto& vertex : mesh2.vertices) {
        // NegX faces have normal.x = -1
        if (std::abs(vertex.normal.x - (-1.0f)) < 0.1f &&
            std::abs(vertex.position.x - 6.0f) < 0.1f &&
            vertex.position.y >= 4.9f && vertex.position.y <= 6.1f &&
            vertex.position.z >= 4.9f && vertex.position.z <= 6.1f) {
            sideFaceLight = std::max(sideFaceLight, vertex.light);
            sideFaceCount++;
        }
    }

    std::cout << "Side face (NegX of (6,5,5)) light: " << sideFaceLight
              << " (vertices found: " << sideFaceCount << ")\n";
    std::cout << "Floor face light: " << mesh2FloorLight << "\n";
    std::cout << "Ratio (side/floor): " << (mesh2FloorLight > 0 ? sideFaceLight / mesh2FloorLight : 0) << "\n";
}
