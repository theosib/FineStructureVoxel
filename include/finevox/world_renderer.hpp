#pragma once

/**
 * @file world_renderer.hpp
 * @brief View-relative rendering coordination
 *
 * Design: [06-rendering.md] §6.1 WorldRenderer
 */

#include "finevox/position.hpp"
#include "finevox/subchunk_view.hpp"
#include "finevox/mesh.hpp"
#include "finevox/world.hpp"
#include "finevox/block_atlas.hpp"
#include "finevox/texture_manager.hpp"
#include "finevox/lod.hpp"
#include "finevox/distances.hpp"
#include "finevox/mesh_worker_pool.hpp"
#include "finevox/mesh_rebuild_queue.hpp"
#include "finevox/wake_signal.hpp"

#include <chrono>

// FineStructureVK types
#include <finevk/engine/camera.hpp>
#include <finevk/high/simple_renderer.hpp>
#include <finevk/rendering/pipeline.hpp>
#include <finevk/rendering/descriptors.hpp>
#include <finevk/high/uniform_buffer.hpp>
#include <finevk/high/texture.hpp>

#include <unordered_map>
#include <memory>
#include <vector>
#include <array>
#include <algorithm>

namespace finevox {

// ============================================================================
// ChunkUniform - Per-subchunk push constant data
// ============================================================================

struct ChunkPushConstants {
    alignas(16) glm::vec3 chunkOffset;  // World offset of subchunk origin relative to camera
    alignas(4) float fogStart;          // Fog start distance
    alignas(16) glm::vec3 fogColor;     // Fog color
    alignas(4) float fogEnd;            // Fog end distance
};

// ============================================================================
// WorldRendererConfig - Configuration for WorldRenderer
// ============================================================================

struct WorldRendererConfig {
    float viewDistance = 256.0f;        // Maximum render distance in blocks
    uint32_t maxVisibleChunks = 4096;   // Maximum subchunks to render per frame
    float meshCapacityMultiplier = 1.5f; // Extra GPU buffer capacity for mesh updates

    // GPU Memory Management
    size_t gpuMemoryBudget = 512 * 1024 * 1024;  // Target GPU memory budget (default 512MB)
    float unloadDistanceMultiplier = 1.2f;       // Unload chunks beyond viewDistance * this (hysteresis)
    uint32_t maxUnloadsPerFrame = 16;            // Limit unloads per frame to avoid stalls

    // Fog configuration
    FogConfig fog;

    // Debug: offset the render camera backwards from the cull camera
    // This reveals frustum culling edges for testing purposes.
    // When enabled, culling uses the real camera position, but rendering
    // occurs from an offset position so you can see culled geometry edges.
    bool debugCameraOffset = false;
    glm::vec3 debugOffset = glm::vec3(0.0f, 0.0f, 32.0f);  // Default: 32 blocks back (positive Z = backward in camera space)

