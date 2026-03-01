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
