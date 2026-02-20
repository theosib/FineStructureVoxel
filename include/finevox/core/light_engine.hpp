#pragma once

/**
 * @file light_engine.hpp
 * @brief BFS-based light propagation engine
 *
 * Design: [24-event-system.md] §24.8-24.11 Lighting
 */

#include "finevox/core/position.hpp"
#include "finevox/core/light_data.hpp"  // Keep for utility functions (packLightValue, etc.)
#include "finevox/core/string_interner.hpp"
#include "finevox/core/mesh_rebuild_queue.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/lod.hpp"
#include <functional>
#include <queue>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>

namespace finevox {

// ============================================================================
// LightingUpdate - Lightweight event for lighting thread
// ============================================================================

/**
 * @brief Lighting update event (lightweight)
 *
 * Represents a block change that requires lighting recalculation.
 * Design: [24-event-system.md] §24.8
 */
struct LightingUpdate {
    BlockCoord pos;
    BlockTypeId oldType;
    BlockTypeId newType;

    /// Optional fluid change at this position (only set when fluid type changes).
    /// Empty IDs mean no fluid change (or no fluid present).
    FluidTypeId oldFluid;
    FluidTypeId newFluid;

    /// If true, trigger a mesh rebuild for the affected subchunk after lighting completes.
    /// Use this to defer mesh generation until lighting is calculated, avoiding double rebuilds.
    bool triggerMeshRebuild = false;
};

// ============================================================================
// LightingQueue - Thread-safe consolidating queue
// ============================================================================

/**
 * @brief Consolidating queue for lighting thread
 *
 * If the lighting thread falls behind, only processes the latest update per
 * block position. This prevents unbounded queue growth during heavy activity.
 *
 * Thread safety: All methods are thread-safe.
 *
 * Design: [24-event-system.md] §24.8
 */
class LightingQueue {
public:
    LightingQueue() = default;
    ~LightingQueue() = default;

    // Non-copyable
    LightingQueue(const LightingQueue&) = delete;
    LightingQueue& operator=(const LightingQueue&) = delete;

    /**
     * @brief Enqueue a lighting update
     *
     * If an update already exists for this position, the new update
     * replaces it (consolidation by position).
     *
     * Thread-safe: Can be called from any thread.
     */
    void enqueue(LightingUpdate update);

    /**
     * @brief Dequeue a batch of updates
     *
     * Returns up to maxCount updates. If queue is empty, blocks until
     * updates are available, alarm fires, or stop() is called.
     *
     * @param maxCount Maximum number of updates to return
     * @param alarmFired Output parameter set to true if woken by alarm
     * @return Vector of updates (may be empty if stopped or alarm fired)
     */
    std::vector<LightingUpdate> dequeueBatch(size_t maxCount, bool* alarmFired = nullptr);

    /**
     * @brief Non-blocking dequeue
     *
     * Returns available updates without blocking.
     *
     * @param maxCount Maximum number of updates to return
     * @return Vector of updates (may be empty)
     */
    std::vector<LightingUpdate> tryDequeueBatch(size_t maxCount);

    /**
     * @brief Check if queue is empty
     */
    [[nodiscard]] bool empty() const;

    /**
     * @brief Get number of pending updates (after consolidation)
     */
    [[nodiscard]] size_t size() const;

    /**
     * @brief Signal the queue to stop (wakes up waiting threads)
     */
    void stop();

    /**
     * @brief Reset the stop flag (for reuse)
     */
    void reset();

    /**
     * @brief Set an alarm time point
     *
     * The next dequeueBatch() call will wake at this time even if no
     * updates are available. Used for periodic background scan work.
     */
    void setAlarm(std::chrono::steady_clock::time_point tp);

    /**
     * @brief Clear any pending alarm
     */
    void clearAlarm();

private:
    // Internal helper (caller must hold mutex_)
    std::vector<LightingUpdate> tryDequeueBatchUnlocked(size_t maxCount);

