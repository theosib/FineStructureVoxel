#pragma once

/**
 * @file sky.hpp
 * @brief SkyParameters and computation from time of day
 *
 * Design: Phase 15 Sky + Day/Night Cycle
 *
 * Time-of-day convention: [0.0, 1.0)
 *   0.00 = dawn, 0.25 = noon, 0.50 = sunset, 0.75 = midnight
 */

#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace finevox {

struct SkyParameters {
    glm::vec4 skyColor;       // Clear/background color (RGBA)
    glm::vec3 fogColor;       // Distance fog tint
    glm::vec3 sunDirection;   // Directional light direction (normalized)
    float skyBrightness;      // Sky light multiplier [0, 1]
    float ambientLevel;       // Minimum ambient light for shader
    float sunIntensity;       // Diffuse light strength
};

/// A color keyframe for time-of-day interpolation
struct SkyColorKeyframe {
    float time;        // Time of day [0, 1)
    glm::vec4 skyColor;
    glm::vec3 fogColor;
};

/// A brightness keyframe for time-of-day interpolation
struct SkyBrightnessKeyframe {
    float time;        // Time of day [0, 1)
    float skyBrightness;
    float ambientLevel;
    float sunIntensity;
};

/// Data-driven sky configuration loaded from a config file.
/// Holds color and brightness keyframes that define the day/night cycle.
struct SkyConfig {
    std::vector<SkyColorKeyframe> colorKeyframes;
    std::vector<SkyBrightnessKeyframe> brightnessKeyframes;

    /// Create the default (hardcoded) sky config
    static SkyConfig defaults();

    /// Load from a config file (returns defaults if file not found or parse error)
    static SkyConfig fromFile(const std::string& path);
};

/// Compute sky parameters from time of day using the given config.
SkyParameters computeSkyParameters(float timeOfDay, const SkyConfig& config);

/// Compute sky parameters using the default config (convenience).
SkyParameters computeSkyParameters(float timeOfDay);

}  // namespace finevox
