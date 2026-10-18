#include <gtest/gtest.h>
#include "finevox/core/world_time.hpp"
#include "finevox/core/sky.hpp"
#include "finevox/core/data_container.hpp"
#include <cmath>

using namespace finevox;

// ============================================================================
// WorldTime - Basic Construction
// ============================================================================

TEST(WorldTimeTest, DefaultState) {
    WorldTime wt;
    EXPECT_EQ(wt.totalTicks(), 0);
    EXPECT_EQ(wt.dayTicks(), 0);
    EXPECT_EQ(wt.dayNumber(), 0);
    EXPECT_FLOAT_EQ(wt.timeOfDay(), 0.0f);
    EXPECT_TRUE(wt.isDaytime());
    EXPECT_FALSE(wt.isNighttime());
}

// ============================================================================
// WorldTime - Advancement
// ============================================================================

TEST(WorldTimeTest, AdvanceOneTick) {
    WorldTime wt;
    // At 30 ticks/sec, 1/30 seconds = 1 tick
    wt.advance(1.0f / 30.0f);
    EXPECT_EQ(wt.totalTicks(), 1);
}

TEST(WorldTimeTest, AdvanceMultipleTicks) {
    WorldTime wt;
    // 1 second at 30 tps = 30 ticks
    wt.advance(1.0f);
    EXPECT_EQ(wt.totalTicks(), 30);
}

TEST(WorldTimeTest, AdvanceAccumulates) {
    WorldTime wt;
    // 0.02 seconds * 30 tps = 0.6 ticks (rounds down to 0)
    wt.advance(0.02f);
    EXPECT_EQ(wt.totalTicks(), 0);

    // Another 0.02 = total 0.04 * 30 = 1.2, should have 1 tick now
    wt.advance(0.02f);
    EXPECT_EQ(wt.totalTicks(), 1);
}

TEST(WorldTimeTest, AdvanceNegativeDeltaIgnored) {
    WorldTime wt;
    wt.advance(1.0f);  // 30 ticks
    wt.advance(-1.0f); // Should be ignored
    EXPECT_EQ(wt.totalTicks(), 30);
}

TEST(WorldTimeTest, AdvanceZeroDeltaIgnored) {
    WorldTime wt;
    wt.advance(1.0f);
    wt.advance(0.0f);
    EXPECT_EQ(wt.totalTicks(), 30);
}

TEST(WorldTimeTest, FrozenDoesNotAdvance) {
    WorldTime wt;
    wt.setFrozen(true);
    wt.advance(1.0f);
    EXPECT_EQ(wt.totalTicks(), 0);

    wt.setFrozen(false);
    wt.advance(1.0f);
    EXPECT_EQ(wt.totalTicks(), 30);
}

// ============================================================================
// WorldTime - Day/Night Queries
// ============================================================================

TEST(WorldTimeTest, DayTicksWraps) {
    WorldTime wt;
    wt.setTime(WorldTime::TICKS_PER_DAY + 100);
    EXPECT_EQ(wt.dayTicks(), 100);
}

TEST(WorldTimeTest, DayNumber) {
    WorldTime wt;
    wt.setTime(0);
    EXPECT_EQ(wt.dayNumber(), 0);

    wt.setTime(WorldTime::TICKS_PER_DAY);
    EXPECT_EQ(wt.dayNumber(), 1);

    wt.setTime(3 * WorldTime::TICKS_PER_DAY + 100);
    EXPECT_EQ(wt.dayNumber(), 3);
}

TEST(WorldTimeTest, TimeOfDayRange) {
    WorldTime wt;

    // Dawn
    wt.setTime(0);
    EXPECT_FLOAT_EQ(wt.timeOfDay(), 0.0f);

    // Noon
    wt.setTime(WorldTime::NOON);
    EXPECT_NEAR(wt.timeOfDay(), 0.25f, 0.001f);

    // Sunset
    wt.setTime(WorldTime::SUNSET);
    EXPECT_NEAR(wt.timeOfDay(), 0.5f, 0.001f);

    // Midnight
    wt.setTime(WorldTime::MIDNIGHT);
    EXPECT_NEAR(wt.timeOfDay(), 0.75f, 0.001f);
}

