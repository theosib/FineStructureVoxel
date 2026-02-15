#include "finevox/core/world_time.hpp"
#include "finevox/core/data_container.hpp"

#include <algorithm>
#include <cmath>

namespace finevox {

// ============================================================================
// Advancement
// ============================================================================

void WorldTime::advance(float deltaSeconds) {
    if (frozen_ || deltaSeconds <= 0.0f) return;

    accumulator_ += deltaSeconds * ticksPerSecond_ * timeSpeed_;

    // Convert accumulated time to whole ticks
    auto wholeTicks = static_cast<int64_t>(accumulator_);
    if (wholeTicks > 0) {
        totalTicks_.fetch_add(wholeTicks, std::memory_order_release);
        accumulator_ -= static_cast<float>(wholeTicks);
    }
}

// ============================================================================
// Queries
// ============================================================================

int64_t WorldTime::dayTicks() const {
    int64_t ticks = totalTicks_.load(std::memory_order_acquire);
    int64_t dt = ticks % TICKS_PER_DAY;
    if (dt < 0) dt += TICKS_PER_DAY;  // Handle negative totalTicks
    return dt;
}

int32_t WorldTime::dayNumber() const {
    int64_t ticks = totalTicks_.load(std::memory_order_acquire);
    if (ticks < 0) return static_cast<int32_t>((ticks - TICKS_PER_DAY + 1) / TICKS_PER_DAY);
    return static_cast<int32_t>(ticks / TICKS_PER_DAY);
}

float WorldTime::timeOfDay() const {
    return static_cast<float>(dayTicks()) / static_cast<float>(TICKS_PER_DAY);
}

bool WorldTime::isDaytime() const {
    return dayTicks() < SUNSET;
}

bool WorldTime::isNighttime() const {
    return !isDaytime();
}

uint8_t WorldTime::skyLightLevel() const {
    // Map sky brightness to discrete 0-15 range
    float brightness = skyBrightness();
    return static_cast<uint8_t>(std::round(brightness * 15.0f));
}

float WorldTime::skyBrightness() const {
    float t = timeOfDay();

    // Brightness curve:
    // Dawn transition:   0.00-0.04 → 0.2 to 1.0
    // Full day:          0.04-0.42 → 1.0
    // Sunset transition: 0.42-0.50 → 1.0 to 0.2
    // Night:             0.50-0.96 → 0.2
    // Pre-dawn:          0.96-1.00 → 0.2 (handled by wrap-around)

    constexpr float NIGHT_BRIGHTNESS = 0.2f;
    constexpr float DAY_BRIGHTNESS = 1.0f;

    if (t < 0.04f) {
        // Dawn: rising
        float frac = t / 0.04f;
        return NIGHT_BRIGHTNESS + (DAY_BRIGHTNESS - NIGHT_BRIGHTNESS) * frac;
    } else if (t < 0.42f) {
        // Full day
        return DAY_BRIGHTNESS;
    } else if (t < 0.50f) {
        // Sunset: falling
        float frac = (t - 0.42f) / 0.08f;
        return DAY_BRIGHTNESS - (DAY_BRIGHTNESS - NIGHT_BRIGHTNESS) * frac;
    } else {
        // Night (including pre-dawn — wraps to dawn)
        return NIGHT_BRIGHTNESS;
    }
}

// ============================================================================
// Configuration
// ============================================================================

void WorldTime::setTicksPerSecond(float tps) {
    ticksPerSecond_ = std::max(tps, 0.001f);
}

void WorldTime::setTimeSpeed(float speed) {
    timeSpeed_ = std::max(speed, 0.0f);
}

void WorldTime::setTime(int64_t ticks) {
    totalTicks_.store(ticks, std::memory_order_release);
    accumulator_ = 0.0f;
}

void WorldTime::setFrozen(bool frozen) {
    frozen_ = frozen;
}

// ============================================================================
// Persistence
// ============================================================================

void WorldTime::saveTo(DataContainer& dc) const {
    dc.set<int64_t>("totalTicks", totalTicks_.load(std::memory_order_relaxed));
    dc.set<double>("ticksPerSecond", static_cast<double>(ticksPerSecond_));
    dc.set<double>("timeSpeed", static_cast<double>(timeSpeed_));
    dc.set<int64_t>("frozen", frozen_ ? 1 : 0);
}

WorldTime WorldTime::loadFrom(const DataContainer& dc) {
    WorldTime wt;
    if (dc.has("totalTicks")) {
        wt.totalTicks_.store(dc.get<int64_t>("totalTicks"), std::memory_order_relaxed);
    }
    if (dc.has("ticksPerSecond")) {
        wt.ticksPerSecond_ = static_cast<float>(dc.get<double>("ticksPerSecond", 20.0));
    }
    if (dc.has("timeSpeed")) {
        wt.timeSpeed_ = static_cast<float>(dc.get<double>("timeSpeed", 1.0));
    }
    if (dc.has("frozen")) {
        wt.frozen_ = dc.get<int64_t>("frozen") != 0;
    }
    return wt;
}

}  // namespace finevox
