#include "finevox/subchunk_view.hpp"

// FineStructureVK headers
#include <finevk/high/raw_mesh.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/command/command_pool.hpp>
#include <finevk/command/command_buffer.hpp>

namespace finevox {

SubChunkView::SubChunkView(ChunkPos pos)
    : pos_(pos)
{
}

void SubChunkView::upload(
    finevk::LogicalDevice& device,
    finevk::CommandPool& commandPool,
    const MeshData& meshData,
    float capacityMultiplier
) {
    // Empty mesh - release resources
    if (meshData.isEmpty()) {
        release();
        return;
    }

    // Determine index type based on vertex count
    // Use 16-bit indices if vertex count fits (more efficient)
    bool use16BitIndices = meshData.vertexCount() <= 65535;

    // Build the RawMesh
    auto builder = finevk::RawMesh::create(&device)
        .vertexLayout(getChunkVertexBindingDescription(),
                      getChunkVertexAttributeDescriptions())
        .vertices(meshData.vertices.data(), meshData.vertices.size())
        .reserveCapacity(capacityMultiplier);

    if (use16BitIndices) {
        // Convert indices to 16-bit
        std::vector<uint16_t> indices16(meshData.indices.size());
        for (size_t i = 0; i < meshData.indices.size(); ++i) {
            indices16[i] = static_cast<uint16_t>(meshData.indices[i]);
        }
        builder.indices(indices16.data(), indices16.size());
    } else {
        builder.indices(meshData.indices.data(), meshData.indices.size());
    }

    mesh_ = builder.build(commandPool);
    indexCount_ = static_cast<uint32_t>(meshData.indices.size());
    vertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
    dirty_ = false;
}

bool SubChunkView::canUpdateInPlace(const MeshData& meshData) const {
    if (!mesh_) {
        return false;
    }
    return mesh_->canUpdateInPlace(meshData.vertices.size(), meshData.indices.size());
}

void SubChunkView::update(finevk::CommandPool& commandPool, const MeshData& meshData) {
    // Empty mesh - release resources
    if (meshData.isEmpty()) {
        release();
        return;
    }

    if (!mesh_) {
        // Can't update without existing mesh - this shouldn't happen if
        // caller checked canUpdateInPlace first
        return;
    }

    // Determine if we're using 16-bit indices (check stored type)
    bool use16BitIndices = (mesh_->indexType() == VK_INDEX_TYPE_UINT16);

    if (use16BitIndices) {
        // Convert indices to 16-bit
        std::vector<uint16_t> indices16(meshData.indices.size());
        for (size_t i = 0; i < meshData.indices.size(); ++i) {
            indices16[i] = static_cast<uint16_t>(meshData.indices[i]);
        }
        mesh_->update(commandPool,
                      meshData.vertices.data(), meshData.vertices.size(),
                      indices16.data(), indices16.size());
    } else {
        mesh_->update(commandPool,
                      meshData.vertices.data(), meshData.vertices.size(),
                      meshData.indices.data(), meshData.indices.size());
    }

    indexCount_ = static_cast<uint32_t>(meshData.indices.size());
    vertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
    dirty_ = false;
}

void SubChunkView::release() {
    mesh_.reset();
    indexCount_ = 0;
    vertexCount_ = 0;
}

void SubChunkView::bind(finevk::CommandBuffer& cmd) const {
    if (mesh_) {
        mesh_->bind(cmd);
    }
}

void SubChunkView::draw(finevk::CommandBuffer& cmd, uint32_t instanceCount) const {
    if (mesh_) {
        mesh_->draw(cmd, instanceCount);
    }
}

}  // namespace finevox
