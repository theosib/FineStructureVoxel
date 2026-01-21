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
    // With greedy meshing, we'll use fewer vertices, but this is a safe upper bound
    size_t estimatedFaces = subChunk.nonAirCount();
    mesh.reserve(estimatedFaces * 4, estimatedFaces * 6);

    // Use greedy meshing if enabled, otherwise simple per-face meshing
    // No transparent provider = all blocks treated as opaque
    if (greedyMeshing_) {
        buildGreedyMesh(mesh, subChunk, chunkPos, opaqueProvider, textureProvider, nullptr, false);
    } else {
        buildSimpleMesh(mesh, subChunk, chunkPos, opaqueProvider, textureProvider, nullptr, false);
    }

    return mesh;
}

SubChunkMeshData MeshBuilder::buildSubChunkMeshSplit(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& opaqueProvider,
    const BlockTransparentProvider& transparentProvider,
    const BlockTextureProvider& textureProvider
) {
    SubChunkMeshData result;

    // Early out if subchunk is empty
    if (subChunk.isEmpty()) {
        return result;
    }

    // Reserve approximate space
    size_t estimatedFaces = subChunk.nonAirCount();
    result.opaque.reserve(estimatedFaces * 4, estimatedFaces * 6);
    result.transparent.reserve(estimatedFaces / 4, estimatedFaces / 4 * 6);  // Assume fewer transparent

    // Build opaque mesh first
    if (greedyMeshing_) {
        buildGreedyMesh(result.opaque, subChunk, chunkPos, opaqueProvider, textureProvider,
                        &transparentProvider, false);
        // Build transparent mesh (no greedy merging for transparent - need sorting)
        buildSimpleMesh(result.transparent, subChunk, chunkPos, opaqueProvider, textureProvider,
                        &transparentProvider, true);
    } else {
        buildSimpleMesh(result.opaque, subChunk, chunkPos, opaqueProvider, textureProvider,
                        &transparentProvider, false);
        buildSimpleMesh(result.transparent, subChunk, chunkPos, opaqueProvider, textureProvider,
                        &transparentProvider, true);
    }

    return result;
}

SubChunkMeshData MeshBuilder::buildSubChunkMeshSplit(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const World& world,
    const BlockTransparentProvider& transparentProvider,
    const BlockTextureProvider& textureProvider
) {
    // Create opaque provider that checks the world
    BlockOpaqueProvider opaqueProvider = [&world](const BlockPos& pos) -> bool {
        BlockTypeId type = world.getBlock(pos);
        return type != AIR_BLOCK_TYPE;
    };

    return buildSubChunkMeshSplit(subChunk, chunkPos, opaqueProvider, transparentProvider, textureProvider);
}

void MeshBuilder::buildSimpleMesh(
    MeshData& mesh,
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& opaqueProvider,
    const BlockTextureProvider& textureProvider,
    const BlockTransparentProvider* transparentProvider,
    bool buildTransparent
) {
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

                // Filter by transparency if provider is given
                if (transparentProvider) {
                    bool isTransparent = (*transparentProvider)(blockType);
                    if (isTransparent != buildTransparent) {
                        continue;  // Skip blocks that don't match the pass type
                    }
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
                    // For transparent blocks, only cull against opaque neighbors
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
}

void MeshBuilder::buildGreedyMesh(
    MeshData& mesh,
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& opaqueProvider,
    const BlockTextureProvider& textureProvider,
    const BlockTransparentProvider* transparentProvider,
    bool buildTransparent
) {
    // Process each face direction separately
    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        Face face = static_cast<Face>(faceIdx);
        greedyMeshFace(mesh, face, subChunk, chunkPos, opaqueProvider, textureProvider,
                       transparentProvider, buildTransparent);
    }
}

