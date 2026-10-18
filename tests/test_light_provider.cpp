#include <gtest/gtest.h>
#include "finevox/core/light_provider.hpp"
#include "finevox/core/light_engine.hpp"
#include "finevox/core/world.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"

using namespace finevox;

// ============================================================================
// Mock light providers for testing
// ============================================================================

class MockLightProvider : public LightProvider {
public:
    MockLightProvider(std::string_view n, int32_t pri = 100)
        : name_(n), priority_(pri) {}

    std::string_view name() const override { return name_; }
    int32_t priority() const override { return priority_; }

    uint8_t getEmission(BlockTypeId) const override { return emission_; }
    uint8_t getAttenuation(BlockTypeId) const override { return attenuation_; }
    float getLogAttenuation(const BlockCoord&) const override { return logAtten_; }
    bool blocksSkyLight(BlockTypeId) const override { return blocksSky_; }

    uint8_t emission_ = 0;
    uint8_t attenuation_ = 0;
    float logAtten_ = 0.0f;
    bool blocksSky_ = false;

private:
    std::string name_;
    int32_t priority_;
};

// ============================================================================
// LightProvider interface tests
// ============================================================================

TEST(LightProviderTest, DefaultPriority) {
    class MinimalProvider : public LightProvider {
    public:
        std::string_view name() const override { return "Minimal"; }
        uint8_t getEmission(BlockTypeId) const override { return 0; }
        uint8_t getAttenuation(BlockTypeId) const override { return 0; }
        bool blocksSkyLight(BlockTypeId) const override { return false; }
    };

    MinimalProvider provider;
    EXPECT_EQ(provider.priority(), 100);
}

TEST(LightProviderTest, DefaultLogAttenuation) {
    class MinimalProvider : public LightProvider {
    public:
        std::string_view name() const override { return "Minimal"; }
        uint8_t getEmission(BlockTypeId) const override { return 0; }
        uint8_t getAttenuation(BlockTypeId) const override { return 0; }
        bool blocksSkyLight(BlockTypeId) const override { return false; }
    };

    MinimalProvider provider;
    EXPECT_FLOAT_EQ(provider.getLogAttenuation(BlockCoord{0, 0, 0}), 0.0f);
}

TEST(LightProviderTest, SharedFromThis) {
    auto provider = std::make_shared<MockLightProvider>("Test");
    auto shared = provider->shared_from_this();
    EXPECT_EQ(shared.get(), provider.get());
}

// ============================================================================
// Provider management on LightEngine
// ============================================================================

TEST(LightProviderTest, AddProviderSortsByPriority) {
    World world;
    LightEngine engine(world);

    auto high = std::make_shared<MockLightProvider>("High", 200);
    auto low = std::make_shared<MockLightProvider>("Low", 10);
    auto mid = std::make_shared<MockLightProvider>("Mid", 100);

    // Add in wrong order
    engine.addLightProvider(high);
    engine.addLightProvider(low);
    engine.addLightProvider(mid);

    const auto& providers = engine.lightProviders();
    ASSERT_EQ(providers.size(), 3u);
    EXPECT_EQ(providers[0]->name(), "Low");
    EXPECT_EQ(providers[1]->name(), "Mid");
    EXPECT_EQ(providers[2]->name(), "High");
}

TEST(LightProviderTest, RemoveProvider) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    auto b = std::make_shared<MockLightProvider>("B", 1);

    engine.addLightProvider(a);
    engine.addLightProvider(b);
    EXPECT_EQ(engine.lightProviders().size(), 2u);

    engine.removeLightProvider(a);
    EXPECT_EQ(engine.lightProviders().size(), 1u);
    EXPECT_EQ(engine.lightProviders()[0]->name(), "B");
}

TEST(LightProviderTest, RemoveNonExistentProviderNoOp) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A");
    auto b = std::make_shared<MockLightProvider>("B");

    engine.addLightProvider(a);
    engine.removeLightProvider(b);  // Not added
    EXPECT_EQ(engine.lightProviders().size(), 1u);
}

