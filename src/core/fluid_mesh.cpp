#include "finevox/core/fluid_mesh.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/fluid_layer.hpp"
#include "finevox/core/fluid_type.hpp"
#include "finevox/core/fluid_registry.hpp"

namespace finevox {

// ============================================================================
// Face Data (same geometry as MeshBuilder::FACE_DATA)
// ============================================================================

const std::array<FluidMeshBuilder::FaceData, 6> FluidMeshBuilder::FACE_DATA = {{
    // [0] NegX
    {{{
        {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}
    }}, {-1.0f, 0.0f, 0.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}},
    // [1] PosX
    {{{
        {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}
    }}, {1.0f, 0.0f, 0.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}},
    // [2] NegY
    {{{
        {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}
    }}, {0.0f, -1.0f, 0.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}},
    // [3] PosY
    {{{
        {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}
    }}, {0.0f, 1.0f, 0.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}},
    // [4] NegZ
    {{{
        {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}
    }}, {0.0f, 0.0f, -1.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}},
    // [5] PosZ
    {{{
        {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
        {0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}
    }}, {0.0f, 0.0f, 1.0f}, {{{0.0f,0.0f},{1.0f,0.0f},{1.0f,1.0f},{0.0f,1.0f}}}}
}};

// ============================================================================
// Surface Height
// ============================================================================

float FluidMeshBuilder::surfaceHeight(uint8_t level, int32_t maxLevel) {
    if (level == FLUID_SOURCE_LEVEL) {
        // Source blocks render slightly below full block height
        return 14.0f / 16.0f;
    }
    // Flowing: scale proportionally, capped at source height (14/16).
    // level/maxLevel == 1.0 would equal full block height, exceeding source blocks.
    static constexpr float kSourceHeight = 14.0f / 16.0f;
    return static_cast<float>(level) / static_cast<float>(maxLevel) * kSourceHeight;
}

// ============================================================================
// Face Culling
// ============================================================================

bool FluidMeshBuilder::shouldCullFace(
    Face face,
    FluidTypeId type,
    uint8_t level,
    FluidTypeId neighborFluidType,
    uint8_t neighborFluidLevel,
    bool neighborIsSolid
) const {
    // Always cull faces against full solid blocks
    if (neighborIsSolid) return true;

    // If neighbor has same fluid type, apply level-based culling
    if (neighborFluidType == type && !neighborFluidType.isEmpty()) {
        switch (face) {
            case Face::PosY:
                // Cull top face whenever any same fluid is directly above —
                // the face is submerged regardless of the exact level ratio.
                return neighborFluidLevel > 0;
            case Face::NegY:
                // Cull bottom face if any same fluid below
                return neighborFluidLevel > 0;
            default:
                // With smooth corner heights, adjacent same-fluid blocks share identical
                // heights at their common edge, so side faces between them are interior
                // to the volume and invisible. Cull whenever any same fluid is present.
                return neighborFluidLevel > 0;
        }
    }

    return false;
}

// ============================================================================
// Corner Height Computation
// ============================================================================

std::array<float, 4> FluidMeshBuilder::computeCornerHeights(
    BlockCoord worldPos,
    FluidTypeId type,
    uint8_t level,
    const FluidType& ft,
    const FluidNeighborProvider& fluidNeighbor
) const {
    // If this block has same fluid directly above, all corners are 1.0.
    // (Block is submerged — top face will be culled, but side top-edges still need 1.0.)
    BlockCoord above{worldPos.x, worldPos.y + 1, worldPos.z};
    auto [aboveFluid, aboveLevel] = fluidNeighbor(above);
    if (aboveFluid == type && aboveLevel > 0) {
        return {1.0f, 1.0f, 1.0f, 1.0f};
    }

    // Corner indexing: idx = cx + cz*2
    //   cx=0 → NegX side (x=0),  cx=1 → PosX side (x=1)
    //   cz=0 → NegZ side (z=0),  cz=1 → PosZ side (z=1)
    //
    // The 4 blocks sharing each corner (dx, dz offsets from current block):
    //   idx 0 (x=0,z=0): (0,0) (-1,0) (0,-1) (-1,-1)
    //   idx 1 (x=1,z=0): (0,0) (+1,0) (0,-1) (+1,-1)
    //   idx 2 (x=0,z=1): (0,0) (-1,0) (0,+1) (-1,+1)
    //   idx 3 (x=1,z=1): (0,0) (+1,0) (0,+1) (+1,+1)
    static const int32_t kOffsets[4][4][2] = {
        {{ 0, 0}, {-1, 0}, { 0,-1}, {-1,-1}},
        {{ 0, 0}, { 1, 0}, { 0,-1}, { 1,-1}},
        {{ 0, 0}, {-1, 0}, { 0, 1}, {-1, 1}},
        {{ 0, 0}, { 1, 0}, { 0, 1}, { 1, 1}},
    };

    std::array<float, 4> result;
    for (int c = 0; c < 4; ++c) {
        float sum = 0.0f;
        int count = 0;
        for (int n = 0; n < 4; ++n) {
            BlockCoord nPos{
                worldPos.x + kOffsets[c][n][0],
                worldPos.y,
                worldPos.z + kOffsets[c][n][1]
            };
            auto [nFluid, nLevel] = fluidNeighbor(nPos);
            if (nFluid == type && nLevel > 0) {
                // If this neighbor has fluid above, its surface is at 1.0
                BlockCoord nAbove{nPos.x, nPos.y + 1, nPos.z};
                auto [nabFluid, nabLevel] = fluidNeighbor(nAbove);
                float h = (nabFluid == type && nabLevel > 0)
                    ? 1.0f
                    : surfaceHeight(nLevel, ft.maxLevel);
                sum += h;
                count++;
            }
        }
        // Fall back to this block's own height if no same-fluid neighbors at this corner
        result[c] = (count > 0)
            ? sum / static_cast<float>(count)
            : surfaceHeight(level, ft.maxLevel);
    }
    return result;
}

// ============================================================================
// Face Generation
// ============================================================================

void FluidMeshBuilder::addFluidFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    const std::array<float, 4>& cornerHeights,
    const glm::vec4& tintColor,
    float skyLight,
    float blockLight
) {
    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        glm::vec3 pos = blockPos + faceData.positions[i];

        // For non-bottom faces, raise top-edge vertices using per-corner heights.
        // Corner index: cx + cz*2, derived from the vertex's XZ position.
        if (face != Face::NegY && faceData.positions[i].y > 0.5f) {
            int cx = (faceData.positions[i].x > 0.5f) ? 1 : 0;
            int cz = (faceData.positions[i].z > 0.5f) ? 1 : 0;
            pos.y = blockPos.y + cornerHeights[cx + cz * 2];
        }

        vertex.position = pos;
        vertex.normal = faceData.normal;
        vertex.texCoord = faceData.uvOffsets[i];  // Standard 0-1 UVs
        vertex.tileBounds = tintColor;             // Repurposed as tint color
        vertex.ao = tintColor.a;                   // Repurposed as alpha
        vertex.skyLight = skyLight;
        vertex.blockLight = blockLight;
        mesh.vertices.push_back(vertex);
    }