    // Consolidates by position - newer updates overwrite older
    mutable std::mutex mutex_;
    std::unordered_map<BlockCoord, LightingUpdate> pending_;
    std::condition_variable cv_;
    std::atomic<bool> stopped_{false};

    // Alarm support for periodic background work
    std::chrono::steady_clock::time_point alarmTime_{};
    bool alarmSet_ = false;
};

// Forward declarations
class World;
class SubChunk;
class ChunkColumn;

/**
 * @brief Custom light attenuation callback for special materials
 *
 * Used for materials like water that have non-linear light falloff.
 *
 * @param blockType The block type light is passing through
 * @param incomingLight Light level entering the block (0-15)
 * @param depthInMaterial Number of blocks traveled through this material type
 * @return Resulting light level after attenuation
 */
using LightAttenuationCallback = std::function<uint8_t(
    BlockTypeId blockType,
    uint8_t incomingLight,
    int depthInMaterial
)>;

/**
 * @brief Light propagation engine for block and sky lighting
 *
 * Implements BFS-based light propagation with support for:
 * - Block light (emitted by torches, lava, etc.)
 * - Sky light (propagates down from exposed sky)
 * - Light attenuation through transparent blocks
 * - Custom attenuation callbacks for special materials (water)
 * - Async lighting thread with consolidating queue
 *
 * Light values range from 0 (complete darkness) to 15 (maximum brightness).
 *
 * Threading model:
 * - Game logic thread calls enqueue() to submit lighting updates
 * - Lighting thread processes updates asynchronously via LightingQueue
 * - Consolidation prevents unbounded queue growth
 *
 * Design: [24-event-system.md] §24.8-24.11
 */
class LightEngine {
public:
    /**
     * @brief Construct light engine for a world
     * @param world World to operate on
     */
    explicit LightEngine(World& world);

    ~LightEngine();

    // ========================================================================
    // Light Access
    // ========================================================================

    /// Get sky light at world position (0-15)
    [[nodiscard]] uint8_t getSkyLight(const BlockCoord& pos) const;

    /// Get block light at world position (0-15)
    [[nodiscard]] uint8_t getBlockLight(const BlockCoord& pos) const;

    /// Get combined light (max of sky and block) at world position
    [[nodiscard]] uint8_t getCombinedLight(const BlockCoord& pos) const;

    /// Get subchunk containing light data at position (may be null if chunk not loaded)
    [[nodiscard]] SubChunk* getSubChunkForLight(const ChunkPos& chunkPos);
    [[nodiscard]] const SubChunk* getSubChunkForLight(const ChunkPos& chunkPos) const;

    // ========================================================================
    // Block Light Updates
    // ========================================================================

    /// Update lighting after a block is placed
    /// @param pos Position where block was placed
    /// @param oldType Previous block type (for light removal)
    /// @param newType New block type (for light emission)
    void onBlockPlaced(const BlockCoord& pos, BlockTypeId oldType, BlockTypeId newType);

    /// Update lighting after a block is removed (set to air)
    /// @param pos Position where block was removed
    /// @param oldType Previous block type
    void onBlockRemoved(const BlockCoord& pos, BlockTypeId oldType);

    /// Propagate block light from a light source
    /// @param pos Position of the light source
    /// @param lightLevel Light level to emit (0-15)
    void propagateBlockLight(const BlockCoord& pos, uint8_t lightLevel);

    /// Remove block light from a position and update surrounding area
    /// @param pos Position where light source was removed
    /// @param oldLevel Previous light level at this position
    void removeBlockLight(const BlockCoord& pos, uint8_t oldLevel);

    // ========================================================================
    // Sky Light Updates
    // ========================================================================

    /// Initialize sky light for a chunk column
    /// Call after loading a chunk or generating terrain
    /// @param columnPos Column position
    void initializeSkyLight(const ColumnPos& columnPos);

    /// Update sky light after heightmap change
    /// @param pos Position where block was placed/removed affecting sky
    /// @param oldHeight Previous height at this X,Z
    /// @param newHeight New height at this X,Z
    void updateSkyLight(const BlockCoord& pos, int32_t oldHeight, int32_t newHeight);