void MeshBuilder::greedyMeshFace(
    MeshData& mesh,
    Face face,
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& opaqueProvider,
    const BlockTextureProvider& textureProvider,
    const BlockTransparentProvider* transparentProvider,
    bool buildTransparent
) {
    constexpr int SIZE = SubChunk::SIZE;

    // Determine which axis is normal to this face, and which are tangent
    // For each face direction:
    //   normalAxis: the axis perpendicular to the face (0=X, 1=Y, 2=Z)
    //   uAxis, vAxis: the two axes parallel to the face
    int normalAxis, uAxis, vAxis;

    switch (face) {
        case Face::NegX: normalAxis = 0; uAxis = 2; vAxis = 1; break;
        case Face::PosX: normalAxis = 0; uAxis = 2; vAxis = 1; break;
        case Face::NegY: normalAxis = 1; uAxis = 0; vAxis = 2; break;
        case Face::PosY: normalAxis = 1; uAxis = 0; vAxis = 2; break;
        case Face::NegZ: normalAxis = 2; uAxis = 0; vAxis = 1; break;
        case Face::PosZ: normalAxis = 2; uAxis = 0; vAxis = 1; break;
        default: return;
    }

    // World origin of this subchunk
    BlockPos subChunkWorldOrigin(
        chunkPos.x * SIZE,
        chunkPos.y * SIZE,
        chunkPos.z * SIZE
    );

    // 2D mask array for this face direction
    // Each entry contains the block type and AO for a potentially visible face
    std::array<FaceMaskEntry, SIZE * SIZE> mask;

    // Process each slice perpendicular to the normal axis
    for (int slice = 0; slice < SIZE; ++slice) {
        // Clear the mask
        mask.fill(FaceMaskEntry{});

        // Build the mask for this slice
        for (int v = 0; v < SIZE; ++v) {
            for (int u = 0; u < SIZE; ++u) {
                // Convert (slice, u, v) to (x, y, z) based on face direction
                int coords[3];
                coords[normalAxis] = slice;
                coords[uAxis] = u;
                coords[vAxis] = v;

                int x = coords[0];
                int y = coords[1];
                int z = coords[2];

                BlockTypeId blockType = subChunk.getBlock(x, y, z);

                // Skip air blocks
                if (blockType == AIR_BLOCK_TYPE) {
                    continue;
                }

                // Filter by transparency if provider is given
                if (transparentProvider) {
                    bool isTransparent = (*transparentProvider)(blockType);
                    if (isTransparent != buildTransparent) {
                        continue;  // Skip blocks that don't match the pass type
                    }
                }

                // World position of this block
                BlockPos blockWorldPos(
                    subChunkWorldOrigin.x + x,
                    subChunkWorldOrigin.y + y,
                    subChunkWorldOrigin.z + z
                );

                // Check if neighbor (in face direction) is opaque
                BlockPos neighborPos = blockWorldPos.neighbor(face);
                if (!disableFaceCulling_ && opaqueProvider(neighborPos)) {
                    continue;  // Face is hidden
                }

                // This face is visible - add to mask
                int maskIdx = v * SIZE + u;
                mask[maskIdx].blockType = blockType;
                mask[maskIdx].uvBounds = textureProvider(blockType, face);

                if (calculateAO_) {
                    mask[maskIdx].aoValues = getFaceAO(blockWorldPos, face, opaqueProvider);
                }
            }
        }

        // Greedy merge the mask into quads
        // We use a simple row-by-row greedy algorithm
        std::array<bool, SIZE * SIZE> used;
        used.fill(false);

        for (int v = 0; v < SIZE; ++v) {
            for (int u = 0; u < SIZE; ++u) {
                int startIdx = v * SIZE + u;

                // Skip if already used or empty
                if (used[startIdx] || mask[startIdx].isEmpty()) {
                    continue;
                }

                const FaceMaskEntry& entry = mask[startIdx];

                // Find width: extend in U direction while faces match
                int width = 1;
                while (u + width < SIZE) {
                    int checkIdx = v * SIZE + (u + width);
                    if (used[checkIdx] || mask[checkIdx] != entry) {
                        break;
                    }
                    ++width;
                }

                // Find height: extend in V direction while entire row matches
                int height = 1;
                bool canExtend = true;
                while (canExtend && v + height < SIZE) {
                    for (int du = 0; du < width; ++du) {
                        int checkIdx = (v + height) * SIZE + (u + du);
                        if (used[checkIdx] || mask[checkIdx] != entry) {
                            canExtend = false;
                            break;
                        }
                    }
                    if (canExtend) {
                        ++height;
                    }
                }

                // Mark all cells in this quad as used
                for (int dv = 0; dv < height; ++dv) {
                    for (int du = 0; du < width; ++du) {
                        used[(v + dv) * SIZE + (u + du)] = true;
                    }
                }

                // Add the merged quad
                addGreedyQuad(mesh, face, slice, u, v, width, height, entry, textureProvider);
            }
        }
    }
}

