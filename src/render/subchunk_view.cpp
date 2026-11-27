#include "finevox/render/subchunk_view.hpp"

// FineStructureVK headers
#include <finevk/high/raw_mesh.hpp>
#include <finevk/device/logical_device.hpp>
#include <finevk/device/buffer.hpp>
#include <finevk/device/command.hpp>
#include <finevk/device/upload_batch.hpp>

namespace finevox::render {

SubChunkView::SubChunkView(ChunkPos pos)
    : pos_(pos)
{
}

SubChunkView::~SubChunkView() = default;

finevk::RawMeshPtr SubChunkView::takeMesh() {
    return std::move(mesh_);
}

finevk::RawMeshPtr SubChunkView::takeFluidMesh() {
    return std::move(fluidMesh_);
}

namespace {

// 16-bit indices where the vertex count allows (half the index memory)
std::vector<uint16_t> narrowIndices(const MeshData& meshData) {
    std::vector<uint16_t> out(meshData.indices.size());
    for (size_t i = 0; i < meshData.indices.size(); ++i) {
        out[i] = static_cast<uint16_t>(meshData.indices[i]);
    }
    return out;
}

// A new GPU mesh holding meshData, uploaded now or staged in batch
finevk::RawMeshPtr buildMesh(finevk::LogicalDevice& device, finevk::CommandPool& commandPool,
                             const MeshData& meshData, float capacityMultiplier,
                             finevk::UploadBatch* batch, size_t& gpuBytes) {
    bool use16BitIndices = meshData.vertexCount() <= 65535;

    auto builder = finevk::RawMesh::create(&device)
        .vertexLayout(getChunkVertexBindingDescription(),
                      getChunkVertexAttributeDescriptions())
        .vertices(meshData.vertices.data(), meshData.vertices.size())
        .reserveCapacity(capacityMultiplier);

    // Kept alive until after build(): the builder holds a pointer
    std::vector<uint16_t> indices16;
    if (use16BitIndices) {
        indices16 = narrowIndices(meshData);
        builder.indices(indices16.data(), indices16.size());
    } else {
        builder.indices(meshData.indices.data(), meshData.indices.size());
    }

    size_t indexSize = use16BitIndices ? sizeof(uint16_t) : sizeof(uint32_t);
    gpuBytes = static_cast<size_t>(
        (meshData.vertices.size() * sizeof(ChunkVertex) +
         meshData.indices.size() * indexSize) * capacityMultiplier);

    return batch ? builder.build(batch) : builder.build(commandPool);
}

// New contents for an existing GPU mesh, uploaded now or staged in batch
void updateMesh(finevk::RawMesh& mesh, finevk::CommandPool& commandPool,
                const MeshData& meshData, finevk::UploadBatch* batch) {
    // The index type is the one the mesh was built with
    std::vector<uint16_t> indices16;
    const void* indices = meshData.indices.data();
    if (mesh.indexType() == VK_INDEX_TYPE_UINT16) {
        indices16 = narrowIndices(meshData);
        indices = indices16.data();
    }
    if (batch) {
        mesh.update(batch, meshData.vertices.data(), meshData.vertices.size(),
                    indices, meshData.indices.size());
    } else {
        mesh.update(commandPool, meshData.vertices.data(), meshData.vertices.size(),
                    indices, meshData.indices.size());
    }
}

}  // namespace

void SubChunkView::upload(
    finevk::LogicalDevice& device,
    finevk::CommandPool& commandPool,
    const MeshData& meshData,
    float capacityMultiplier,
    finevk::UploadBatch* batch
) {
    // Empty mesh - release resources
    if (meshData.isEmpty()) {
        release();
        return;
    }

    mesh_ = buildMesh(device, commandPool, meshData, capacityMultiplier, batch, gpuMemoryBytes_);
    indexCount_ = static_cast<uint32_t>(meshData.indices.size());
    vertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
}

bool SubChunkView::canUpdateInPlace(const MeshData& meshData) const {
    if (!mesh_) {
        return false;
    }
    return mesh_->canUpdateInPlace(meshData.vertices.size(), meshData.indices.size());
}

void SubChunkView::update(finevk::CommandPool& commandPool, const MeshData& meshData,
                          finevk::UploadBatch* batch) {
    // Empty mesh - release resources
    if (meshData.isEmpty()) {
        release();
        return;
    }

    // Can't update without existing mesh - the caller checks canUpdateInPlace first
    if (!mesh_) return;

    updateMesh(*mesh_, commandPool, meshData, batch);
    indexCount_ = static_cast<uint32_t>(meshData.indices.size());
    vertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
    // gpuMemoryBytes_ stays the same since capacity doesn't change on in-place update
}

void SubChunkView::release() {
    mesh_.reset();
    indexCount_ = 0;
    vertexCount_ = 0;
    gpuMemoryBytes_ = 0;
    releaseFluid();
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

// ============================================================================
// Fluid Mesh
// ============================================================================

void SubChunkView::uploadFluid(
    finevk::LogicalDevice& device,
    finevk::CommandPool& commandPool,
    const MeshData& meshData,
    float capacityMultiplier,
    finevk::UploadBatch* batch
) {
    if (meshData.isEmpty()) {
        releaseFluid();
        return;
    }

    fluidMesh_ = buildMesh(device, commandPool, meshData, capacityMultiplier, batch,
                           fluidGpuMemoryBytes_);
    fluidIndexCount_ = static_cast<uint32_t>(meshData.indices.size());
    fluidVertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
}

bool SubChunkView::canUpdateFluidInPlace(const MeshData& meshData) const {
    if (!fluidMesh_) return false;
    return fluidMesh_->canUpdateInPlace(meshData.vertices.size(), meshData.indices.size());
}

void SubChunkView::updateFluid(finevk::CommandPool& commandPool, const MeshData& meshData,
                               finevk::UploadBatch* batch) {
    if (meshData.isEmpty()) {
        releaseFluid();
        return;
    }

    if (!fluidMesh_) return;

    updateMesh(*fluidMesh_, commandPool, meshData, batch);
    fluidIndexCount_ = static_cast<uint32_t>(meshData.indices.size());
    fluidVertexCount_ = static_cast<uint32_t>(meshData.vertices.size());
}

void SubChunkView::releaseFluid() {
    fluidMesh_.reset();
    fluidIndexCount_ = 0;
    fluidVertexCount_ = 0;
    fluidGpuMemoryBytes_ = 0;
}

void SubChunkView::bindFluid(finevk::CommandBuffer& cmd) const {
    if (fluidMesh_) {
        fluidMesh_->bind(cmd);
    }
}

void SubChunkView::drawFluid(finevk::CommandBuffer& cmd, uint32_t instanceCount) const {
    if (fluidMesh_) {
        fluidMesh_->draw(cmd, instanceCount);
    }
}

}  // namespace finevox::render