    // Debug: disable frustum culling (render all chunks in view distance)
    // Useful for profiling total vertex counts without culling effects
    bool disableFrustumCulling = false;
};

// ============================================================================
// WorldRenderer - Renders visible subchunks of a World
// ============================================================================

/**
 * @brief Renders visible subchunks of a World using FineStructureVK
 *
 * WorldRenderer manages:
 * - GPU mesh storage (SubChunkView per subchunk)
 * - View-relative coordinate system (subtracts camera position for precision)
 * - Frustum culling using finevk::Camera
 * - Rendering pipeline and descriptors
 *
 * Usage with BlockAtlas (simple):
 * @code
 * BlockAtlas atlas;
 * atlas.createPlaceholderAtlas(device, commandPool, 16, 16);
 * atlas.setBlockTexture(stoneId, 0, 0);
 *
 * WorldRenderer renderer(device, simpleRenderer, world, config);
 * renderer.loadShaders("shaders/chunk.vert.spv", "shaders/chunk.frag.spv");
 * renderer.setBlockAtlas(atlas.texture());
 * renderer.setTextureProvider(atlas.createProvider());
 * renderer.initialize();
 * @endcode
 *
 * Usage with TextureManager (advanced):
 * @code
 * TextureManager textures(device, commandPool);
 * textures.loadAtlas("game://textures/blocks.atlas");
 * textures.loadBlockTextureConfig("game://config/block_textures.cbor");
 *
 * WorldRenderer renderer(device, simpleRenderer, world, config);
 * renderer.loadShaders("shaders/chunk.vert.spv", "shaders/chunk.frag.spv");
 * renderer.setBlockAtlas(textures.getAtlasTexture(0));  // Primary atlas
 * renderer.setTextureProvider([&](BlockTypeId id, Face face) {
 *     return textures.getTexture(textures.getBlockTextureName(id, face))
 *         .value_or(TextureHandle{}).region.bounds();
 * });
 * renderer.initialize();
 * @endcode
 *
 * Per-frame:
 * @code
 * camera.updateState();
 * renderer.updateCamera(camera.state());
 * renderer.updateMeshes();  // Rebuild dirty meshes
 *
 * // During render pass
 * renderer.render(commandBuffer);
 * @endcode
 */
class WorldRenderer {
public:
    /**
     * @brief Create a WorldRenderer
     * @param device FineStructureVK logical device
     * @param renderer SimpleRenderer for render pass and frame info
     * @param world The World to render
     * @param config Configuration options
     */
    WorldRenderer(
        finevk::LogicalDevice* device,
        finevk::SimpleRenderer* renderer,
        World& world,
        const WorldRendererConfig& config = {}
    );

    ~WorldRenderer();

    // Non-copyable
    WorldRenderer(const WorldRenderer&) = delete;
    WorldRenderer& operator=(const WorldRenderer&) = delete;

    // ========================================================================
    // Setup
    // ========================================================================

    /**
     * @brief Load vertex and fragment shaders from SPIR-V files
     */
    void loadShaders(const std::string& vertPath, const std::string& fragPath);

    /**
     * @brief Set the block texture atlas
     * @param atlas Texture containing all block face textures
     */
    void setBlockAtlas(finevk::Texture* atlas);

    /**
     * @brief Set the texture provider for block faces
     */
    void setTextureProvider(BlockTextureProvider provider);

    /**
     * @brief Initialize rendering resources (call after shaders and atlas are set)
     */
    void initialize();

    // ========================================================================
    // Async Meshing (Optional)
    // ========================================================================

    /**
     * @brief Enable async meshing with a worker thread pool
     *
     * When enabled, mesh generation runs on background threads. The graphics
     * thread uploads pending meshes each frame. Stale meshes continue to render
     * while new meshes are being built.
     *
     * @param numThreads Number of worker threads (0 = auto, based on hardware)
     */
    void enableAsyncMeshing(size_t numThreads = 0);

    /**
     * @brief Disable async meshing and return to synchronous mode
     *
     * Stops worker threads and clears the mesh cache. Existing GPU meshes
     * are preserved.
     */
    void disableAsyncMeshing();

    /**
     * @brief Check if async meshing is enabled
     */
    [[nodiscard]] bool asyncMeshingEnabled() const { return meshWorkerPool_ != nullptr; }

    /**
     * @brief Get the mesh worker pool (for advanced configuration)
     * @return Pointer to worker pool, or nullptr if async meshing is disabled
     */
    [[nodiscard]] MeshWorkerPool* meshWorkerPool() { return meshWorkerPool_.get(); }

    /**
     * @brief Get the mesh rebuild queue (for connecting to LightEngine)
     * @return Pointer to rebuild queue, or nullptr if async meshing is disabled
     */
    [[nodiscard]] MeshRebuildQueue* meshRebuildQueue() { return meshRebuildQueue_.get(); }

    // ========================================================================
    // Frame Timing and Deadline-Based Waiting
    // ========================================================================

    /**
     * @brief Wait for mesh uploads with a deadline
     *
     * Blocks until either:
     * 1. A mesh becomes available in the upload queue
     * 2. The deadline is reached
     * 3. Shutdown is signaled
     *
     * This enables the graphics thread to sleep efficiently instead of
     * busy-polling, while ensuring it wakes in time to submit the frame.
     *
     * Requires async meshing to be enabled.
     *
     * @param deadline Time point at which to wake regardless of uploads
     * @return true if woken normally, false if shutdown was signaled
     */
    bool waitForMeshUploads(std::chrono::steady_clock::time_point deadline);

