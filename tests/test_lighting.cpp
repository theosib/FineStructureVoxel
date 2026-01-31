#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "finevox/light_data.hpp"
#include "finevox/light_engine.hpp"
#include "finevox/block_type.hpp"
#include "finevox/world.hpp"
#include "finevox/chunk_column.hpp"
#include "finevox/subchunk.hpp"

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