// ============================================================================
// Combined query tests
// ============================================================================

TEST(LightProviderTest, CombinedEmissionTakesMax) {
    World world;
    LightEngine engine(world);

    auto low = std::make_shared<MockLightProvider>("Low", 0);
    low->emission_ = 5;
    auto high = std::make_shared<MockLightProvider>("High", 1);
    high->emission_ = 12;
    auto mid = std::make_shared<MockLightProvider>("Mid", 2);
    mid->emission_ = 8;

    engine.addLightProvider(low);
    engine.addLightProvider(high);
    engine.addLightProvider(mid);

    EXPECT_EQ(engine.queryCombinedEmission(AIR_BLOCK_TYPE), 12);
}

TEST(LightProviderTest, CombinedEmissionSingleProvider) {
    World world;
    LightEngine engine(world);

    auto provider = std::make_shared<MockLightProvider>("Only", 0);
    provider->emission_ = 7;

    engine.addLightProvider(provider);
    EXPECT_EQ(engine.queryCombinedEmission(AIR_BLOCK_TYPE), 7);
}

TEST(LightProviderTest, CombinedAttenuationSumsClamped) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    a->attenuation_ = 3;
    auto b = std::make_shared<MockLightProvider>("B", 1);
    b->attenuation_ = 5;

    engine.addLightProvider(a);
    engine.addLightProvider(b);

    EXPECT_EQ(engine.queryCombinedAttenuation(AIR_BLOCK_TYPE), 8);
}

TEST(LightProviderTest, CombinedAttenuationClampedTo15) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    a->attenuation_ = 10;
    auto b = std::make_shared<MockLightProvider>("B", 1);
    b->attenuation_ = 10;

    engine.addLightProvider(a);
    engine.addLightProvider(b);

    EXPECT_EQ(engine.queryCombinedAttenuation(AIR_BLOCK_TYPE), 15);
}

TEST(LightProviderTest, CombinedLogAttenuationMultiplicative) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    a->logAtten_ = 0.8f;
    auto b = std::make_shared<MockLightProvider>("B", 1);
    b->logAtten_ = 0.5f;

    engine.addLightProvider(a);
    engine.addLightProvider(b);

    EXPECT_FLOAT_EQ(engine.queryCombinedLogAttenuation(BlockCoord{0, 0, 0}), 0.4f);
}

TEST(LightProviderTest, CombinedLogAttenuationSkipsZero) {
    World world;
    LightEngine engine(world);

    auto noAtten = std::make_shared<MockLightProvider>("NoAtten", 0);
    noAtten->logAtten_ = 0.0f;  // Not applicable
    auto hasAtten = std::make_shared<MockLightProvider>("HasAtten", 1);
    hasAtten->logAtten_ = 0.7f;

    engine.addLightProvider(noAtten);
    engine.addLightProvider(hasAtten);

    // Should skip the 0.0f and return just 0.7f
    EXPECT_FLOAT_EQ(engine.queryCombinedLogAttenuation(BlockCoord{0, 0, 0}), 0.7f);
}

TEST(LightProviderTest, CombinedLogAttenuationAllZero) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    auto b = std::make_shared<MockLightProvider>("B", 1);

    engine.addLightProvider(a);
    engine.addLightProvider(b);

    EXPECT_FLOAT_EQ(engine.queryCombinedLogAttenuation(BlockCoord{0, 0, 0}), 0.0f);
}

TEST(LightProviderTest, CombinedSkyBlockOR) {
    World world;
    LightEngine engine(world);

    auto noBlock = std::make_shared<MockLightProvider>("NoBlock", 0);
    noBlock->blocksSky_ = false;
    auto blocks = std::make_shared<MockLightProvider>("Blocks", 1);
    blocks->blocksSky_ = true;

    engine.addLightProvider(noBlock);
    engine.addLightProvider(blocks);

    EXPECT_TRUE(engine.queryCombinedBlocksSkyLight(AIR_BLOCK_TYPE));
}