    /**
     * @brief Wait for mesh uploads with a timeout
     *
     * Convenience overload that calculates deadline from current time.
     *
     * @param timeout Maximum time to wait
     * @return true if woken normally, false if shutdown was signaled
     */
    bool waitForMeshUploads(std::chrono::milliseconds timeout);

    /**
     * @brief Record frame timing for adaptive deadline calculation
     *
     * Call at the start of each frame to track actual frame intervals.
     * This data is used to estimate how much time is available for
     * mesh processing before the next frame deadline.
     *
     * @return Estimated time until next frame deadline (based on vsync timing)
     */
    std::chrono::microseconds recordFrameStart();

    /**
     * @brief Get the estimated frame period based on vsync timing
     *
     * Returns a smoothed average of recent frame intervals.
     * Useful for calculating deadlines.
     *
     * @return Estimated frame period, or 16.67ms if no data available
     */
    [[nodiscard]] std::chrono::microseconds estimatedFramePeriod() const;

    /**
     * @brief Get the WakeSignal for external coordination
     *
     * Allows other systems to signal the graphics thread wake.
     * Returns nullptr if async meshing is disabled.
     */
    [[nodiscard]] WakeSignal* wakeSignal() { return asyncMeshingEnabled() ? &wakeSignal_ : nullptr; }

    // ========================================================================
    // Per-Frame Updates
    // ========================================================================

    /**
     * @brief Update camera state for culling and rendering
     * @param cameraState Current camera state from finevk::Camera
     */
    void updateCamera(const finevk::CameraState& cameraState);

    /**
     * @brief Update camera with high-precision position for large world support
     *
     * Use this overload when the camera is at large world coordinates (>10000 blocks)
     * to avoid float32 precision jitter. The double-precision position is used for
     * view-relative offset calculations, while the cameraState provides orientation.
     *
     * @param cameraState Camera state (orientation, projection, frustum)
     * @param highPrecisionPos Camera position in double precision
     */
    void updateCamera(const finevk::CameraState& cameraState, const glm::dvec3& highPrecisionPos);

    /**
     * @brief Update meshes for dirty subchunks
     *
     * Rebuilds meshes for subchunks that have been modified.
     * Call once per frame before render().
     *
     * @param maxUpdates Maximum number of mesh updates this frame (0 = unlimited)
     */
    void updateMeshes(uint32_t maxUpdates = 0);

    /**
     * @brief Mark a subchunk as needing mesh rebuild
     * @param pos Subchunk position
     */
    void markDirty(ChunkPos pos);

    /**
     * @brief Mark all subchunks in a column as needing mesh rebuild
     *
     * Call this when a new column is loaded to ensure all its subchunks get meshes.
     * This is typically connected to ColumnManager::setChunkLoadCallback().
     *
     * @param pos Column position
     */
    void markColumnDirty(ColumnPos pos);

    /**
     * @brief Mark all loaded subchunks as dirty
     */
    void markAllDirty();

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Render visible subchunks
     *
     * Must be called within an active render pass.
     *
     * @param cmd Command buffer from SimpleRenderer
     */
    void render(finevk::CommandBuffer& cmd);

    // ========================================================================
    // Statistics
    // ========================================================================

    /// Get number of subchunks with GPU meshes
    [[nodiscard]] uint32_t loadedChunkCount() const { return static_cast<uint32_t>(views_.size()); }

    /// Get number of subchunks rendered last frame
    [[nodiscard]] uint32_t renderedChunkCount() const { return lastRenderedCount_; }

    /// Get number of subchunks culled last frame
    [[nodiscard]] uint32_t culledChunkCount() const { return lastCulledCount_; }

    /// Get total vertex count of rendered meshes
    [[nodiscard]] uint32_t renderedVertexCount() const { return lastRenderedVertices_; }

    /// Get total triangle count of rendered meshes
    [[nodiscard]] uint32_t renderedTriangleCount() const { return lastRenderedTriangles_; }

