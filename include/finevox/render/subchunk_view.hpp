#pragma once

/**
 * @file subchunk_view.hpp
 * @brief GPU mesh handle and read-only subchunk access
 *
 * Design: [06-rendering.md] §6.5 SubChunkView
 */

#include "finevox/core/position.hpp"
#include "finevox/core/mesh.hpp"
#include "finevox/core/lod.hpp"

// Vulkan-dependent headers
#include <vulkan/vulkan.h>

// FineStructureVK types (forward declarations)
namespace finevk {
class LogicalDevice;
class CommandPool;
class CommandBuffer;
class RawMesh;
using RawMeshPtr = std::unique_ptr<RawMesh>;
}

#include <memory>
#include <optional>

namespace finevox::render {

// ============================================================================
// ChunkVertex Vulkan helpers
// ============================================================================

/// Get Vulkan vertex input binding description for ChunkVertex
[[nodiscard]] inline VkVertexInputBindingDescription getChunkVertexBindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(ChunkVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

/// Get Vulkan vertex input attribute descriptions for ChunkVertex
[[nodiscard]] inline std::vector<VkVertexInputAttributeDescription> getChunkVertexAttributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attributes(6);

    // Position (location 0)
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(ChunkVertex, position);

    // Normal (location 1)
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(ChunkVertex, normal);

    // TexCoord (location 2)
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributes[2].offset = offsetof(ChunkVertex, texCoord);

    // TileBounds (location 3) - for atlas texture tiling in shader
    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attributes[3].offset = offsetof(ChunkVertex, tileBounds);

    // Ambient Occlusion (location 4)
    attributes[4].binding = 0;
    attributes[4].location = 4;
    attributes[4].format = VK_FORMAT_R32_SFLOAT;
    attributes[4].offset = offsetof(ChunkVertex, ao);

    // Smooth Lighting (location 5)
    attributes[5].binding = 0;
    attributes[5].location = 5;
    attributes[5].format = VK_FORMAT_R32_SFLOAT;
    attributes[5].offset = offsetof(ChunkVertex, light);

    return attributes;
}

// ============================================================================
// SubChunkView - GPU mesh handle for a subchunk
// ============================================================================

/**
 * @brief GPU mesh representation for a subchunk
 *
 * SubChunkView manages the GPU-side mesh data for a single 16x16x16 subchunk.
 * It wraps FineStructureVK's RawMesh class, providing:
 *
 * - Chunk-relative positioning (world position offset)
 * - Mesh update support with capacity reservation
 * - Empty mesh optimization (no GPU resources for empty subchunks)
 *
 * Usage:
 *   // Create view for a subchunk
 *   SubChunkView view(chunkPos);
 *
 *   // Upload mesh data
 *   view.upload(device, commandPool, meshData);
 *
 *   // Later: update when subchunk changes
 *   if (view.canUpdateInPlace(newMeshData)) {
 *       view.update(commandPool, newMeshData);
 *   } else {
 *       view.upload(device, commandPool, newMeshData);
 *   }
 *
 *   // Render
 *   if (view.hasGeometry()) {
 *       view.bind(cmd);
 *       view.draw(cmd);
 *   }
 */
class SubChunkView {
public:
    /// Create a view for a subchunk at the given position
    explicit SubChunkView(ChunkPos pos);

    /// Default constructor (invalid position)
    SubChunkView() = default;

    /// Destructor (defined in cpp due to incomplete RawMesh type)
    ~SubChunkView();

    /// Get the chunk position
    [[nodiscard]] ChunkPos position() const { return pos_; }

    /// Get the world position of the subchunk's origin corner
    [[nodiscard]] glm::vec3 worldOrigin() const {
        return glm::vec3(
            static_cast<float>(pos_.x * 16),
            static_cast<float>(pos_.y * 16),
            static_cast<float>(pos_.z * 16)
        );
    }

    // ========================================================================
    // GPU Resource Management
    // ========================================================================