TEST(WorldTimeTest, IsDaytime) {
    WorldTime wt;

    // Dawn (tick 0) = daytime
    wt.setTime(0);
    EXPECT_TRUE(wt.isDaytime());

    // Noon = daytime
    wt.setTime(WorldTime::NOON);
    EXPECT_TRUE(wt.isDaytime());

    // Just before sunset = daytime
    wt.setTime(WorldTime::SUNSET - 1);
    EXPECT_TRUE(wt.isDaytime());

    // Sunset = nighttime
    wt.setTime(WorldTime::SUNSET);
    EXPECT_TRUE(wt.isNighttime());

    // Midnight = nighttime
    wt.setTime(WorldTime::MIDNIGHT);
    EXPECT_TRUE(wt.isNighttime());
}

// ============================================================================
// WorldTime - Sky Brightness
// ============================================================================

TEST(WorldTimeTest, SkyBrightnessDaytime) {
    WorldTime wt;

    // Full day (around noon)
    wt.setTime(WorldTime::NOON);
    EXPECT_NEAR(wt.skyBrightness(), 1.0f, 0.01f);
}

TEST(WorldTimeTest, SkyBrightnessNighttime) {
    WorldTime wt;

    // Full night
    wt.setTime(WorldTime::MIDNIGHT);
    EXPECT_NEAR(wt.skyBrightness(), 0.2f, 0.01f);
}

TEST(WorldTimeTest, SkyBrightnessDawnTransition) {
    WorldTime wt;

    // At dawn start (tick 0), brightness should be night level
    wt.setTime(0);
    EXPECT_NEAR(wt.skyBrightness(), 0.2f, 0.05f);

    // After dawn transition (tick ~1440 = 0.04 * 36000), brightness should be full
    wt.setTime(1440);
    EXPECT_NEAR(wt.skyBrightness(), 1.0f, 0.05f);
}

TEST(WorldTimeTest, SkyLightLevel) {
    WorldTime wt;

    // Full day = level 15
    wt.setTime(WorldTime::NOON);
    EXPECT_EQ(wt.skyLightLevel(), 15);

    // Night = level 3 (0.2 * 15 = 3)
    wt.setTime(WorldTime::MIDNIGHT);
    EXPECT_EQ(wt.skyLightLevel(), 3);
}

// ============================================================================
// WorldTime - Configuration
// ============================================================================

TEST(WorldTimeTest, CustomTickRate) {
    WorldTime wt;
    wt.setTicksPerSecond(40.0f);  // Double speed
    wt.advance(1.0f);
    EXPECT_EQ(wt.totalTicks(), 40);
}

TEST(WorldTimeTest, TimeSpeed) {
    WorldTime wt;
    wt.setTimeSpeed(2.0f);
    wt.advance(1.0f);
    EXPECT_EQ(wt.totalTicks(), 60);  // 30 tps * 2x speed = 60
}

TEST(WorldTimeTest, SetTime) {
    WorldTime wt;
    wt.setTime(12345);
    EXPECT_EQ(wt.totalTicks(), 12345);
}

// ============================================================================
// WorldTime - Persistence
// ============================================================================

TEST(WorldTimeTest, SaveLoadRoundTrip) {
    WorldTime original;
    original.setTime(54321);
    original.setTicksPerSecond(40.0f);
    original.setTimeSpeed(3.0f);
    original.setFrozen(true);

    DataContainer dc;
    original.saveTo(dc);

    WorldTime loaded = WorldTime::loadFrom(dc);
    EXPECT_EQ(loaded.totalTicks(), 54321);
    // After loading, frozen should be true
    loaded.advance(1.0f);
    EXPECT_EQ(loaded.totalTicks(), 54321);  // Didn't advance because frozen

    loaded.setFrozen(false);
    loaded.advance(1.0f);
    EXPECT_EQ(loaded.totalTicks(), 54321 + 120);  // 40 tps * 3x speed = 120
}

// ============================================================================
// SkyParameters - computeSkyParameters
// ============================================================================

TEST(SkyParametersTest, DawnHasWarmColors) {
    auto sky = computeSkyParameters(0.02f);  // Mid-dawn
    // Dawn sky should have warm colors (red/orange hue)
    EXPECT_GT(sky.skyColor.r, sky.skyColor.b);
}

TEST(SkyParametersTest, DayHasBlueColors) {
    auto sky = computeSkyParameters(0.25f);  // Noon
    // Day sky should be blue
    EXPECT_GT(sky.skyColor.b, sky.skyColor.r);
    EXPECT_GT(sky.skyColor.b, sky.skyColor.g);
}

TEST(SkyParametersTest, NightIsDark) {
    auto sky = computeSkyParameters(0.75f);  // Midnight
    // Night sky should be very dark
    EXPECT_LT(sky.skyColor.r, 0.1f);
    EXPECT_LT(sky.skyColor.g, 0.1f);
    EXPECT_LT(sky.skyColor.b, 0.1f);
}

