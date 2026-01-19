#include "finevox/mesh.hpp"
#include "finevox/subchunk.hpp"
#include "finevox/world.hpp"

namespace finevox {

// ============================================================================
// Face vertex data - static initialization
// ============================================================================

// Face data for each of the 6 faces
// Positions are relative to block corner (0,0,0), CCW winding when looking at front
// UV offsets map to texture coordinates
// IMPORTANT: Array order must match Face enum: NegX=0, PosX=1, NegY=2, PosY=3, NegZ=4, PosZ=5
const std::array<MeshBuilder::FaceData, 6> MeshBuilder::FACE_DATA = {{
    // [0] NegX (-X face) - left side of block
    {
        .positions = {{
            {0.0f, 0.0f, 1.0f},  // bottom-back  (v0)
            {0.0f, 0.0f, 0.0f},  // bottom-front (v1)
            {0.0f, 1.0f, 0.0f},  // top-front    (v2)
            {0.0f, 1.0f, 1.0f}   // top-back     (v3)
        }},
        .normal = {-1.0f, 0.0f, 0.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    },
    // [1] PosX (+X face) - right side of block
    {
        .positions = {{
            {1.0f, 0.0f, 0.0f},  // bottom-front (v0)
            {1.0f, 0.0f, 1.0f},  // bottom-back  (v1)
            {1.0f, 1.0f, 1.0f},  // top-back     (v2)
            {1.0f, 1.0f, 0.0f}   // top-front    (v3)
        }},
        .normal = {1.0f, 0.0f, 0.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    },
    // [2] NegY (-Y face) - bottom of block
    {
        .positions = {{
            {0.0f, 0.0f, 1.0f},  // front-left  (v0)
            {1.0f, 0.0f, 1.0f},  // front-right (v1)
            {1.0f, 0.0f, 0.0f},  // back-right  (v2)
            {0.0f, 0.0f, 0.0f}   // back-left   (v3)
        }},
        .normal = {0.0f, -1.0f, 0.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    },
    // [3] PosY (+Y face) - top of block
    {
        .positions = {{
            {0.0f, 1.0f, 0.0f},  // back-left   (v0)
            {1.0f, 1.0f, 0.0f},  // back-right  (v1)
            {1.0f, 1.0f, 1.0f},  // front-right (v2)
            {0.0f, 1.0f, 1.0f}   // front-left  (v3)
        }},
        .normal = {0.0f, 1.0f, 0.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    },
    // [4] NegZ (-Z face) - back of block
    {
        .positions = {{
            {0.0f, 0.0f, 0.0f},  // bottom-left  (v0)
            {1.0f, 0.0f, 0.0f},  // bottom-right (v1)
            {1.0f, 1.0f, 0.0f},  // top-right    (v2)
            {0.0f, 1.0f, 0.0f}   // top-left     (v3)
        }},
        .normal = {0.0f, 0.0f, -1.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    },
    // [5] PosZ (+Z face) - front of block
    {
        .positions = {{
            {1.0f, 0.0f, 1.0f},  // bottom-right (v0)
            {0.0f, 0.0f, 1.0f},  // bottom-left  (v1)
            {0.0f, 1.0f, 1.0f},  // top-left     (v2)
            {1.0f, 1.0f, 1.0f}   // top-right    (v3)
        }},
        .normal = {0.0f, 0.0f, 1.0f},
        .uvOffsets = {{
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {1.0f, 1.0f},
            {0.0f, 1.0f}
        }}
    }
}};

// ============================================================================
// MeshBuilder implementation
// ============================================================================

MeshBuilder::MeshBuilder() = default;

MeshData MeshBuilder::buildSubChunkMesh(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& opaqueProvider,
    const BlockTextureProvider& textureProvider
) {
    MeshData mesh;

    // Early out if subchunk is empty
    if (subChunk.isEmpty()) {
        return mesh;
    }

    // Reserve approximate space (assume ~1/6 of faces are visible on average)
    // Each visible face = 4 vertices + 6 indices
    size_t estimatedFaces = subChunk.nonAirCount();
    mesh.reserve(estimatedFaces * 4, estimatedFaces * 6);

    // Convert chunk position to world block coordinates (corner of subchunk)
    BlockPos subChunkWorldOrigin(
        chunkPos.x * SubChunk::SIZE,
        chunkPos.y * SubChunk::SIZE,
        chunkPos.z * SubChunk::SIZE
    );

    // Iterate through all blocks in the subchunk
    for (int32_t y = 0; y < SubChunk::SIZE; ++y) {
        for (int32_t z = 0; z < SubChunk::SIZE; ++z) {
            for (int32_t x = 0; x < SubChunk::SIZE; ++x) {
                BlockTypeId blockType = subChunk.getBlock(x, y, z);

                // Skip air blocks
                if (blockType == AIR_BLOCK_TYPE) {
                    continue;
                }

                // World position of this block
                BlockPos blockWorldPos(
                    subChunkWorldOrigin.x + x,
                    subChunkWorldOrigin.y + y,
                    subChunkWorldOrigin.z + z
                );

                // Local position within subchunk (for mesh vertices)
                glm::vec3 localPos(
                    static_cast<float>(x),
                    static_cast<float>(y),
                    static_cast<float>(z)
                );

                // Check each face
                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    Face face = static_cast<Face>(faceIdx);

                    // Get neighbor position
                    BlockPos neighborPos = blockWorldPos.neighbor(face);

                    // Check if neighbor is opaque (if so, this face is hidden)
                    // Skip this check if face culling is disabled (debug mode)
                    if (!disableFaceCulling_ && opaqueProvider(neighborPos)) {
                        continue;  // Face is hidden, don't render
                    }

                    // Get texture UVs for this face
                    glm::vec4 uvBounds = textureProvider(blockType, face);

                    // Calculate AO for this face
                    std::array<float, 4> aoValues;
                    if (calculateAO_) {
                        aoValues = getFaceAO(blockWorldPos, face, opaqueProvider);
                    } else {
                        aoValues = {1.0f, 1.0f, 1.0f, 1.0f};
                    }

                    // Add the face to the mesh
                    addFace(mesh, localPos, face, uvBounds, aoValues);
                }
            }
        }
    }

    return mesh;
}

MeshData MeshBuilder::buildSubChunkMesh(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const World& world,
    const BlockTextureProvider& textureProvider
) {
    // Create opaque provider that checks the world
    BlockOpaqueProvider opaqueProvider = [&world](const BlockPos& pos) -> bool {
        BlockTypeId type = world.getBlock(pos);
        // For now, any non-air block is considered opaque
        // TODO: Add transparency support via block type registry
        return type != AIR_BLOCK_TYPE;
    };

    return buildSubChunkMesh(subChunk, chunkPos, opaqueProvider, textureProvider);
}

void MeshBuilder::addFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    const glm::vec4& uvBounds,
    const std::array<float, 4>& aoValues
) {
    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

    // Extract UV bounds
    float minU = uvBounds.x;
    float minV = uvBounds.y;
    float maxU = uvBounds.z;
    float maxV = uvBounds.w;

    // Add 4 vertices for the face
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        vertex.position = blockPos + faceData.positions[i];
        vertex.normal = faceData.normal;

        // Map UV offsets to actual UV coordinates
        vertex.texCoord = glm::vec2(
            minU + faceData.uvOffsets[i].x * (maxU - minU),
            minV + faceData.uvOffsets[i].y * (maxV - minV)
        );

        vertex.ao = aoValues[i];
        mesh.vertices.push_back(vertex);
    }

    // Add 6 indices for 2 triangles
    // Vertices are in order: v0, v1, v2, v3 forming a quad
    // Standard quad triangulation: 0-1-2 and 0-2-3
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 1);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 3);
}

float MeshBuilder::calculateCornerAO(bool side1, bool side2, bool corner) const {
    // Classic Minecraft-style ambient occlusion
    // Based on "0fps" AO algorithm
    if (side1 && side2) {
        // Both sides solid = maximum shadow (corner is irrelevant)
        return 0.0f;
    }

    int solidCount = (side1 ? 1 : 0) + (side2 ? 1 : 0) + (corner ? 1 : 0);

    // Map solid count to AO value (0 solid = 1.0, 3 solid = 0.25)
    switch (solidCount) {
        case 0: return 1.0f;     // Fully lit
        case 1: return 0.75f;    // Light shadow
        case 2: return 0.5f;     // Medium shadow
        case 3: return 0.25f;    // Heavy shadow
        default: return 1.0f;
    }
}

std::array<float, 4> MeshBuilder::getFaceAO(
    const BlockPos& blockWorldPos,
    Face face,
    const BlockOpaqueProvider& opaqueProvider
) const {
    std::array<float, 4> aoValues;

    // For each face, we need to check blocks around the face to calculate AO
    // The pattern depends on which face we're rendering

    // Get the tangent directions for this face
    // These define "right" and "up" relative to the face normal
    glm::ivec3 tangent1, tangent2;

    switch (face) {
        case Face::PosX:
        case Face::NegX:
            tangent1 = {0, 0, 1};  // Z direction
            tangent2 = {0, 1, 0};  // Y direction
            break;
        case Face::PosY:
        case Face::NegY:
            tangent1 = {1, 0, 0};  // X direction
            tangent2 = {0, 0, 1};  // Z direction
            break;
        case Face::PosZ:
        case Face::NegZ:
            tangent1 = {1, 0, 0};  // X direction
            tangent2 = {0, 1, 0};  // Y direction
            break;
    }

    // Adjust tangent directions based on face direction (for consistent winding)
    if (face == Face::NegX || face == Face::PosY || face == Face::NegZ) {
        tangent1 = -tangent1;
    }

    // Get the face normal as offset
    BlockPos normalOffset = faceOffset(face);

    // The face is one block in the normal direction from the block position
    BlockPos facePos(
        blockWorldPos.x + normalOffset.x,
        blockWorldPos.y + normalOffset.y,
        blockWorldPos.z + normalOffset.z
    );

    // Check the 8 blocks around the face (at the same level as the face)
    // These are used for AO calculation
    // Layout (looking at face from outside):
    //   6 7 8
    //   3 X 5    (X = face center, not a block)
    //   0 1 2

    auto isOpaqueAt = [&](int dx, int dy) -> bool {
        BlockPos checkPos(
            facePos.x + tangent1.x * dx + tangent2.x * dy,
            facePos.y + tangent1.y * dx + tangent2.y * dy,
            facePos.z + tangent1.z * dx + tangent2.z * dy
        );
        return opaqueProvider(checkPos);
    };

    // Get opacity of neighboring blocks
    bool b0 = isOpaqueAt(-1, -1);  // bottom-left corner
    bool b1 = isOpaqueAt( 0, -1);  // bottom side
    bool b2 = isOpaqueAt( 1, -1);  // bottom-right corner
    bool b3 = isOpaqueAt(-1,  0);  // left side
    bool b5 = isOpaqueAt( 1,  0);  // right side
    bool b6 = isOpaqueAt(-1,  1);  // top-left corner
    bool b7 = isOpaqueAt( 0,  1);  // top side
    bool b8 = isOpaqueAt( 1,  1);  // top-right corner

    // Calculate AO for each corner of the face
    // Corners are in CCW order: bottom-left, bottom-right, top-right, top-left
    aoValues[0] = calculateCornerAO(b3, b1, b0);  // bottom-left
    aoValues[1] = calculateCornerAO(b1, b5, b2);  // bottom-right
    aoValues[2] = calculateCornerAO(b5, b7, b8);  // top-right
    aoValues[3] = calculateCornerAO(b7, b3, b6);  // top-left

    return aoValues;
}

}  // namespace finevox