    /// Get total GPU memory used by all loaded meshes (bytes)
    [[nodiscard]] size_t gpuMemoryUsed() const;

    /// Get configured GPU memory budget (bytes)
    [[nodiscard]] size_t gpuMemoryBudget() const { return config_.gpuMemoryBudget; }

    /// Set GPU memory budget (bytes)
    void setGpuMemoryBudget(size_t bytes) { config_.gpuMemoryBudget = bytes; }

    /// Get number of chunks unloaded last frame
    [[nodiscard]] uint32_t unloadedChunkCount() const { return lastUnloadedCount_; }

    // ========================================================================
    // Debug
    // ========================================================================

    /**
     * @brief Enable/disable debug camera offset mode
     *
     * When enabled, the render camera is offset from the cull camera,
     * allowing you to see the edges of frustum culling for testing.
     */
    void setDebugCameraOffset(bool enabled) { config_.debugCameraOffset = enabled; }
    [[nodiscard]] bool debugCameraOffset() const { return config_.debugCameraOffset; }

    /**
     * @brief Set the debug camera offset vector
     * @param offset Offset from cull camera to render camera (in world units)
     */
    void setDebugOffset(const glm::vec3& offset) { config_.debugOffset = offset; }
    [[nodiscard]] glm::vec3 debugOffset() const { return config_.debugOffset; }

    /**
     * @brief Enable/disable frustum culling (for profiling)
     * When disabled, all chunks in view distance are rendered regardless of frustum
     */
    void setFrustumCullingEnabled(bool enabled) { config_.disableFrustumCulling = !enabled; }
    [[nodiscard]] bool frustumCullingEnabled() const { return !config_.disableFrustumCulling; }

    /**
     * @brief Disable hidden face culling (renders all faces for debugging)
     */
    void setDisableFaceCulling(bool disabled) { meshBuilder_.setDisableFaceCulling(disabled); }
    [[nodiscard]] bool disableFaceCulling() const { return meshBuilder_.disableFaceCulling(); }

    /**
     * @brief Enable/disable greedy meshing (merges adjacent faces)
     */
    void setGreedyMeshing(bool enabled) { meshBuilder_.setGreedyMeshing(enabled); }
    [[nodiscard]] bool greedyMeshing() const { return meshBuilder_.greedyMeshing(); }

    /**
     * @brief Enable/disable smooth lighting (per-vertex light interpolation)
     * Requires a light provider to be set via setLightProvider()
     */
    void setSmoothLighting(bool enabled);
    [[nodiscard]] bool smoothLighting() const { return meshBuilder_.smoothLighting(); }

    /**
     * @brief Enable/disable flat lighting (single light sample per face, no interpolation)
     * Shows the raw L1 ball from light propagation. Requires a light provider.
     * Note: smooth lighting takes precedence if both are enabled.
     */
    void setFlatLighting(bool enabled);
    [[nodiscard]] bool flatLighting() const { return meshBuilder_.flatLighting(); }

    /**
     * @brief Set the light provider for smooth/flat lighting
     * @param provider Function that returns combined light (0-15) for a world position
     */
    void setLightProvider(BlockLightProvider provider);

    // ========================================================================
    // Fog Configuration
    // ========================================================================

    /**
     * @brief Get mutable reference to fog configuration
     */
    [[nodiscard]] FogConfig& fogConfig() { return config_.fog; }
    [[nodiscard]] const FogConfig& fogConfig() const { return config_.fog; }

    /**
     * @brief Enable/disable fog rendering
     */
    void setFogEnabled(bool enabled) { config_.fog.enabled = enabled; }
    [[nodiscard]] bool fogEnabled() const { return config_.fog.enabled; }

    /**
     * @brief Set fog distance range
     * @param start Distance where fog begins (0% density)
     * @param end Distance where fog is complete (100% density)
     */
    void setFogDistances(float start, float end) {
        config_.fog.startDistance = start;
        config_.fog.endDistance = end;
    }
    [[nodiscard]] float fogStartDistance() const { return config_.fog.startDistance; }
    [[nodiscard]] float fogEndDistance() const { return config_.fog.endDistance; }

