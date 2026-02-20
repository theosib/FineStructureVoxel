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
    // Flowing: height proportional to level
    return static_cast<float>(level) / static_cast<float>(maxLevel);
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
                // Cull top face if fluid above at >= our level
                return neighborFluidLevel >= level;
            case Face::NegY:
                // Cull bottom face if any same fluid below
                return neighborFluidLevel > 0;
            default:
                // Cull side faces if neighbor at >= our level
                return neighborFluidLevel >= level;
        }
    }

    return false;
}

// ============================================================================
// Face Generation
// ============================================================================

void FluidMeshBuilder::addFluidFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    float height,
    const glm::vec4& tintColor,
    float skyLight,
    float blockLight
) {
    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        glm::vec3 pos = blockPos + faceData.positions[i];

        // Clamp Y positions to fluid surface height for non-bottom faces
        if (face != Face::NegY) {
            // For top face: all vertices at surface height
            // For side faces: top edge vertices clamped to surface height
            if (faceData.positions[i].y > 0.5f) {
                pos.y = blockPos.y + height;
            }
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

                float height = surfaceHeight(level, ft->maxLevel);
                glm::vec4 tintColor = ft->tintColor;

                // World position of this block
                BlockCoord worldPos(origin.x + x, origin.y + y, origin.z + z);

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

                    addFluidFace(mesh, localPos, face, height, tintColor, skyLight, blockLight);
                }
            }
        }
    }

    return mesh;
}

}  // namespace finevox