    /**
     * @brief Upload mesh data to GPU
     *
     * Creates new GPU buffers with capacity reservation for future updates.
     * If meshData is empty, releases any existing GPU resources.
     *
     * @param device Vulkan logical device
     * @param commandPool Command pool for GPU upload
     * @param meshData CPU-side mesh data to upload
     * @param capacityMultiplier Extra capacity for updates (default 1.5 = 50% headroom)
     */
    void upload(
        finevk::LogicalDevice& device,
        finevk::CommandPool& commandPool,
        const MeshData& meshData,
        float capacityMultiplier = 1.5f
    );

    /**
     * @brief Check if mesh can be updated in-place
     *
     * Returns true if the new mesh data fits within the reserved capacity.
     */
    [[nodiscard]] bool canUpdateInPlace(const MeshData& meshData) const;

    /**
     * @brief Update mesh data in-place
     *
     * Requires: canUpdateInPlace(meshData) == true
     * If meshData is empty, releases GPU resources.
     *
     * @param commandPool Command pool for GPU upload
     * @param meshData New mesh data
     */
    void update(finevk::CommandPool& commandPool, const MeshData& meshData);

    /**
     * @brief Release GPU resources
     */
    void release();

    // ========================================================================
    // State Queries
    // ========================================================================

    /// Check if this view has geometry to render
    [[nodiscard]] bool hasGeometry() const { return mesh_ != nullptr && indexCount_ > 0; }

    /// Check if this view has allocated GPU resources
    [[nodiscard]] bool hasGpuResources() const { return mesh_ != nullptr; }

    /// Get the number of indices
    [[nodiscard]] uint32_t indexCount() const { return indexCount_; }

    /// Get the number of vertices
    [[nodiscard]] uint32_t vertexCount() const { return vertexCount_; }

    /// Get the number of triangles
    [[nodiscard]] uint32_t triangleCount() const { return indexCount_ / 3; }

    /// Get allocated GPU memory in bytes (vertex buffer + index buffer)
    /// Returns 0 if no GPU resources allocated
    [[nodiscard]] size_t gpuMemoryBytes() const { return gpuMemoryBytes_; }

    // ========================================================================
    // Rendering
    // ========================================================================

    /**
     * @brief Bind mesh to command buffer
     *
     * Call this before draw(). Binds vertex and index buffers.
     * Requires: hasGeometry() == true
     */
    void bind(finevk::CommandBuffer& cmd) const;

    /**
     * @brief Draw the mesh
     *
     * Requires: bind() was called, hasGeometry() == true
     *
     * @param cmd Command buffer
     * @param instanceCount Number of instances (default 1)
     */
    void draw(finevk::CommandBuffer& cmd, uint32_t instanceCount = 1) const;

    // ========================================================================
    // Version and LOD-Based Change Detection
    // ========================================================================

    /// Get the block version this mesh was built from
    /// Returns 0 if no mesh has been built yet
    [[nodiscard]] uint64_t lastBuiltVersion() const { return lastBuiltVersion_; }

    /// Set the block version after building a mesh
    void setLastBuiltVersion(uint64_t version) { lastBuiltVersion_ = version; }

    /// Get the light version this mesh was built from
    /// Returns 0 if no mesh has been built yet
    [[nodiscard]] uint64_t lastBuiltLightVersion() const { return lastBuiltLightVersion_; }

    /// Set the light version after building a mesh
    void setLastBuiltLightVersion(uint64_t version) { lastBuiltLightVersion_ = version; }

    /// Get the LOD level this mesh was built at
    [[nodiscard]] LODLevel lastBuiltLOD() const { return lastBuiltLOD_; }

    /// Set the LOD level after building a mesh
    void setLastBuiltLOD(LODLevel lod) { lastBuiltLOD_ = lod; }