    // Two triangles: 0-1-2 and 0-2-3
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 1);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 3);
}

// ============================================================================
// Main Mesh Building
// ============================================================================

MeshData FluidMeshBuilder::buildFluidMesh(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const FluidNeighborProvider& fluidNeighbor,
    const BlockSolidProvider& solidProvider
) {
    MeshData mesh;

    // Early exit if no fluid layer
    if (!subChunk.hasFluidLayer()) return mesh;

    const FluidLayer* layer = subChunk.fluidLayer();
    if (!layer || layer->isEmpty()) return mesh;

    // Reserve approximate space (non-empty count * ~3 faces * 4 verts)
    size_t estimatedFaces = layer->nonEmptyCount() * 3;
    mesh.reserve(estimatedFaces * 4, estimatedFaces * 6);

    // SubChunk world origin
    BlockCoord origin(
        chunkPos.x * SubChunk::SIZE,
        chunkPos.y * SubChunk::SIZE,
        chunkPos.z * SubChunk::SIZE
    );

    // Iterate all cells in the fluid layer
    for (int32_t y = 0; y < SubChunk::SIZE; ++y) {
        for (int32_t z = 0; z < SubChunk::SIZE; ++z) {
            for (int32_t x = 0; x < SubChunk::SIZE; ++x) {
                FluidCell cell = layer->getCell(x, y, z);
                if (cell.isEmpty()) continue;

                FluidTypeId fluidType = layer->getFluidType(x, y, z);
                uint8_t level = cell.level;

                // Look up fluid properties
                const FluidType* ft = FluidRegistry::global().getType(fluidType);
                if (!ft) continue;

                glm::vec4 tintColor = ft->tintColor;

                // World position of this block
                BlockCoord worldPos(origin.x + x, origin.y + y, origin.z + z);

                // Compute per-corner heights for smooth surface blending
                std::array<float, 4> cornerHeights =
                    computeCornerHeights(worldPos, fluidType, level, *ft, fluidNeighbor);

                // Local position for mesh vertices
                glm::vec3 localPos(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)
                );

                // Sample light at the fluid position (from the air/fluid space)
                float skyLight = 1.0f;
                float blockLight = 0.0f;
                if (lightProvider_) {
                    uint8_t packed = lightProvider_(worldPos);
                    skyLight = static_cast<float>(packed >> 4) / 15.0f;
                    blockLight = static_cast<float>(packed & 0x0F) / 15.0f;
                }

                // Check each face
                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    Face face = static_cast<Face>(faceIdx);
                    BlockCoord neighborPos = worldPos.neighbor(face);

                    // Query neighbor fluid
                    auto [neighborFluid, neighborLevel] = fluidNeighbor(neighborPos);

                    // Query if neighbor is a solid block
                    bool neighborSolid = solidProvider(neighborPos);

                    if (shouldCullFace(face, fluidType, level, neighborFluid, neighborLevel, neighborSolid)) {
                        continue;
                    }

                    addFluidFace(mesh, localPos, face, cornerHeights, tintColor, skyLight, blockLight);
                }
            }
        }
    }

    return mesh;
}

}  // namespace finevox
