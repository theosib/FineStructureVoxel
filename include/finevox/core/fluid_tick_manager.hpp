#pragma once

/**
 * @file fluid_tick_manager.hpp
 * @brief Manages fluid simulation ticking within the game loop
 *
 * Tracks which subchunks have active (non-static) fluid and only
 * ticks the FluidSimulator when there's work to do.
 *
 * Thread safety: Must be called from the game thread only.
 */

#include "finevox/core/position.hpp"
#include "finevox/core/fluid_simulator.hpp"
#include <unordered_set>
#include <memory>

namespace finevox {

// Forward declarations
class World;

/// Manages the fluid simulation lifecycle within the game tick loop.
///
/// Usage:
///   1. GameSession creates FluidTickManager
///   2. Each game tick calls tick()
///   3. Block/fluid changes call notifyFluidChanged() / notifyBlockChanged()
class FluidTickManager {
public:
    explicit FluidTickManager(World& world);

    /// Process one game tick of fluid simulation.
    /// Called from the game thread tick loop.
    void tick();

    /// Access the underlying simulator (for configuration)
    [[nodiscard]] FluidSimulator& simulator() { return simulator_; }
    [[nodiscard]] const FluidSimulator& simulator() const { return simulator_; }

    /// Notify that fluid changed at a position
    void notifyFluidChanged(BlockCoord pos) { simulator_.notifyFluidChanged(pos); }

    /// Notify that a block changed at a position (may affect adjacent fluid)
    void notifyBlockChanged(BlockCoord pos) { simulator_.notifyBlockChanged(pos); }

    /// Mark a subchunk as having active fluid (non-static, needs ticking)
    void markActive(ChunkPos pos);

    /// Check if a subchunk is active
    [[nodiscard]] bool isActive(ChunkPos pos) const;

    /// Get number of active subchunks
    [[nodiscard]] size_t activeCount() const { return activeSubChunks_.size(); }

    /// Enable/disable fluid simulation
    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;

private:
    FluidSimulator simulator_;
    World& world_;

    /// Set of subchunks that have non-static fluid (need ticking)
    std::unordered_set<ChunkPos> activeSubChunks_;
};

}  // namespace finevox
