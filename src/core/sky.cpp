#include "finevox/core/sky.hpp"

#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace finevox {

namespace {

float smoothstep(float edge0, float edge1, float x) {
    float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

glm::vec3 lerpColor3(const glm::vec3& a, const glm::vec3& b, float t) {
    return a + (b - a) * t;
}

glm::vec4 lerpColor4(const glm::vec4& a, const glm::vec4& b, float t) {
    return a + (b - a) * t;
}

// Find the two keyframes surrounding time t and compute the interpolation factor.
// Keyframes must be sorted by time. Wraps around from last to first.
template<typename K>
std::pair<const K*, const K*> findBracket(const std::vector<K>& keys, float t, float& frac) {
    if (keys.empty()) { frac = 0.0f; return {nullptr, nullptr}; }
    if (keys.size() == 1) { frac = 0.0f; return {&keys[0], &keys[0]}; }

    // Find first keyframe with time > t
    size_t next = 0;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time > t) { next = i; break; }
        if (i == keys.size() - 1) { next = 0; break; }
    }
    size_t prev = (next == 0) ? keys.size() - 1 : next - 1;

    float t0 = keys[prev].time;
    float t1 = keys[next].time;
    if (t1 <= t0) t1 += 1.0f;  // wrap
    float localT = t;
    if (localT < t0) localT += 1.0f;

    float range = t1 - t0;
    frac = (range > 0.0001f) ? smoothstep(t0, t1, localT) : 0.0f;
    return {&keys[prev], &keys[next]};
}

}  // namespace

// ============================================================================
// SkyConfig
// ============================================================================

SkyConfig SkyConfig::defaults() {
    SkyConfig cfg;

    // Color keyframes (dawn, day, sunset, night)
    cfg.colorKeyframes = {
        {0.00f, {0.01f, 0.01f, 0.05f, 1.0f}, {0.02f, 0.02f, 0.06f}},  // night→dawn
        {0.02f, {0.8f,  0.4f,  0.2f,  1.0f}, {0.85f, 0.5f,  0.3f}},   // dawn
        {0.06f, {0.4f,  0.6f,  0.9f,  1.0f}, {0.6f,  0.7f,  0.85f}},  // day
        {0.42f, {0.4f,  0.6f,  0.9f,  1.0f}, {0.6f,  0.7f,  0.85f}},  // day (hold)
        {0.46f, {0.9f,  0.4f,  0.15f, 1.0f}, {0.9f,  0.45f, 0.2f}},   // sunset
        {0.52f, {0.01f, 0.01f, 0.05f, 1.0f}, {0.02f, 0.02f, 0.06f}},  // night
        {0.96f, {0.01f, 0.01f, 0.05f, 1.0f}, {0.02f, 0.02f, 0.06f}},  // night (hold)
    };

    // Brightness keyframes
    cfg.brightnessKeyframes = {
        {0.00f, 0.2f, 0.15f, 0.1f},   // night
        {0.04f, 1.0f, 0.4f,  0.6f},   // day
        {0.42f, 1.0f, 0.4f,  0.6f},   // day (hold)
        {0.50f, 0.2f, 0.15f, 0.1f},   // night
    };

    return cfg;
}

SkyConfig SkyConfig::fromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return defaults();

    SkyConfig cfg;
    std::string line;
    enum Section { None, Color, Brightness } section = None;

    while (std::getline(file, line)) {
        // Trim
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        if (line.empty() || line[0] == '#') continue;

        if (line == "[color_keyframes]") { section = Color; continue; }
        if (line == "[brightness_keyframes]") { section = Brightness; continue; }

        std::istringstream iss(line);
        if (section == Color) {
            SkyColorKeyframe k{};
            char comma;
            if (iss >> k.time >> comma
                >> k.skyColor.r >> comma >> k.skyColor.g >> comma >> k.skyColor.b >> comma
                >> k.fogColor.r >> comma >> k.fogColor.g >> comma >> k.fogColor.b) {
                k.skyColor.a = 1.0f;
                cfg.colorKeyframes.push_back(k);
            }
        } else if (section == Brightness) {
            SkyBrightnessKeyframe k{};
            char comma;
            if (iss >> k.time >> comma >> k.skyBrightness >> comma
                >> k.ambientLevel >> comma >> k.sunIntensity) {
                cfg.brightnessKeyframes.push_back(k);
            }
        }
    }

    // Validate: must have at least one keyframe in each category
    if (cfg.colorKeyframes.empty() || cfg.brightnessKeyframes.empty()) {
        return defaults();
    }

    return cfg;
}

// ============================================================================
// computeSkyParameters
// ============================================================================

SkyParameters computeSkyParameters(float timeOfDay, const SkyConfig& config) {
    float t = timeOfDay - std::floor(timeOfDay);
    SkyParameters sky{};

    // Interpolate color keyframes
    float colorFrac;
    auto [c0, c1] = findBracket(config.colorKeyframes, t, colorFrac);
    if (c0 && c1) {
        sky.skyColor = lerpColor4(c0->skyColor, c1->skyColor, colorFrac);
        sky.fogColor = lerpColor3(c0->fogColor, c1->fogColor, colorFrac);
    }

    // Interpolate brightness keyframes
    float brightFrac;
    auto [b0, b1] = findBracket(config.brightnessKeyframes, t, brightFrac);
    if (b0 && b1) {
        sky.skyBrightness = b0->skyBrightness + (b1->skyBrightness - b0->skyBrightness) * brightFrac;
        sky.ambientLevel = b0->ambientLevel + (b1->ambientLevel - b0->ambientLevel) * brightFrac;
        sky.sunIntensity = b0->sunIntensity + (b1->sunIntensity - b0->sunIntensity) * brightFrac;
    }

    // Sun direction (semicircle arc from east to west during day)
    if (t < 0.5f) {
        float sunAngle = t * 2.0f * glm::pi<float>();
        float y = std::sin(sunAngle);
        float x = -std::cos(sunAngle);
        sky.sunDirection = glm::normalize(glm::vec3(x, y + 0.1f, 0.3f));
    } else {
        sky.sunDirection = glm::normalize(glm::vec3(0.2f, 0.8f, 0.3f));
    }

    return sky;
}

SkyParameters computeSkyParameters(float timeOfDay) {
    static SkyConfig defaultConfig = SkyConfig::defaults();
    return computeSkyParameters(timeOfDay, defaultConfig);
}

}  // namespace finevox
