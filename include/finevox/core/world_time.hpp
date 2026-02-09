#pragma once

/**
 * @file world_time.hpp
 * @brief WorldTime — tick-based day/night cycle
 *
 * Design: Phase 15 Sky + Day/Night Cycle
 *
 * Tracks in-game time using tick-based progression (Minecraft convention:
 * 24000 ticks = 1 day, 20 ticks/sec default). Provides time-of-day queries
 * for sky rendering, gameplay decisions (spawning, crop growth), and
 * persistence via DataContainer.
 *
 * The time-of-day range [0, 1) maps to:
 *   0.00 = dawn, 0.25 = noon, 0.50 = sunset, 0.75 = midnight
 */

#include <cstdint>

namespace finevox {

// Forward declaration
class DataContainer;

class WorldTime {
public:
    // ========================================================================
    // Constants
    // ========================================================================

    static constexpr int64_t TICKS_PER_DAY = 24000;
    static constexpr int64_t DAWN     = 0;
    static constexpr int64_t NOON     = 6000;
    static constexpr int64_t SUNSET   = 12000;
    static constexpr int64_t MIDNIGHT = 18000;

    // ========================================================================
    // Advancement
    // ========================================================================

    /// Advance time by deltaSeconds real-time seconds.
    /// Accounts for ticksPerSecond and timeSpeed. No-op if frozen.
    void advance(float deltaSeconds);

    // ========================================================================
    // Queries
    // ========================================================================

    /// Total ticks elapsed since world creation
    [[nodiscard]] int64_t totalTicks() const { return totalTicks_; }

    /// Ticks within current day [0, TICKS_PER_DAY)
    [[nodiscard]] int64_t dayTicks() const;

    /// Day number (0-based)
    [[nodiscard]] int32_t dayNumber() const;

    /// Time of day as [0.0, 1.0) where 0=dawn, 0.25=noon, 0.5=sunset, 0.75=midnight
    [[nodiscard]] float timeOfDay() const;

    /// True during daylight [DAWN, SUNSET)
    [[nodiscard]] bool isDaytime() const;

    /// True during nighttime [SUNSET, next DAWN)
    [[nodiscard]] bool isNighttime() const;

    /// Sky light level for gameplay queries (0-15, varies with time)
    [[nodiscard]] uint8_t skyLightLevel() const;

    /// Continuous sky brightness [0.0, 1.0] for shader use
    [[nodiscard]] float skyBrightness() const;

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Set real-time ticks per second (default 20.0)
    void setTicksPerSecond(float tps);
    [[nodiscard]] float ticksPerSecond() const { return ticksPerSecond_; }

    /// Set time speed multiplier (default 1.0, 0 = paused, 2 = double speed)
    void setTimeSpeed(float speed);
    [[nodiscard]] float timeSpeed() const { return timeSpeed_; }

    /// Set absolute time in ticks
    void setTime(int64_t ticks);

    /// Freeze/unfreeze time progression
    void setFrozen(bool frozen);
    [[nodiscard]] bool isFrozen() const { return frozen_; }

    // ========================================================================
    // Persistence
    // ========================================================================

    /// Save to DataContainer
    void saveTo(DataContainer& dc) const;

    /// Load from DataContainer (returns default if keys missing)
    static WorldTime loadFrom(const DataContainer& dc);

private:
    int64_t totalTicks_ = 0;
    float accumulator_ = 0.0f;   // Sub-tick fractional accumulator
    float ticksPerSecond_ = 20.0f;
    float timeSpeed_ = 1.0f;
    bool frozen_ = false;
};

}  // namespace finevox