    /// Check if mesh needs regeneration by comparing block version
    /// @param currentBlockVersion The current SubChunk::blockVersion()
    [[nodiscard]] bool needsBlockRebuild(uint64_t currentBlockVersion) const {
        return lastBuiltVersion_ != currentBlockVersion;
    }

    /// Check if mesh needs regeneration by comparing light version
    /// @param currentLightVersion The current SubChunk::lightVersion()
    [[nodiscard]] bool needsLightRebuild(uint64_t currentLightVersion) const {
        return lastBuiltLightVersion_ != currentLightVersion;
    }

    /// Check if mesh needs regeneration by comparing either version
    /// @param currentBlockVersion The current SubChunk::blockVersion()
    /// @param currentLightVersion The current SubChunk::lightVersion()
    [[nodiscard]] bool needsRebuild(uint64_t currentBlockVersion, uint64_t currentLightVersion) const {
        return needsBlockRebuild(currentBlockVersion) || needsLightRebuild(currentLightVersion);
    }

    /// Check if mesh needs regeneration due to LOD change (exact match)
    /// @param targetLOD The desired LOD level
    [[nodiscard]] bool needsLODChange(LODLevel targetLOD) const {
        return lastBuiltLOD_ != targetLOD;
    }

    /// Check if the current mesh satisfies an LOD request
    /// Uses flexible matching: request may accept multiple LOD levels
    /// @param request The LOD request (may be exact or flexible)
    [[nodiscard]] bool satisfiesLODRequest(LODRequest request) const {
        return request.accepts(lastBuiltLOD_);
    }

    /// Check if mesh needs any kind of rebuild (block/light version or LOD)
    /// @param currentBlockVersion The current SubChunk::blockVersion()
    /// @param currentLightVersion The current SubChunk::lightVersion()
    /// @param targetLOD The desired LOD level (exact match)
    [[nodiscard]] bool needsRebuild(uint64_t currentBlockVersion, uint64_t currentLightVersion, LODLevel targetLOD) const {
        return needsRebuild(currentBlockVersion, currentLightVersion) || needsLODChange(targetLOD);
    }

    /// Check if mesh needs any kind of rebuild (block/light version or LOD request)
    /// Uses flexible LOD matching for hysteresis
    /// @param currentBlockVersion The current SubChunk::blockVersion()
    /// @param currentLightVersion The current SubChunk::lightVersion()
    /// @param lodRequest The LOD request (may accept multiple levels)
    [[nodiscard]] bool needsRebuild(uint64_t currentBlockVersion, uint64_t currentLightVersion, LODRequest lodRequest) const {
        return needsRebuild(currentBlockVersion, currentLightVersion) || !satisfiesLODRequest(lodRequest);
    }

    // Legacy dirty flag interface (deprecated - use version comparison instead)
    // Kept for compatibility during transition
    void markDirty() { lastBuiltVersion_ = 0; lastBuiltLightVersion_ = 0; }
    void clearDirty() { /* no-op, use setLastBuiltVersion instead */ }
    [[nodiscard]] bool isDirty() const { return lastBuiltVersion_ == 0 || lastBuiltLightVersion_ == 0; }

    // Non-copyable, movable
    SubChunkView(const SubChunkView&) = delete;
    SubChunkView& operator=(const SubChunkView&) = delete;
    SubChunkView(SubChunkView&&) noexcept = default;
    SubChunkView& operator=(SubChunkView&&) noexcept = default;

private:
    ChunkPos pos_{0, 0, 0};
    finevk::RawMeshPtr mesh_;
    uint32_t indexCount_ = 0;
    uint32_t vertexCount_ = 0;
    size_t gpuMemoryBytes_ = 0;       // Allocated GPU memory (vertex + index buffers)
    uint64_t lastBuiltVersion_ = 0;       // 0 means never built (block version)
    uint64_t lastBuiltLightVersion_ = 0;  // 0 means never built (light version)
    LODLevel lastBuiltLOD_ = LODLevel::LOD0;  // LOD level of current mesh
};

}  // namespace finevox::render