TEST(SkyParametersTest, SunsetHasWarmColors) {
    auto sky = computeSkyParameters(0.46f);  // Mid-sunset
    // Sunset should have warm colors
    EXPECT_GT(sky.skyColor.r, sky.skyColor.b);
}

TEST(SkyParametersTest, SunDirectionAtNoon) {
    auto sky = computeSkyParameters(0.25f);  // Noon
    // Sun should be roughly overhead at noon (high Y)
    EXPECT_GT(sky.sunDirection.y, 0.5f);
}

TEST(SkyParametersTest, SunDirectionAtDawn) {
    auto sky = computeSkyParameters(0.0f);  // Dawn
    // Sun should be near horizon at dawn (low Y)
    EXPECT_LT(sky.sunDirection.y, 0.3f);
}

TEST(SkyParametersTest, SunDirectionAtNight) {
    auto sky = computeSkyParameters(0.75f);  // Midnight
    // At night, sun direction represents moonlight
    // Y component should indicate above horizon (soft illumination)
    EXPECT_GT(sky.sunDirection.y, 0.0f);
}

TEST(SkyParametersTest, SkyBrightnessCurve) {
    // Dawn
    auto dawn = computeSkyParameters(0.0f);
    EXPECT_LT(dawn.skyBrightness, 0.5f);

    // Day
    auto day = computeSkyParameters(0.25f);
    EXPECT_NEAR(day.skyBrightness, 1.0f, 0.05f);

    // Sunset
    auto sunset = computeSkyParameters(0.5f);
    EXPECT_LT(sunset.skyBrightness, 0.5f);

    // Night
    auto night = computeSkyParameters(0.75f);
    EXPECT_LT(night.skyBrightness, 0.3f);
}

TEST(SkyParametersTest, AmbientLevelRange) {
    // Test that ambient stays within reasonable range across the day
    for (float t = 0.0f; t < 1.0f; t += 0.05f) {
        auto sky = computeSkyParameters(t);
        EXPECT_GE(sky.ambientLevel, 0.0f) << "t=" << t;
        EXPECT_LE(sky.ambientLevel, 1.0f) << "t=" << t;
    }
}

TEST(SkyParametersTest, SunIntensityRange) {
    for (float t = 0.0f; t < 1.0f; t += 0.05f) {
        auto sky = computeSkyParameters(t);
        EXPECT_GE(sky.sunIntensity, 0.0f) << "t=" << t;
        EXPECT_LE(sky.sunIntensity, 1.0f) << "t=" << t;
    }
}

TEST(SkyParametersTest, SkyColorAlphaIsOne) {
    for (float t = 0.0f; t < 1.0f; t += 0.1f) {
        auto sky = computeSkyParameters(t);
        EXPECT_FLOAT_EQ(sky.skyColor.a, 1.0f);
    }
}

TEST(SkyParametersTest, SunDirectionIsNormalized) {
    for (float t = 0.0f; t < 1.0f; t += 0.05f) {
        auto sky = computeSkyParameters(t);
        float len = glm::length(sky.sunDirection);
        EXPECT_NEAR(len, 1.0f, 0.01f) << "t=" << t;
    }
}

TEST(SkyParametersTest, FogColorMatchesSky) {
    // Fog color should be similar to sky color (within reason)
    auto day = computeSkyParameters(0.25f);
    // During day, fog should be close to sky color
    EXPECT_NEAR(day.fogColor.r, day.skyColor.r, 0.3f);
    EXPECT_NEAR(day.fogColor.g, day.skyColor.g, 0.3f);
    EXPECT_NEAR(day.fogColor.b, day.skyColor.b, 0.3f);
}

// ============================================================================
// WorldTime + SkyParameters Integration
// ============================================================================

TEST(SkyIntegrationTest, FullDayCycle) {
    WorldTime wt;

    // Advance through a full day and check transitions
    float prevBrightness = 0.0f;
    bool sawDark = false;
    bool sawBright = false;

    for (int tick = 0; tick < WorldTime::TICKS_PER_DAY; tick += 240) {
        wt.setTime(tick);
        auto sky = computeSkyParameters(wt.timeOfDay());

        if (sky.skyBrightness < 0.3f) sawDark = true;
        if (sky.skyBrightness > 0.9f) sawBright = true;

        // Verify brightness is in valid range
        EXPECT_GE(sky.skyBrightness, 0.0f);
        EXPECT_LE(sky.skyBrightness, 1.0f);

        prevBrightness = sky.skyBrightness;
    }

    EXPECT_TRUE(sawDark) << "Should have dark periods during the day";
    EXPECT_TRUE(sawBright) << "Should have bright periods during the day";
    (void)prevBrightness;
}