    /**
     * @brief Set fog color
     */
    void setFogColor(const glm::vec3& color) { config_.fog.color = color; }
    [[nodiscard]] glm::vec3 fogColor() const { return config_.fog.color; }

    /**
     * @brief Enable/disable dynamic fog color (ties to sky color)
     */
    void setFogDynamicColor(bool enabled) { config_.fog.dynamicColor = enabled; }
    [[nodiscard]] bool fogDynamicColor() const { return config_.fog.dynamicColor; }

    /**
     * @brief Calculate fog factor for a given distance
     * @return 0.0 = no fog, 1.0 = full fog
     */
    [[nodiscard]] float getFogFactor(float distance) const {
        return config_.fog.getFogFactor(distance);
    }

    // ========================================================================
    // LOD (Level of Detail)
    // ========================================================================

    /**
     * @brief Get mutable reference to LOD configuration
     * Allows adjusting LOD distance thresholds, hysteresis, bias, and force settings
     */
    [[nodiscard]] LODConfig& lodConfig() { return lodConfig_; }
    [[nodiscard]] const LODConfig& lodConfig() const { return lodConfig_; }

    /**
     * @brief Enable/disable LOD system
     * When disabled, all chunks render at full detail (LOD0)
     */
    void setLODEnabled(bool enabled) { lodEnabled_ = enabled; }
    [[nodiscard]] bool lodEnabled() const { return lodEnabled_; }

    /**
     * @brief Set LOD debug visualization mode
     */
    void setLODDebugMode(LODDebugMode mode) { lodDebugMode_ = mode; }
    [[nodiscard]] LODDebugMode lodDebugMode() const { return lodDebugMode_; }

    /**
     * @brief Set LOD merge mode (how LOD blocks are sized)
     * FullHeight: LOD blocks are always full cubes (default, best culling)
     * HeightLimited: LOD blocks match highest source block (smoother transitions)
     */
    void setLODMergeMode(LODMergeMode mode);
    [[nodiscard]] LODMergeMode lodMergeMode() const;

    /**
     * @brief Increment LOD bias (shifts all chunks to lower detail)
     */
    void increaseLODBias() { lodConfig_.lodBias = std::min(lodConfig_.lodBias + 1, 4); }

    /**
     * @brief Decrement LOD bias (shifts all chunks to higher detail)
     */
    void decreaseLODBias() { lodConfig_.lodBias = std::max(lodConfig_.lodBias - 1, -4); }

    /**
     * @brief Cycle through LOD debug modes
     */
    void cycleLODDebugMode();

    /**
     * @brief Get statistics about current LOD distribution
     */
    struct LODStats {
        std::array<uint32_t, LOD_LEVEL_COUNT> chunksPerLevel{};
        uint32_t totalChunks = 0;
    };
    [[nodiscard]] LODStats getLODStats() const;

    /**
     * @brief Get total vertex count across all loaded meshes
     */
    [[nodiscard]] size_t totalVertexCount() const;

    /**
     * @brief Get total index count across all loaded meshes
     */
    [[nodiscard]] size_t totalIndexCount() const;

    /**
     * @brief Get number of loaded subchunk meshes
     */
    [[nodiscard]] size_t loadedMeshCount() const;

    // ========================================================================
    // Cleanup
    // ========================================================================

    /**
     * @brief Unload mesh for a subchunk
     * @param pos Subchunk position
     */
    void unloadChunk(ChunkPos pos);

    /**
     * @brief Unload meshes for subchunks outside unload distance
     *
     * Uses hysteresis: unload distance = viewDistance * unloadDistanceMultiplier
     * This prevents thrashing when camera is near the view distance boundary.
     * Limited to maxUnloadsPerFrame to avoid GPU stalls.
     *
     * @return Number of chunks unloaded
     */
    uint32_t unloadDistantChunks();

    /**
     * @brief Enforce GPU memory budget by unloading furthest chunks
     *
     * If GPU memory usage exceeds budget, unloads chunks starting from
     * the furthest from camera until under budget or no more can be unloaded.
     * Only unloads chunks beyond view distance (won't unload visible chunks).
     *
     * @return Number of chunks unloaded
     */
    uint32_t enforceMemoryBudget();