TEST(LightProviderTest, CombinedSkyBlockAllFalse) {
    World world;
    LightEngine engine(world);

    auto a = std::make_shared<MockLightProvider>("A", 0);
    auto b = std::make_shared<MockLightProvider>("B", 1);

    engine.addLightProvider(a);
    engine.addLightProvider(b);

    EXPECT_FALSE(engine.queryCombinedBlocksSkyLight(AIR_BLOCK_TYPE));
}

// ============================================================================
// Fallback behavior (no providers registered)
// ============================================================================

TEST(LightProviderTest, EmissionFallbackToInternal) {
    World world;
    LightEngine engine(world);

    // No providers - should use internal getLightEmission()
    // Air has 0 emission
    EXPECT_EQ(engine.queryCombinedEmission(AIR_BLOCK_TYPE), 0);
}

TEST(LightProviderTest, AttenuationFallbackToInternal) {
    World world;
    LightEngine engine(world);

    // No providers - should use internal getAttenuation()
    // Air has attenuation of 1
    EXPECT_EQ(engine.queryCombinedAttenuation(AIR_BLOCK_TYPE), 1);
}

TEST(LightProviderTest, SkyBlockFallbackToInternal) {
    World world;
    LightEngine engine(world);

    // No providers - should use internal blocksSkyLight()
    // Air doesn't block sky light
    EXPECT_FALSE(engine.queryCombinedBlocksSkyLight(AIR_BLOCK_TYPE));
}

// ============================================================================
// Built-in provider factory tests
// ============================================================================

TEST(LightProviderTest, BlockLightProviderCreation) {
    auto provider = createBlockLightProvider();
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "BlockLight");
    EXPECT_EQ(provider->priority(), 0);
}

TEST(LightProviderTest, BlockLightProviderAirProperties) {
    auto provider = createBlockLightProvider();

    EXPECT_EQ(provider->getEmission(AIR_BLOCK_TYPE), 0);
    EXPECT_EQ(provider->getAttenuation(AIR_BLOCK_TYPE), 1);
    EXPECT_FALSE(provider->blocksSkyLight(AIR_BLOCK_TYPE));
}

TEST(LightProviderTest, FluidLightProviderCreation) {
    World world;
    auto provider = createFluidLightProvider(world);
    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->name(), "FluidLight");
    EXPECT_EQ(provider->priority(), 50);
}

TEST(LightProviderTest, FluidLightProviderNoFluidNoAttenuation) {
    World world;
    auto provider = createFluidLightProvider(world);

    // No fluid at position - log attenuation should be 0
    EXPECT_FLOAT_EQ(provider->getLogAttenuation(BlockCoord{0, 64, 0}), 0.0f);
}

TEST(LightProviderTest, FluidLightProviderReturnsZeroEmission) {
    World world;
    auto provider = createFluidLightProvider(world);

    // Fluid emission is handled separately, provider always returns 0
    EXPECT_EQ(provider->getEmission(AIR_BLOCK_TYPE), 0);
}

TEST(LightProviderTest, FluidLightProviderDoesNotBlockSky) {
    World world;
    auto provider = createFluidLightProvider(world);

    EXPECT_FALSE(provider->blocksSkyLight(AIR_BLOCK_TYPE));
}

// ============================================================================
// Phase R0 Tests: LightProvider & LightingQueue::enqueue wiring in LightEngine
// ============================================================================