    /// Propagate sky light from a position
    /// @param pos Starting position
    /// @param lightLevel Light level to propagate
    void propagateSkyLight(const BlockCoord& pos, uint8_t lightLevel);

    // ========================================================================
    // Batch Operations
    // ========================================================================

    /// Recalculate all lighting for a subchunk
    /// @param chunkPos Subchunk position
    void recalculateSubChunk(const ChunkPos& chunkPos);

    /// Recalculate all lighting for a column
    /// @param columnPos Column position
    void recalculateColumn(const ColumnPos& columnPos);

    /// Mark a region as needing light recalculation
    /// Light updates will be processed on next processUpdates() call
    void markDirty(const BlockCoord& pos);

    /// Process pending light updates
    /// Call periodically to batch light updates for efficiency
    void processUpdates();

    // ========================================================================
    // Custom Attenuation
    // ========================================================================

    /// Set custom attenuation callback for a block type
    /// Use for materials like water with non-linear light falloff
    void setAttenuationCallback(BlockTypeId blockType, LightAttenuationCallback callback);

    /// Remove custom attenuation callback
    void clearAttenuationCallback(BlockTypeId blockType);

    // ========================================================================
    // Fluid Light Updates
    // ========================================================================

    /// Update lighting when fluid is placed at a position
    /// Handles emission (lava) and attenuation (water blocks light)
    void onFluidPlaced(const BlockCoord& pos, FluidTypeId fluidType);

    /// Update lighting when fluid is removed from a position
    /// Re-propagates light that was previously attenuated
    void onFluidRemoved(const BlockCoord& pos, FluidTypeId fluidType);

    // ========================================================================
    // Async Lighting Thread
    // ========================================================================

    /**
     * @brief Enqueue a lighting update (called from game logic thread)
     *
     * Thread-safe. Updates are consolidated by position - only the latest
     * update for each position is processed.
     */
    void enqueue(LightingUpdate update);

    /**
     * @brief Start the lighting thread
     *
     * The thread processes updates from the queue asynchronously.
     */
    void start();

    /**
     * @brief Stop the lighting thread
     *
     * Signals the thread to stop and waits for it to finish.
     */
    void stop();

    /**
     * @brief Check if the lighting thread is running
     */
    [[nodiscard]] bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /**
     * @brief Set the mesh rebuild queue for deferred mesh generation
     *
     * When a LightingUpdate has triggerMeshRebuild=true, the lighting thread
     * will push a mesh rebuild request to this queue after processing.
     * This allows deferring mesh generation until lighting is calculated.
     *
     * @param queue Pointer to mesh rebuild queue (nullptr to disable)
     */
    void setMeshRebuildQueue(MeshRebuildQueue* queue) { meshRebuildQueue_ = queue; }

    /**
     * @brief Get the lighting queue (for advanced use)
     */
    [[nodiscard]] LightingQueue& queue() { return queue_; }
    [[nodiscard]] const LightingQueue& queue() const { return queue_; }

    // ========================================================================
    // Configuration
    // ========================================================================

    /// Get maximum propagation distance per update (for limiting work)
    [[nodiscard]] int32_t maxPropagationDistance() const { return maxPropagationDistance_; }

    /// Set maximum propagation distance per update
    void setMaxPropagationDistance(int32_t distance) { maxPropagationDistance_ = distance; }

    /// Set batch size for lighting thread (updates per iteration)
    void setBatchSize(size_t size) { batchSize_ = size; }

    /// Get batch size
    [[nodiscard]] size_t batchSize() const { return batchSize_; }

    /// Update player positions for background scan distance culling
    /// Thread-safe: can be called from game thread while lighting thread runs
    void setPlayerPositions(std::vector<glm::dvec3> positions);

    /// Set scan radius for background scan distance culling (in blocks)
    void setScanRadius(float radius) { scanRadius_ = radius; }

private:
    World& world_;

    // Custom attenuation callbacks
    std::unordered_map<BlockTypeId, LightAttenuationCallback> attenuationCallbacks_;