// ============================================================================
// SkyConfig::fromFile Tests (Phase R4)
// ============================================================================

#include <filesystem>
#include <fstream>

namespace {
std::string findResourcesDirForSkyTest() {
    std::vector<std::string> candidates = {
        "resources",
        "../resources",
        "../../resources",
        "/Users/theosib/projects/FineStructure/FineStructureVoxel/resources"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c + "/sky.conf")) {
            return c;
        }
    }
    return "";
}
}  // namespace

TEST(SkyConfigTest, LoadsShippedSkyConfFile) {
    std::string resDir = findResourcesDirForSkyTest();
    ASSERT_FALSE(resDir.empty()) << "Could not locate resources/sky.conf";

    SkyConfig cfg = SkyConfig::fromFile(resDir + "/sky.conf");
    ASSERT_EQ(cfg.colorKeyframes.size(), 7u);
    ASSERT_EQ(cfg.brightnessKeyframes.size(), 4u);

    // Check dawn and noon keyframes from resources/sky.conf
    EXPECT_FLOAT_EQ(cfg.colorKeyframes[1].time, 0.02f);
    EXPECT_FLOAT_EQ(cfg.colorKeyframes[1].skyColor.r, 0.80f);
    EXPECT_FLOAT_EQ(cfg.colorKeyframes[1].skyColor.g, 0.40f);
    EXPECT_FLOAT_EQ(cfg.colorKeyframes[1].skyColor.b, 0.20f);

    EXPECT_FLOAT_EQ(cfg.brightnessKeyframes[1].time, 0.04f);
    EXPECT_FLOAT_EQ(cfg.brightnessKeyframes[1].skyBrightness, 1.0f);
    EXPECT_FLOAT_EQ(cfg.brightnessKeyframes[1].ambientLevel, 0.40f);
    EXPECT_FLOAT_EQ(cfg.brightnessKeyframes[1].sunIntensity, 0.6f);

    SkyParameters noon = computeSkyParameters(0.25f, cfg);
    EXPECT_NEAR(noon.skyColor.r, 0.40f, 1e-4f);
    EXPECT_NEAR(noon.skyColor.g, 0.60f, 1e-4f);
    EXPECT_NEAR(noon.skyColor.b, 0.90f, 1e-4f);
    EXPECT_NEAR(noon.skyBrightness, 1.0f, 1e-4f);
}

TEST(SkyConfigTest, CustomSkyConfOverridesDefaultsAndMissingFallsBack) {
    auto tmpPath = std::filesystem::temp_directory_path() / "finevox_custom_sky.conf";
    {
        std::ofstream out(tmpPath);
        out << "[color_keyframes]\n"
            << "0.00, 0.75, 0.10, 0.55, 0.70, 0.15, 0.50\n"
            << "0.50, 0.75, 0.10, 0.55, 0.70, 0.15, 0.50\n"
            << "[brightness_keyframes]\n"
            << "0.00, 0.85, 0.55, 0.95\n"
            << "0.50, 0.85, 0.55, 0.95\n";
    }

    SkyConfig custom = SkyConfig::fromFile(tmpPath.string());
    std::filesystem::remove(tmpPath);

    ASSERT_EQ(custom.colorKeyframes.size(), 2u);
    ASSERT_EQ(custom.brightnessKeyframes.size(), 2u);

    SkyParameters params = computeSkyParameters(0.25f, custom);
    EXPECT_NEAR(params.skyColor.r, 0.75f, 1e-4f);
    EXPECT_NEAR(params.skyColor.g, 0.10f, 1e-4f);
    EXPECT_NEAR(params.skyColor.b, 0.55f, 1e-4f);
    EXPECT_NEAR(params.skyBrightness, 0.85f, 1e-4f);
    EXPECT_NEAR(params.ambientLevel, 0.55f, 1e-4f);
    EXPECT_NEAR(params.sunIntensity, 0.95f, 1e-4f);

    // Missing file falls back to SkyConfig::defaults()
    SkyConfig fallback = SkyConfig::fromFile("/nonexistent/path/sky.conf");
    SkyConfig defs = SkyConfig::defaults();
    EXPECT_EQ(fallback.colorKeyframes.size(), defs.colorKeyframes.size());
    EXPECT_EQ(fallback.brightnessKeyframes.size(), defs.brightnessKeyframes.size());
}

