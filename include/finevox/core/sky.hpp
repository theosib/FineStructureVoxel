#pragma once

/**
 * @file sky.hpp
 * @brief SkyParameters and computation from time of day
 *
 * Design: Phase 15 Sky + Day/Night Cycle
 *
 * Pure functions that compute sky rendering state from a time-of-day value.
 * No state — call computeSkyParameters() each frame with the current time.
 *
 * Time-of-day convention: [0.0, 1.0)
 *   0.00 = dawn, 0.25 = noon, 0.50 = sunset, 0.75 = midnight
 */

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>

namespace finevox {

struct SkyParameters {
    glm::vec4 skyColor;       // Clear/background color (RGBA)
    glm::vec3 fogColor;       // Distance fog tint
    glm::vec3 sunDirection;   // Directional light direction (normalized)
    float skyBrightness;      // Sky light multiplier [0, 1]
    float ambientLevel;       // Minimum ambient light for shader
    float sunIntensity;       // Diffuse light strength
};

namespace detail {

inline float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline glm::vec3 lerpColor(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * t;
}

inline glm::vec4 lerpColor(const glm::vec4& a, const glm::vec4& b, float t) {
    return a + (b - a) * t;
}

}  // namespace detail

/// Compute sky parameters from time of day [0.0, 1.0).
/// Pure function — no side effects.
inline SkyParameters computeSkyParameters(float timeOfDay) {
    using namespace detail;

    // Clamp and wrap to [0, 1)
    float t = timeOfDay - std::floor(timeOfDay);

    SkyParameters sky{};

    // ====================================================================
    // Sky color gradient
    // ====================================================================

    // Color keyframes
    constexpr glm::vec4 NIGHT_SKY   {0.01f, 0.01f, 0.05f, 1.0f};
    constexpr glm::vec4 DAWN_SKY    {0.8f,  0.4f,  0.2f,  1.0f};
    constexpr glm::vec4 DAY_SKY     {0.4f,  0.6f,  0.9f,  1.0f};
    constexpr glm::vec4 SUNSET_SKY  {0.9f,  0.4f,  0.15f, 1.0f};

    if (t < 0.02f) {
        // Night → dawn transition start
        float frac = smoothstep(0.0f, 0.02f, t);
        sky.skyColor = lerpColor(NIGHT_SKY, DAWN_SKY, frac);
    } else if (t < 0.06f) {
        // Dawn → day
        float frac = smoothstep(0.02f, 0.06f, t);
        sky.skyColor = lerpColor(DAWN_SKY, DAY_SKY, frac);
    } else if (t < 0.42f) {
        // Full day
        sky.skyColor = DAY_SKY;
    } else if (t < 0.46f) {
        // Day → sunset
        float frac = smoothstep(0.42f, 0.46f, t);
        sky.skyColor = lerpColor(DAY_SKY, SUNSET_SKY, frac);
    } else if (t < 0.52f) {
        // Sunset → night
        float frac = smoothstep(0.46f, 0.52f, t);
        sky.skyColor = lerpColor(SUNSET_SKY, NIGHT_SKY, frac);
    } else if (t < 0.96f) {
        // Full night
        sky.skyColor = NIGHT_SKY;
    } else {
        // Pre-dawn (night → start of dawn)
        float frac = smoothstep(0.96f, 1.0f, t);
        sky.skyColor = lerpColor(NIGHT_SKY, DAWN_SKY, frac * 0.3f);
    }

    // ====================================================================
    // Fog color (slightly brighter/warmer than sky)
    // ====================================================================

    constexpr glm::vec3 NIGHT_FOG  {0.02f, 0.02f, 0.06f};
    constexpr glm::vec3 DAY_FOG    {0.6f,  0.7f,  0.85f};
    constexpr glm::vec3 DAWN_FOG   {0.85f, 0.5f,  0.3f};
    constexpr glm::vec3 SUNSET_FOG {0.9f,  0.45f, 0.2f};

    if (t < 0.02f) {
        float frac = smoothstep(0.0f, 0.02f, t);
        sky.fogColor = lerpColor(NIGHT_FOG, DAWN_FOG, frac);
    } else if (t < 0.06f) {
        float frac = smoothstep(0.02f, 0.06f, t);
        sky.fogColor = lerpColor(DAWN_FOG, DAY_FOG, frac);
    } else if (t < 0.42f) {
        sky.fogColor = DAY_FOG;
    } else if (t < 0.46f) {
        float frac = smoothstep(0.42f, 0.46f, t);
        sky.fogColor = lerpColor(DAY_FOG, SUNSET_FOG, frac);
    } else if (t < 0.52f) {
        float frac = smoothstep(0.46f, 0.52f, t);
        sky.fogColor = lerpColor(SUNSET_FOG, NIGHT_FOG, frac);
    } else if (t < 0.96f) {
        sky.fogColor = NIGHT_FOG;
    } else {
        float frac = smoothstep(0.96f, 1.0f, t);
        sky.fogColor = lerpColor(NIGHT_FOG, DAWN_FOG, frac * 0.3f);
    }

    // ====================================================================
    // Sun direction (semicircle arc from east to west during day)
    // ====================================================================

    // Sun angle: at dawn (t=0) → horizon east, noon (t=0.25) → overhead,
    // sunset (t=0.5) → horizon west. Night → below horizon (moonlight).

    if (t < 0.5f) {
        // Daytime: sun arcs from east horizon through zenith to west horizon
        float sunAngle = t * 2.0f * glm::pi<float>();  // 0 to pi
        float y = std::sin(sunAngle);                    // 0 → 1 → 0
        float x = -std::cos(sunAngle);                   // 1 → -1 (east to west)
        sky.sunDirection = glm::normalize(glm::vec3(x, y + 0.1f, 0.3f));
    } else {
        // Night: dim moonlight from above
        sky.sunDirection = glm::normalize(glm::vec3(0.2f, 0.8f, 0.3f));
    }

    // ====================================================================
    // Sky brightness (multiplier for sky light in shader)
    // ====================================================================

    constexpr float NIGHT_BRIGHTNESS = 0.2f;
    constexpr float DAY_BRIGHTNESS = 1.0f;

    if (t < 0.04f) {
        float frac = smoothstep(0.0f, 0.04f, t);
        sky.skyBrightness = NIGHT_BRIGHTNESS + (DAY_BRIGHTNESS - NIGHT_BRIGHTNESS) * frac;
    } else if (t < 0.42f) {
        sky.skyBrightness = DAY_BRIGHTNESS;
    } else if (t < 0.50f) {
        float frac = smoothstep(0.42f, 0.50f, t);
        sky.skyBrightness = DAY_BRIGHTNESS - (DAY_BRIGHTNESS - NIGHT_BRIGHTNESS) * frac;
    } else {
        sky.skyBrightness = NIGHT_BRIGHTNESS;
    }

    // ====================================================================
    // Ambient and sun intensity
    // ====================================================================

    // Ambient: higher during day, lower at night
    if (t < 0.04f) {
        float frac = smoothstep(0.0f, 0.04f, t);
        sky.ambientLevel = 0.15f + 0.25f * frac;
    } else if (t < 0.42f) {
        sky.ambientLevel = 0.4f;
    } else if (t < 0.50f) {
        float frac = smoothstep(0.42f, 0.50f, t);
        sky.ambientLevel = 0.4f - 0.25f * frac;
    } else {
        sky.ambientLevel = 0.15f;
    }

    // Sun intensity: full during day, reduced at night (moonlight)
    if (t < 0.04f) {
        float frac = smoothstep(0.0f, 0.04f, t);
        sky.sunIntensity = 0.1f + 0.5f * frac;
    } else if (t < 0.42f) {
        sky.sunIntensity = 0.6f;
    } else if (t < 0.50f) {
        float frac = smoothstep(0.42f, 0.50f, t);
        sky.sunIntensity = 0.6f - 0.5f * frac;
    } else {
        sky.sunIntensity = 0.1f;
    }

    return sky;
}

}  // namespace finevox