void MeshBuilder::addGreedyQuad(
    MeshData& mesh,
    Face face,
    int sliceCoord,
    int startU, int startV,
    int width, int height,
    const FaceMaskEntry& entry,
    [[maybe_unused]] const BlockTextureProvider& textureProvider
) {
    // Determine axis mapping (same as in greedyMeshFace)
    int normalAxis, uAxis, vAxis;
    bool positiveNormal;

    switch (face) {
        case Face::NegX: normalAxis = 0; uAxis = 2; vAxis = 1; positiveNormal = false; break;
        case Face::PosX: normalAxis = 0; uAxis = 2; vAxis = 1; positiveNormal = true;  break;
        case Face::NegY: normalAxis = 1; uAxis = 0; vAxis = 2; positiveNormal = false; break;
        case Face::PosY: normalAxis = 1; uAxis = 0; vAxis = 2; positiveNormal = true;  break;
        case Face::NegZ: normalAxis = 2; uAxis = 0; vAxis = 1; positiveNormal = false; break;
        case Face::PosZ: normalAxis = 2; uAxis = 0; vAxis = 1; positiveNormal = true;  break;
        default: return;
    }

    // Calculate the 4 corners of the quad in local (subchunk) coordinates
    // The face is at sliceCoord along the normal axis
    // For positive-facing faces, the face is at sliceCoord + 1 (far side of block)
    float normalCoord = positiveNormal ? static_cast<float>(sliceCoord + 1) : static_cast<float>(sliceCoord);

    // Build corner positions
    glm::vec3 corners[4];
    float coords[3];

    // Corner order for CCW winding (matching FACE_DATA):
    // 0: (startU, startV)
    // 1: (startU + width, startV)
    // 2: (startU + width, startV + height)
    // 3: (startU, startV + height)

    // Adjust corner order based on face direction to maintain correct winding
    int uCoords[4], vCoords[4];

    // The winding needs to match what addFace produces
    // We need to be careful here - the FACE_DATA has specific corner ordering
    // For simplicity, we'll generate corners in a consistent order and rely on
    // the index ordering to produce correct winding

    switch (face) {
        case Face::NegX:
            // Looking at -X face from outside (from -X direction)
            // U=Z, V=Y, normal at X=sliceCoord
            uCoords[0] = startU + width; uCoords[1] = startU;            uCoords[2] = startU;            uCoords[3] = startU + width;
            vCoords[0] = startV;         vCoords[1] = startV;            vCoords[2] = startV + height;   vCoords[3] = startV + height;
            break;
        case Face::PosX:
            // Looking at +X face from outside (from +X direction)
            uCoords[0] = startU;         uCoords[1] = startU + width;    uCoords[2] = startU + width;    uCoords[3] = startU;
            vCoords[0] = startV;         vCoords[1] = startV;            vCoords[2] = startV + height;   vCoords[3] = startV + height;
            break;
        case Face::NegY:
            // Looking at -Y face from outside (from below)
            uCoords[0] = startU;         uCoords[1] = startU + width;    uCoords[2] = startU + width;    uCoords[3] = startU;
            vCoords[0] = startV + height; vCoords[1] = startV + height;  vCoords[2] = startV;            vCoords[3] = startV;
            break;
        case Face::PosY:
            // Looking at +Y face from outside (from above)
            uCoords[0] = startU;         uCoords[1] = startU + width;    uCoords[2] = startU + width;    uCoords[3] = startU;
            vCoords[0] = startV;         vCoords[1] = startV;            vCoords[2] = startV + height;   vCoords[3] = startV + height;
            break;
        case Face::NegZ:
            // Looking at -Z face from outside (from -Z direction)
            uCoords[0] = startU;         uCoords[1] = startU + width;    uCoords[2] = startU + width;    uCoords[3] = startU;
            vCoords[0] = startV;         vCoords[1] = startV;            vCoords[2] = startV + height;   vCoords[3] = startV + height;
            break;
        case Face::PosZ:
            // Looking at +Z face from outside (from +Z direction)
            uCoords[0] = startU + width; uCoords[1] = startU;            uCoords[2] = startU;            uCoords[3] = startU + width;
            vCoords[0] = startV;         vCoords[1] = startV;            vCoords[2] = startV + height;   vCoords[3] = startV + height;
            break;
        default:
            return;
    }

    for (int i = 0; i < 4; ++i) {
        coords[normalAxis] = normalCoord;
        coords[uAxis] = static_cast<float>(uCoords[i]);
        coords[vAxis] = static_cast<float>(vCoords[i]);
        corners[i] = glm::vec3(coords[0], coords[1], coords[2]);
    }

    // Get face normal
    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    glm::vec3 normal = faceData.normal;

    // Calculate UV coordinates for the merged quad with proper tiling support
    // The texture coordinates extend beyond the tile bounds (0 to width/height in tile space)
    // The shader uses fract() with tile bounds to wrap them properly within the atlas cell
    glm::vec4 tileBounds = entry.uvBounds;
    float minU = tileBounds.x;
    float minV = tileBounds.y;
    float maxU = tileBounds.z;
    float maxV = tileBounds.w;
    float tileWidth = maxU - minU;
    float tileHeight = maxV - minV;

    // UV coordinates that tile across the merged region
    // Corner 0: (0, 0) in tile space -> minU, minV
    // Corner 1: (width, 0) -> minU + width * tileWidth, minV
    // Corner 2: (width, height) -> minU + width * tileWidth, minV + height * tileHeight
    // Corner 3: (0, height) -> minU, minV + height * tileHeight
    glm::vec2 uvs[4] = {
        {minU, minV},
        {minU + static_cast<float>(width) * tileWidth, minV},
        {minU + static_cast<float>(width) * tileWidth, minV + static_cast<float>(height) * tileHeight},
        {minU, minV + static_cast<float>(height) * tileHeight}
    };

    // For greedy quads, we use uniform AO across the quad
    // (averaging would be complex and greedy meshing typically assumes uniform lighting)
    // Use the entry's AO values directly
    const auto& aoValues = entry.aoValues;

    // Add vertices
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        vertex.position = corners[i];
        vertex.normal = normal;
        vertex.texCoord = uvs[i];
        vertex.tileBounds = tileBounds;  // Pass tile bounds for shader wrapping
        vertex.ao = aoValues[i];
        mesh.vertices.push_back(vertex);
    }

    // Add indices (two triangles)
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 1);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 3);
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

        // Pass tile bounds for shader-based wrapping (for single blocks, UV equals tile bounds)
        vertex.tileBounds = uvBounds;

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

