#pragma once

#include "finevox/position.hpp"
#include "finevox/subchunk_view.hpp"
#include "finevox/mesh.hpp"
#include "finevox/world.hpp"
#include "finevox/block_atlas.hpp"
#include "finevox/texture_manager.hpp"

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

namespace finevox {

// ============================================================================
// ChunkUniform - Per-subchunk push constant data
// ============================================================================

struct ChunkPushConstants {
    alignas(16) glm::vec3 chunkOffset;  // World offset of subchunk origin relative to camera
    alignas(4) float padding;
};

// ============================================================================
// WorldRendererConfig - Configuration for WorldRenderer
// ============================================================================

struct WorldRendererConfig {
    float viewDistance = 256.0f;        // Maximum render distance in blocks
    uint32_t maxVisibleChunks = 4096;   // Maximum subchunks to render per frame
    float meshCapacityMultiplier = 1.5f; // Extra GPU buffer capacity for mesh updates

    // Debug: offset the render camera backwards from the cull camera
    // This reveals frustum culling edges for testing purposes.
    // When enabled, culling uses the real camera position, but rendering
    // occurs from an offset position so you can see culled geometry edges.
    bool debugCameraOffset = false;
    glm::vec3 debugOffset = glm::vec3(0.0f, 0.0f, 32.0f);  // Default: 32 blocks back (positive Z = backward in camera space)
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
     * @brief Disable hidden face culling (renders all faces for debugging)
     */
    void setDisableFaceCulling(bool disabled) { meshBuilder_.setDisableFaceCulling(disabled); }
    [[nodiscard]] bool disableFaceCulling() const { return meshBuilder_.disableFaceCulling(); }

    // ========================================================================
    // Cleanup
    // ========================================================================

    /**
     * @brief Unload mesh for a subchunk
     * @param pos Subchunk position
     */
    void unloadChunk(ChunkPos pos);

    /**
     * @brief Unload meshes for subchunks outside view distance
     */
    void unloadDistantChunks();

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

    // Build mesh for a subchunk
    MeshData buildMeshFor(ChunkPos pos);

    // Check if a subchunk is within view distance
    bool isInViewDistance(ChunkPos pos) const;

    // Check if a subchunk is in the camera frustum
    bool isInFrustum(ChunkPos pos) const;

    // Calculate view-relative offset for a subchunk
    glm::vec3 calculateViewRelativeOffset(ChunkPos pos) const;

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

    // SubChunk views (GPU meshes)
    std::unordered_map<ChunkPos, std::unique_ptr<SubChunkView>> views_;

    // Dirty tracking
    std::vector<ChunkPos> dirtyChunks_;

    // Mesh building
    MeshBuilder meshBuilder_;

    // Statistics
    uint32_t lastRenderedCount_ = 0;
    uint32_t lastCulledCount_ = 0;
    uint32_t lastRenderedVertices_ = 0;
    uint32_t lastRenderedTriangles_ = 0;

    // State
    bool initialized_ = false;
};

}  // namespace finevox