TEST(LightProviderTest, CustomProviderDrivesLightEnginePropagation) {
    World world;
    LightEngine engine(world);

    // Register a custom block type with 0 emission in BlockRegistry
    BlockType crystal;
    crystal.setOpaque(false)
           .setTransparent(true)
           .setLightEmission(0)
           .setLightAttenuation(1)
           .setBlocksSkyLight(false);
    BlockTypeId crystalId = BlockTypeId::fromName("r0light:magic_crystal");
    BlockRegistry::global().registerType(crystalId, crystal);

    // Seal a 7x7x7 chamber of opaque stone around (8, 8, 8)
    BlockTypeId stoneId = BlockTypeId::fromName("r0light:stone");
    BlockType stone;
    stone.setOpaque(true).setLightAttenuation(15).setBlocksSkyLight(true);
    BlockRegistry::global().registerType(stoneId, stone);
    for (int x = 5; x <= 11; ++x) {
        for (int y = 5; y <= 11; ++y) {
            for (int z = 5; z <= 11; ++z) {
                if (x == 5 || x == 11 || y == 5 || y == 11 || z == 5 || z == 11) {
                    world.setBlock({x, y, z}, stoneId);
                }
            }
        }
    }

    // Negative control: without custom LightProvider, placing crystal emits 0 light
    BlockCoord center{8, 8, 8};
    world.setBlock(center, crystalId);
    engine.onBlockPlaced(center, AIR_BLOCK_TYPE, crystalId);
    EXPECT_EQ(engine.getBlockLight(center), 0);

    // Add a custom LightProvider that makes crystalId emit 12 light
    class CrystalLightProvider : public LightProvider {
    public:
        explicit CrystalLightProvider(BlockTypeId target) : target_(target) {}
        std::string_view name() const override { return "CrystalLight"; }
        uint8_t getEmission(BlockTypeId bt) const override {
            return bt == target_ ? 12 : 0;
        }
        uint8_t getAttenuation(BlockTypeId bt) const override {
            if (bt.isAir()) return 1;
            return BlockRegistry::global().getType(bt).lightAttenuation();
        }
        bool blocksSkyLight(BlockTypeId bt) const override {
            if (bt.isAir()) return false;
            return BlockRegistry::global().getType(bt).blocksSkyLight();
        }
    private:
        BlockTypeId target_;
    };

    engine.addLightProvider(std::make_shared<CrystalLightProvider>(crystalId));
    engine.onBlockPlaced(center, AIR_BLOCK_TYPE, crystalId);

    // Now the custom provider drives LightEngine propagation!
    EXPECT_EQ(engine.getBlockLight(center), 12);
    EXPECT_EQ(engine.getBlockLight({9, 8, 8}), 11);

    // Removing the crystal removes the light
    world.setBlock(center, AIR_BLOCK_TYPE);
    engine.onBlockRemoved(center, crystalId);
    EXPECT_EQ(engine.getBlockLight(center), 0);
    EXPECT_EQ(engine.getBlockLight({9, 8, 8}), 0);
}

TEST(LightProviderTest, LightingQueueCoalescesPreservingOriginalOldTypeAndTriggerFlag) {
    LightingQueue queue;
    BlockCoord pos{4, 4, 4};
    BlockTypeId torch = BlockTypeId::fromName("r0light:torch");
    BlockTypeId stone = BlockTypeId::fromName("r0light:stone");
    BlockTypeId dirt = BlockTypeId::fromName("r0light:dirt");

    // Update 1: torch -> stone, triggerMeshRebuild = true
    LightingUpdate u1;
    u1.pos = pos;
    u1.oldType = torch;
    u1.newType = stone;
    u1.triggerMeshRebuild = true;
    queue.enqueue(u1);

    // Update 2 at same position: stone -> dirt, triggerMeshRebuild = false
    LightingUpdate u2;
    u2.pos = pos;
    u2.oldType = stone;
    u2.newType = dirt;
    u2.triggerMeshRebuild = false;
    queue.enqueue(u2);

    EXPECT_EQ(queue.size(), 1u);
    auto batch = queue.tryDequeueBatch(10);
    ASSERT_EQ(batch.size(), 1u);
    EXPECT_EQ(batch[0].pos, pos);
    EXPECT_EQ(batch[0].oldType, torch);
    EXPECT_EQ(batch[0].newType, dirt);
    EXPECT_TRUE(batch[0].triggerMeshRebuild);
}

