#pragma once

#include "finevox/position.hpp"
#include "finevox/mesh.hpp"

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

namespace finevox {

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
    std::vector<VkVertexInputAttributeDescription> attributes(4);

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

    // Ambient Occlusion (location 3)
    attributes[3].binding = 0;
    attributes[3].location = 3;
    attributes[3].format = VK_FORMAT_R32_SFLOAT;
    attributes[3].offset = offsetof(ChunkVertex, ao);

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
    // Dirty Tracking (for mesh rebuild scheduling)
    // ========================================================================

    /// Mark this view as needing mesh regeneration
    void markDirty() { dirty_ = true; }

    /// Clear the dirty flag
    void clearDirty() { dirty_ = false; }

    /// Check if mesh needs regeneration
    [[nodiscard]] bool isDirty() const { return dirty_; }

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
    bool dirty_ = true;  // Start dirty so initial mesh is generated
};

}  // namespace finevox
