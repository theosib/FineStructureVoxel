#pragma once

/**
 * @file fluid_mesh.hpp
 * @brief Generates mesh geometry for fluid rendering
 *
 * Uses ChunkVertex format with field repurposing:
 *   - tileBounds → tint color (RGBA from FluidType)
 *   - ao → alpha (from FluidType tintColor.a)
 *
 * This allows reuse of the same vertex input layout and SubChunkView
 * upload code as block meshes, while fluid shaders interpret the
 * fields differently.
 */

#include "finevox/core/mesh.hpp"
#include "finevox/core/fluid_type_id.hpp"
#include "finevox/core/position.hpp"
#include <functional>
#include <utility>

namespace finevox {

// Forward declarations
class SubChunk;
struct FluidType;

/// Callback to query fluid at any world position (for cross-subchunk neighbors)
/// Returns (FluidTypeId, level) pair. Returns (EMPTY_FLUID_TYPE, 0) if no fluid.
using FluidNeighborProvider = std::function<std::pair<FluidTypeId, uint8_t>(BlockCoord)>;

/// Callback to check if a block at a world position is a full solid block
/// (used to cull fluid faces against opaque blocks)
using BlockSolidProvider = std::function<bool(BlockCoord)>;

/// Generates fluid mesh geometry for a SubChunk.
/// Call buildFluidMesh() from a worker thread to produce MeshData
/// that can be uploaded to GPU using the same SubChunkView infrastructure.
class FluidMeshBuilder {
public:
    FluidMeshBuilder() = default;

    /// Set the light provider for sampling sky/block light at fluid positions
    void setLightProvider(BlockLightProvider provider) { lightProvider_ = std::move(provider); }

    /// Build fluid mesh for a subchunk.
    /// @param subChunk The subchunk containing fluid data
    /// @param chunkPos The subchunk's position in chunk coordinates
    /// @param fluidNeighbor Callback to query fluid at world positions (for neighbor culling)
    /// @param solidProvider Callback to check if a block is solid (for face culling)
    /// @return MeshData containing fluid geometry (empty if no fluid)
    [[nodiscard]] MeshData buildFluidMesh(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const FluidNeighborProvider& fluidNeighbor,
        const BlockSolidProvider& solidProvider
    );

    /// Calculate fluid surface height for a given level
    /// @param level Fluid level (1-14 flowing, 15 source)
    /// @param maxLevel Maximum flowing level from FluidType (typically 14)
    /// @return Height as fraction of block (0.0 to ~0.875)
    [[nodiscard]] static float surfaceHeight(uint8_t level, int32_t maxLevel);

private:
    BlockLightProvider lightProvider_;

    /// Face vertex positions (same as MeshBuilder::FACE_DATA but accessible here)
    struct FaceData {
        std::array<glm::vec3, 4> positions;
        glm::vec3 normal;
        std::array<glm::vec2, 4> uvOffsets;
    };
    static const std::array<FaceData, 6> FACE_DATA;

    /// Add a fluid face to the mesh using per-corner heights for smooth surfaces.
    /// cornerHeights[cx + cz*2] where cx=0/1 for NegX/PosX, cz=0/1 for NegZ/PosZ.
    void addFluidFace(
        MeshData& mesh,
        const glm::vec3& blockPos,
        Face face,
        const std::array<float, 4>& cornerHeights,
        const glm::vec4& tintColor,
        float skyLight,
        float blockLight
    );

    /// Compute per-corner surface heights by averaging surrounding fluid levels.
    /// Produces smooth slopes between adjacent blocks of different flow levels.
    [[nodiscard]] std::array<float, 4> computeCornerHeights(
        BlockCoord worldPos,
        FluidTypeId type,
        uint8_t level,
        const FluidType& ft,
        const FluidNeighborProvider& fluidNeighbor
    ) const;

    /// Check if a fluid face should be culled
    /// @return true if the face should NOT be rendered
    [[nodiscard]] bool shouldCullFace(
        Face face,
        FluidTypeId type,
        uint8_t level,
        FluidTypeId neighborFluidType,
        uint8_t neighborFluidLevel,
        bool neighborIsSolid
    ) const;
};

}  // namespace finevox