// ============================================================================
// LOD Mesh Generation
// ============================================================================

void MeshBuilder::addScaledFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    float blockScale,
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
    float tileWidth = maxU - minU;
    float tileHeight = maxV - minV;

    // Add 4 vertices for the face, scaled by blockScale
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        // Scale the face positions by blockScale
        vertex.position = blockPos + faceData.positions[i] * blockScale;
        vertex.normal = faceData.normal;

        // UV coordinates tile across the scaled block
        // A 2x2x2 LOD block should tile the texture 2x2 times
        vertex.texCoord = glm::vec2(
            minU + faceData.uvOffsets[i].x * blockScale * tileWidth,
            minV + faceData.uvOffsets[i].y * blockScale * tileHeight
        );

        // Pass tile bounds for shader-based wrapping
        vertex.tileBounds = uvBounds;

        vertex.ao = aoValues[i];
        mesh.vertices.push_back(vertex);
    }

    // Add 6 indices for 2 triangles
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 1);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 0);
    mesh.indices.push_back(baseVertex + 2);
    mesh.indices.push_back(baseVertex + 3);
}

MeshData MeshBuilder::buildLODMesh(
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockTextureProvider& textureProvider
) {
    // Simple version without neighbor culling - all non-air faces are rendered
    BlockOpaqueProvider alwaysTransparent = [](const BlockPos&) { return false; };
    return buildLODMesh(lodSubChunk, chunkPos, alwaysTransparent, textureProvider);
}

