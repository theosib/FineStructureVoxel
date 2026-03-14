#include <gtest/gtest.h>
#include "finevox/core/game_session.hpp"
#include "finevox/core/block_event.hpp"
#include "finevox/core/distances.hpp"
#include "finevox/core/data_container.hpp"
#include "finevox/core/sky.hpp"

using namespace finevox;

// ============================================================================
// GameSessionConfig
// ============================================================================

TEST(GameSessionConfigTest, Defaults) {
    auto config = GameSessionConfig::defaults();
    EXPECT_TRUE(config.enableLighting);
    EXPECT_TRUE(config.enableSound);
    EXPECT_TRUE(config.enableFluidSimulation);
    EXPECT_FLOAT_EQ(config.gravity, -14.0f);
    EXPECT_EQ(config.tickRate, 30u);
    EXPECT_EQ(config.randomTicksPerChunk, 4u);
}

TEST(GameSessionConfigTest, RoundTrip) {
    GameSessionConfig orig;
    orig.enableLighting = false;
    orig.enableSound = false;
    orig.gravity = -9.8f;
    orig.tickRate = 20;
    orig.randomTicksPerChunk = 8;

    auto dc = orig.toDataContainer();
    auto loaded = GameSessionConfig::fromDataContainer(dc);

    EXPECT_FALSE(loaded.enableLighting);
    EXPECT_FALSE(loaded.enableSound);
    EXPECT_TRUE(loaded.enableFluidSimulation);
    EXPECT_FLOAT_EQ(loaded.gravity, -9.8f);
    EXPECT_EQ(loaded.tickRate, 20u);
    EXPECT_EQ(loaded.randomTicksPerChunk, 8u);
}

TEST(GameSessionConfigTest, FromEmptyDataContainer) {
    DataContainer dc;
    auto config = GameSessionConfig::fromDataContainer(dc);
    // All defaults
    EXPECT_TRUE(config.enableLighting);
    EXPECT_FLOAT_EQ(config.gravity, -14.0f);
    EXPECT_EQ(config.tickRate, 30u);
}

// ============================================================================
// TickConfig
// ============================================================================

TEST(TickConfigTest, RoundTrip) {
    TickConfig orig;
    orig.gameTickIntervalMs = 50;
    orig.randomTicksPerSubchunk = 8;
    orig.randomSeed = 12345;
    orig.gameTicksEnabled = false;
    orig.randomTicksEnabled = false;

    auto dc = orig.toDataContainer();
    auto loaded = TickConfig::fromDataContainer(dc);

    EXPECT_EQ(loaded.gameTickIntervalMs, 50u);
    EXPECT_EQ(loaded.randomTicksPerSubchunk, 8u);
    EXPECT_EQ(loaded.randomSeed, 12345u);
    EXPECT_FALSE(loaded.gameTicksEnabled);
    EXPECT_FALSE(loaded.randomTicksEnabled);
}

TEST(TickConfigTest, FromEmptyDataContainer) {
    DataContainer dc;
    auto config = TickConfig::fromDataContainer(dc);
    EXPECT_EQ(config.gameTickIntervalMs, 33u);
    EXPECT_EQ(config.randomTicksPerSubchunk, 4u);
    EXPECT_TRUE(config.gameTicksEnabled);
}

// ============================================================================
// DistanceConfig
// ============================================================================

TEST(DistanceConfigTest, RoundTrip) {
    DistanceConfig orig;
    orig.rendering.chunkRenderDistance = 128.0f;
    orig.fog.startDistance = 100.0f;
    orig.fog.endDistance = 128.0f;
    orig.loading.loadDistance = 200.0f;

    auto dc = orig.toDataContainer();
    EXPECT_DOUBLE_EQ(dc.get<double>("render.chunk_distance"), 128.0);
    EXPECT_DOUBLE_EQ(dc.get<double>("fog.start_distance"), 100.0);
    EXPECT_DOUBLE_EQ(dc.get<double>("loading.load_distance"), 200.0);
}

// ============================================================================
// SkyConfig
// ============================================================================

TEST(SkyConfigTest, DefaultConfig) {
    auto config = SkyConfig::defaults();
    EXPECT_GT(config.colorKeyframes.size(), 0u);
    EXPECT_GT(config.brightnessKeyframes.size(), 0u);
}

TEST(SkyConfigTest, ComputeWithDefaultConfig) {
    auto sky = computeSkyParameters(0.25f);  // Noon
    EXPECT_GT(sky.skyBrightness, 0.5f);
    EXPECT_GT(sky.ambientLevel, 0.2f);
}

TEST(SkyConfigTest, ComputeWithExplicitConfig) {
    auto config = SkyConfig::defaults();
    auto sky = computeSkyParameters(0.25f, config);
    EXPECT_GT(sky.skyBrightness, 0.5f);
}

TEST(SkyConfigTest, NightIsDark) {
    auto sky = computeSkyParameters(0.75f);  // Midnight
    EXPECT_LT(sky.skyBrightness, 0.5f);
    EXPECT_LT(sky.ambientLevel, 0.3f);
}

TEST(SkyConfigTest, LoadFromMissingFile) {
    auto config = SkyConfig::fromFile("/nonexistent/path/sky.conf");
    // Should return defaults
    EXPECT_GT(config.colorKeyframes.size(), 0u);
}

TEST(SkyConfigTest, AllTimesValid) {
    for (float t = 0.0f; t < 1.0f; t += 0.01f) {
        auto sky = computeSkyParameters(t);
        EXPECT_GE(sky.skyBrightness, 0.0f);
        EXPECT_LE(sky.skyBrightness, 1.1f);
        EXPECT_GE(sky.ambientLevel, 0.0f);
        EXPECT_GE(sky.sunIntensity, 0.0f);
    }
}