    /**
     * @brief Perform all cleanup tasks (call once per frame after render)
     *
     * Combines unloadDistantChunks() and enforceMemoryBudget().
     * Safe to call every frame - uses limits to avoid stalls.
     */
    void performCleanup();

    /**
     * @brief Unload all meshes
     */
    void unloadAll();

private:
    // Create pipeline and descriptors
    void createPipeline();
    void createDescriptorSets();

    // Get or create SubChunkView for a position
    SubChunkView* getOrCreateView(ChunkPos pos);

    // Build mesh for a subchunk at the given LOD level
    MeshData buildMeshFor(ChunkPos pos, LODLevel lodLevel = LODLevel::LOD0);

    // Check if a subchunk is within view distance
    bool isInViewDistance(ChunkPos pos) const;

    // Check if a subchunk is in the camera frustum
    bool isInFrustum(ChunkPos pos) const;

    // Calculate view-relative offset for a subchunk
    glm::vec3 calculateViewRelativeOffset(ChunkPos pos) const;

    // Async mesh update path (used when meshWorkerPool_ is active)
    void updateMeshesAsync(uint32_t maxUpdates);

    // Configuration
    WorldRendererConfig config_;

    // External references
    finevk::LogicalDevice* device_;
    finevk::SimpleRenderer* renderer_;
    World& world_;

    // Camera state
    finevk::CameraState cameraState_;       // Includes viewRelativeFrustumPlanes for culling
    glm::dvec3 highPrecisionCameraPos_{0.0};  // Double-precision camera position
    glm::vec3 renderCameraPos_{0.0f};       // Used for rendering (may be offset, float for GPU)
    glm::vec3 cameraChunkPos_{0.0f};        // Cull camera in chunk coordinates

    // Shaders
    finevk::ShaderModulePtr vertexShader_;
    finevk::ShaderModulePtr fragmentShader_;

    // Pipeline
    finevk::DescriptorSetLayoutPtr descriptorLayout_;
    finevk::DescriptorPoolPtr descriptorPool_;
    finevk::PipelineLayoutPtr pipelineLayout_;
    finevk::GraphicsPipelinePtr pipeline_;

    // Uniform buffers (per-frame)
    std::unique_ptr<finevk::UniformBuffer<finevk::CameraUniform>> cameraUniform_;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Block atlas
    finevk::Texture* blockAtlas_ = nullptr;
    BlockTextureProvider textureProvider_;
    BlockLightProvider lightProvider_;  // Stored separately for worker pool

    // SubChunk views (GPU meshes)
    std::unordered_map<ChunkPos, std::unique_ptr<SubChunkView>> views_;

    // Dirty tracking
    std::vector<ChunkPos> dirtyChunks_;

    // Mesh building
    MeshBuilder meshBuilder_;

    // LOD system
    LODConfig lodConfig_;
    bool lodEnabled_ = true;
    LODDebugMode lodDebugMode_ = LODDebugMode::None;
    LODMergeMode lodMergeMode_ = LODMergeMode::FullHeight;

    // Statistics
    uint32_t lastRenderedCount_ = 0;
    uint32_t lastCulledCount_ = 0;
    uint32_t lastRenderedVertices_ = 0;
    uint32_t lastRenderedTriangles_ = 0;
    uint32_t lastUnloadedCount_ = 0;

    // Async meshing (optional)
    std::unique_ptr<MeshRebuildQueue> meshRebuildQueue_;
    std::unique_ptr<MeshWorkerPool> meshWorkerPool_;

    // Frame timing and wake signal for deadline-based waiting
    WakeSignal wakeSignal_;
    std::chrono::steady_clock::time_point lastFrameStart_;
    static constexpr size_t kFrameHistorySize = 8;  // Track last N frames
    std::array<std::chrono::microseconds, kFrameHistorySize> frameHistory_;
    size_t frameHistoryIndex_ = 0;
    size_t frameHistoryCount_ = 0;  // How many valid entries (0 until first frame)

    // State
    bool initialized_ = false;
};

}  // namespace finevox
