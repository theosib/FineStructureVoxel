#pragma once

/**
 * @file fluid_simulator.hpp
 * @brief Core fluid flow simulation algorithm
 *
 * Implements a hybrid distance-decay + equalization flow model:
 *   1. Gravity first: flow down at full level
 *   2. Horizontal spread: level decays by spreadDecay per step
 *   3. Slope detection: BFS lookahead prefers paths to drops
 *   4. Local equalization: adjacent cells balance when diff >= 2
 *   5. Source formation: N adjacent sources create new source
 *   6. Drain: flowing blocks drain when supply removed
 *
 * Thread safety: FluidSimulator is designed to be called from the game thread only.
 * It reads/writes World fluid state directly.
 */

#include "finevox/core/position.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include <cstdint>
#include <deque>
#include <vector>
#include <unordered_set>

namespace finevox {

// Forward declarations
class World;
struct FluidType;
struct FluidInteraction;

/// Represents a pending fluid update at a world position
struct FluidUpdate {
    BlockCoord pos;
    FluidTypeId type;
    uint8_t level;       // Target level (0 = remove)
    int32_t tickDelay;   // Ticks before this update activates (for flow speed)
};

/// Configuration for the fluid simulator
struct FluidSimulatorConfig {
    /// Maximum number of fluid updates processed per tick
    int32_t maxUpdatesPerTick = 4096;

    /// Maximum BFS depth for slope detection
    int32_t slopeLookahead = 4;

    /// Whether fluid simulation is enabled
    bool enabled = true;
};

/// Core fluid simulation engine.
/// Call simulateTick() once per game tick from the game thread.
class FluidSimulator {
public:
    explicit FluidSimulator(World& world);

    /// Process one tick of fluid simulation.
    /// Processes pending updates and queues new ones.
    void simulateTick();

    /// Schedule a fluid update (e.g., when a block is placed/broken near fluid).
    /// @param pos World position
    /// @param type Fluid type to place (EMPTY_FLUID_TYPE to remove)
    /// @param level Target level (0 = remove)
    /// @param tickDelay Ticks before this update is processed
    void scheduleUpdate(BlockCoord pos, FluidTypeId type, uint8_t level, int32_t tickDelay = 0);

    /// Notify the simulator that fluid was placed or removed at a position.
    /// This triggers flow propagation from that position.
    void notifyFluidChanged(BlockCoord pos);

    /// Notify that a block changed at a position (may affect adjacent fluid).
    void notifyBlockChanged(BlockCoord pos);

    /// Get the current configuration
    [[nodiscard]] const FluidSimulatorConfig& config() const { return config_; }

    /// Set configuration
    void setConfig(const FluidSimulatorConfig& config) { config_ = config; }

    /// Get number of pending updates
    [[nodiscard]] size_t pendingUpdateCount() const { return pendingUpdates_.size() + deferredUpdates_.size(); }

    /// Check if a position has a pending update
    [[nodiscard]] bool hasPendingUpdate(BlockCoord pos) const;

    /// Clear all pending updates
    void clearPendingUpdates();

private:
    World& world_;
    FluidSimulatorConfig config_;

    /// Updates ready to process this tick (FIFO for BFS-order processing)
    std::deque<FluidUpdate> pendingUpdates_;

    /// Updates with remaining tick delay
    std::vector<FluidUpdate> deferredUpdates_;

    /// Positions already processed this tick (prevent duplicate work)
    std::unordered_set<BlockCoord> processedThisTick_;

    // ========================================================================
    // Flow Logic
    // ========================================================================

    /// Apply a flow update: place fluid at the target, then propagate
    void applyFlowUpdate(const FluidUpdate& update);

    /// Process a single position: determine what the fluid should do
    void processFluidAt(BlockCoord pos);

    /// Try to flow downward from a source/flowing cell
    /// Returns true if fluid flowed down
    bool tryFlowDown(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType);

    /// Spread horizontally from a source/flowing cell
    void spreadHorizontally(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType);

    /// Check for source formation at a position
    bool checkSourceFormation(BlockCoord pos, FluidTypeId type, const FluidType& fluidType);

    /// Equalize fluid levels between adjacent cells
    void equalizeNeighbors(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType);

    /// Drain a flowing cell that lost its supply
    void drainCell(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType);

    // ========================================================================
    // Slope Detection
    // ========================================================================

    /// BFS to find which horizontal neighbors lead toward drops.
    /// Returns a bitmask of Face values (NegX, PosX, NegZ, PosZ) that are preferred.
    uint8_t findSlopeDirections(BlockCoord pos, FluidTypeId type, int32_t maxDepth);

    // ========================================================================
    // Helpers
    // ========================================================================

    /// Check if fluid can flow into a position
    /// Considers: air/non-full blocks, block handler veto, existing fluid
    bool canFluidEnter(BlockCoord pos, FluidTypeId type, const FluidType& fluidType) const;

    /// Check if a block at a position is "full" (no room for fluid)
    bool isBlockFull(BlockCoord pos) const;

    /// Handle fluid-fluid interaction when two different fluids meet
    /// Returns true if the interaction consumed the flow (don't place fluid)
    bool handleFluidInteraction(BlockCoord pos, FluidTypeId flowing, FluidTypeId existing);

    /// Set fluid in the world and schedule neighbor updates
    void setFluidAndNotify(BlockCoord pos, FluidTypeId type, uint8_t level, const FluidType& fluidType);

    /// Remove fluid from the world and schedule neighbor updates
    void removeFluidAndNotify(BlockCoord pos);

    /// Check if a cell has a valid supply (source above or adjacent higher-level same fluid)
    bool hasValidSupply(BlockCoord pos, FluidTypeId type, uint8_t level) const;

    /// Count adjacent source blocks of the same type
    int32_t countAdjacentSources(BlockCoord pos, FluidTypeId type) const;

    /// Get the offset for a face direction
    static BlockCoord faceOffset(Face face);
};

}  // namespace finevox