MeshData MeshBuilder::buildLODMesh(
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& neighborProvider,
    const BlockTextureProvider& textureProvider
) {
    MeshData mesh;

    if (lodSubChunk.isEmpty()) {
        return mesh;
    }

    int resolution = lodSubChunk.resolution();
    int grouping = lodSubChunk.grouping();
    float blockScale = static_cast<float>(grouping);

    // Reserve space based on expected geometry
    // Worst case: each LOD cell has 6 faces * 4 vertices
    mesh.reserve(resolution * resolution * resolution * 6 * 4,
                 resolution * resolution * resolution * 6 * 6);

    // Default AO values (no AO calculation for LOD - too expensive and not very visible)
    std::array<float, 4> defaultAO = {1.0f, 1.0f, 1.0f, 1.0f};

    // Iterate over all LOD cells
    for (int ly = 0; ly < resolution; ++ly) {
        for (int lz = 0; lz < resolution; ++lz) {
            for (int lx = 0; lx < resolution; ++lx) {
                BlockTypeId blockType = lodSubChunk.getBlock(lx, ly, lz);
                if (blockType == AIR_BLOCK_TYPE) {
                    continue;
                }

                // Calculate world block position for the LOD cell corner
                // LOD cell (lx, ly, lz) at grouping G corresponds to world blocks:
                // [lx*G, ly*G, lz*G] to [(lx+1)*G-1, (ly+1)*G-1, (lz+1)*G-1]
                float worldX = static_cast<float>(chunkPos.x * 16 + lx * grouping);
                float worldY = static_cast<float>(chunkPos.y * 16 + ly * grouping);
                float worldZ = static_cast<float>(chunkPos.z * 16 + lz * grouping);

                // Convert to local position within subchunk (0-16 range)
                float localX = static_cast<float>(lx * grouping);
                float localY = static_cast<float>(ly * grouping);
                float localZ = static_cast<float>(lz * grouping);
                glm::vec3 localPos(localX, localY, localZ);

                // Check each face
                for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                    Face face = static_cast<Face>(faceIdx);

                    // Check if neighbor is solid (in LOD space)
                    int nx = lx + (faceIdx == 1 ? 1 : (faceIdx == 0 ? -1 : 0));
                    int ny = ly + (faceIdx == 3 ? 1 : (faceIdx == 2 ? -1 : 0));
                    int nz = lz + (faceIdx == 5 ? 1 : (faceIdx == 4 ? -1 : 0));

                    bool neighborOpaque = false;

                    // Check if neighbor is within this LOD subchunk
                    if (nx >= 0 && nx < resolution &&
                        ny >= 0 && ny < resolution &&
                        nz >= 0 && nz < resolution) {
                        // Internal neighbor - check LOD data
                        neighborOpaque = (lodSubChunk.getBlock(nx, ny, nz) != AIR_BLOCK_TYPE);
                    } else {
                        // External neighbor - use the provided neighbor provider
                        // Calculate the world position of the neighbor block
                        // We check the world block adjacent to the LOD cell face
                        BlockPos neighborWorldPos;
                        auto normal = faceNormal(face);
                        neighborWorldPos.x = static_cast<int32_t>(worldX) + normal[0] * grouping;
                        neighborWorldPos.y = static_cast<int32_t>(worldY) + normal[1] * grouping;
                        neighborWorldPos.z = static_cast<int32_t>(worldZ) + normal[2] * grouping;
                        neighborOpaque = neighborProvider(neighborWorldPos);
                    }

                    if (neighborOpaque && !disableFaceCulling_) {
                        continue;  // Face is hidden
                    }

                    // Get texture UVs for this face
                    glm::vec4 uvBounds = textureProvider(blockType, face);

                    // Add the scaled face
                    addScaledFace(mesh, localPos, face, blockScale, uvBounds, defaultAO);
                }
            }
        }
    }

    return mesh;
}

}  // namespace finevox