    // Pending updates for batch processing
    std::unordered_set<BlockCoord> pendingUpdates_;

    // Configuration
    int32_t maxPropagationDistance_ = 256;  // Max blocks to propagate per update

    // BFS queue entry for light propagation
    struct LightNode {
        BlockCoord pos;
        uint8_t light;

        bool operator<(const LightNode& other) const {
            // Lower light values have lower priority (process higher light first)
            return light < other.light;
        }
    };

    // Get light attenuation for a block type
    [[nodiscard]] uint8_t getAttenuation(BlockTypeId blockType) const;

    // Compute attenuation at a position considering both block and fluid
    // For custom (logarithmic) fluid attenuation, returns the block attenuation
    // and sets outFluidMult to the logarithmic base; caller applies multiplicatively.
    // Returns total fixed attenuation; outFluidMult is 0.0f when no logarithmic fluid present.
    [[nodiscard]] uint8_t getAttenuationWithFluid(const BlockCoord& pos, BlockTypeId blockType,
                                                   float& outFluidMult) const;

    // Check if a block type blocks sky light
    [[nodiscard]] bool blocksSkyLight(BlockTypeId blockType) const;

    // Get light emission for a block type
    [[nodiscard]] uint8_t getLightEmission(BlockTypeId blockType) const;

    // Get or create subchunk for light storage (creates empty subchunk if needed)
    SubChunk* getOrCreateSubChunkForLight(const ChunkPos& chunkPos);

    // Convert world position to chunk position and local position
    [[nodiscard]] static ChunkPos toChunkPos(const BlockCoord& pos);
    [[nodiscard]] static int32_t toLocalIndex(const BlockCoord& pos);

    // BFS light propagation implementation
    void propagateLightBFS(const BlockCoord& start, uint8_t startLevel, bool isSkyLight);

    // Light removal with re-propagation
    void removeLightBFS(const BlockCoord& start, uint8_t startLevel, bool isSkyLight);

    // Process a single lighting update (called by lighting thread)
    void processLightingUpdate(const LightingUpdate& update);

    // Lighting thread main loop
    void lightingThreadLoop();

    // ========================================================================
    // Async Lighting Thread State
    // ========================================================================

    LightingQueue queue_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    size_t batchSize_ = 64;  // Updates per iteration

    // Mesh rebuild queue for deferred mesh generation
    MeshRebuildQueue* meshRebuildQueue_ = nullptr;

    // Tracks chunks affected during current batch processing
    // Used to batch mesh rebuild requests at end of each lighting batch
    std::unordered_set<ChunkPos> batchAffectedChunks_;

    // Record a chunk as affected by light changes (for batch mesh rebuild)
    // Also marks adjacent chunks if position is at a subchunk boundary,
    // since faces in neighboring chunks may sample light from this position.
    void recordAffectedChunk(const BlockCoord& pos);

    // Push mesh rebuild requests for all affected chunks and clear the set
    void flushAffectedChunks();

    // ========================================================================
    // Background Scan State (safety net for missed pushes)
    // ========================================================================

    // Inbox/outbox double-buffered scan tracking
    std::unordered_set<ChunkPos> scanInbox_;   // Current scan agenda
    std::unordered_set<ChunkPos> scanOutbox_;  // Accumulates for next cycle

    // Background scan step: process one entry from inbox
    void backgroundScanStep();

    // Compute adaptive scan interval based on world size
    [[nodiscard]] std::chrono::milliseconds computeScanInterval() const;

    // Check if a subchunk is near any tracked player
    [[nodiscard]] bool isNearAnyPlayer(ChunkPos pos) const;

    // Player positions for scan distance culling (updated by game thread)
    std::vector<glm::dvec3> playerPositions_;
    mutable std::mutex playerPosMutex_;
    float scanRadius_ = 320.0f;  // View distance + margin (in blocks)

    // Scan timing constant
    static constexpr int64_t SCAN_CYCLE_SECONDS = 480;  // Target: one full cycle every 8 minutes
};

}  // namespace finevox
