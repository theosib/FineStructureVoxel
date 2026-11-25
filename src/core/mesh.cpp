#include "finevox/core/mesh.hpp"
#include "finevox/core/block_type.hpp"
#include "finevox/core/subchunk.hpp"
#include "finevox/core/world.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace finevox {

// ============================================================================
// Face vertex data - static initialization
// ============================================================================

// Face data for each of the 6 faces
// Positions are relative to block corner (0,0,0), CCW winding when looking at front
// UV offsets map to texture coordinates, with (0, 0) the top left of the image as a viewer
// outside the face sees it, so a side face's top edge shows the image's top row. A side's v0
// is at the viewer's bottom right.
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
            {1.0f, 1.0f},
            {0.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 0.0f}
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
            {1.0f, 1.0f},
            {0.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 0.0f}
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
            {0.0f, 0.0f, 0.0f},  // bottom-right (v0)
            {1.0f, 0.0f, 0.0f},  // bottom-left  (v1)
            {1.0f, 1.0f, 0.0f},  // top-left     (v2)
            {0.0f, 1.0f, 0.0f}   // top-right    (v3)
        }},
        .normal = {0.0f, 0.0f, -1.0f},
        .uvOffsets = {{
            {1.0f, 1.0f},
            {0.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 0.0f}
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
            {1.0f, 1.0f},
            {0.0f, 1.0f},
            {0.0f, 0.0f},
            {1.0f, 0.0f}
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

namespace {

// What shades its neighbours and hides their faces, when there's no face-by-face provider: a block
// that fills its cell, opaque or a plain cube (leaves). A partial shape doesn't. Counting one would
// darken the ground beside it while the ground in its own open part stays lit, leaving a hard edge.
bool fillsCell(const World& world, const BlockCoord& pos) {
    BlockTypeId type = world.getBlock(pos);
    if (type == AIR_BLOCK_TYPE) return false;
    const BlockType& block = BlockRegistry::global().getType(type);
    return block.isOpaque() || !block.hasCustomMesh();
}

}  // namespace

SubChunkMeshData MeshBuilder::buildSubChunkMeshSplit(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const World& world,
    const BlockTransparentProvider& transparentProvider,
    const BlockTextureProvider& textureProvider
) {
    BlockOpaqueProvider opaqueProvider = [&world](const BlockCoord& pos) { return fillsCell(world, pos); };

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
    BlockCoord subChunkWorldOrigin(
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
                BlockCoord blockWorldPos(
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

                // Check if this block has custom geometry
                const BlockGeometry* customGeom = geometryFor(blockType);

                if (customGeom && !customGeom->isEmpty()) {
                    // Render custom geometry faces
                    // Get base light level for the block (sample from center)
                    float baseSkyLight = 1.0f;
                    float baseBlockLight = 0.0f;
                    if ((smoothLighting_ || flatLighting_) && lightProvider_) {
                        // Sample light from the block's position (center approximation)
                        uint8_t packed = lightProvider_(blockWorldPos);
                        baseSkyLight = static_cast<float>(packed >> 4) / 15.0f;
                        baseBlockLight = static_cast<float>(packed & 0x0F) / 15.0f;
                    }

                    const uint16_t joined = customGeom->hasConnections() ? connectionMask(blockWorldPos, blockType) : 0;

                    // Render each face in the custom geometry
                    for (const auto& faceGeom : customGeom->faces()) {
                        if (!faceGeom.isValid()) {
                            continue;
                        }
                        if (faceGeom.connection >= 0 &&
                            faceGeom.whenOpen == bool(joined & (1 << faceGeom.connection))) {
                            continue;
                        }

                        // For standard faces (0-5), check if neighbor occludes this face
                        if (faceGeom.isStandardFace() && !disableFaceCulling_) {
                            Face face = static_cast<Face>(faceGeom.faceIndex);
                            BlockCoord neighborPos = blockWorldPos.neighbor(face);
                            if (faceOccludesProvider_) {
                                Face oppFace = oppositeFace(face);
                                if (faceOccludesProvider_(neighborPos, oppFace)) {
                                    continue;  // Neighbor's face toward us is solid
                                }
                            } else if (opaqueProvider(neighborPos)) {
                                continue;  // Standard face hidden by opaque neighbor
                            }
                        }

                        // Get texture UVs - use the standard face if available, else Face::PosY as default
                        Face texFace = faceGeom.isStandardFace()
                            ? static_cast<Face>(faceGeom.faceIndex)
                            : Face::PosY;
                        glm::vec4 uvBounds = faceGeom.hasTextureUV ? faceGeom.textureUV : textureProvider(blockType, texFace);

                        // Calculate lighting for this face
                        float faceSkyLight = baseSkyLight;
                        float faceBlockLight = baseBlockLight;
                        customFaceLight(faceGeom, blockWorldPos, faceSkyLight, faceBlockLight);

                        // Add the custom face
                        addCustomFace(mesh, localPos, faceGeom, uvBounds, 1.0f, faceSkyLight, faceBlockLight,
                                      faceTint(blockType, faceGeom, blockWorldPos));
                    }
                } else {
                    // Standard cube rendering
                    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
                        Face face = static_cast<Face>(faceIdx);

                        // Get neighbor position
                        BlockCoord neighborPos = blockWorldPos.neighbor(face);

                        // Check if neighbor occludes this face
                        // Skip this check if face culling is disabled (debug mode)
                        if (!disableFaceCulling_) {
                            if (faceOccludesProvider_) {
                                // Use directional occlusion check (handles partial blocks)
                                Face oppFace = oppositeFace(face);
                                if (faceOccludesProvider_(neighborPos, oppFace)) {
                                    continue;  // Neighbor's face toward us is solid
                                }
                            } else if (opaqueProvider(neighborPos)) {
                                // Legacy: any opaque neighbor hides this face
                                continue;
                            }
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

                        // Calculate lighting for this face
                        std::array<float, 4> skyLightValues;
                        std::array<float, 4> blockLightValues;
                        if (smoothLighting_ && lightProvider_) {
                            // Smooth lighting: sample 9 points, average to 4 corners
                            auto lightResult = getFaceSkyBlockLight(blockWorldPos, face);
                            skyLightValues = lightResult.sky;
                            blockLightValues = lightResult.block;
                        } else if (flatLighting_ && lightProvider_) {
                            // Flat lighting: sample 1 point, apply to all corners (shows raw L1 ball)
                            BlockCoord faceAirPos = blockWorldPos.neighbor(face);
                            uint8_t packed = lightProvider_(faceAirPos);
                            float skyVal = static_cast<float>(packed >> 4) / 15.0f;
                            float blockVal = static_cast<float>(packed & 0x0F) / 15.0f;
                            skyLightValues = {skyVal, skyVal, skyVal, skyVal};
                            blockLightValues = {blockVal, blockVal, blockVal, blockVal};
                        } else {
                            skyLightValues = {1.0f, 1.0f, 1.0f, 1.0f};
                            blockLightValues = {0.0f, 0.0f, 0.0f, 0.0f};
                        }

                        // Add the face to the mesh
                        glm::uvec4 blend(0u);
                        if (!buildTransparent) {
                            blend = faceBlend(blockType, blockWorldPos, face, opaqueProvider, textureProvider);
                        }
                        addFace(mesh, localPos, face, uvBounds, aoValues, skyLightValues, blockLightValues,
                                cubeTint(blockType, face, blockWorldPos), blend);
                    }
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
    // Process each face direction separately (greedy meshing for standard cube blocks)
    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        Face face = static_cast<Face>(faceIdx);
        greedyMeshFace(mesh, face, subChunk, chunkPos, opaqueProvider, textureProvider,
                       transparentProvider, buildTransparent);
    }

    // Second pass: render custom geometry blocks (can't be greedy-merged)
    if (geometryProvider_) {
        BlockCoord subChunkWorldOrigin(
            chunkPos.x * SubChunk::SIZE,
            chunkPos.y * SubChunk::SIZE,
            chunkPos.z * SubChunk::SIZE
        );

        for (int32_t y = 0; y < SubChunk::SIZE; ++y) {
            for (int32_t z = 0; z < SubChunk::SIZE; ++z) {
                for (int32_t x = 0; x < SubChunk::SIZE; ++x) {
                    BlockTypeId blockType = subChunk.getBlock(x, y, z);

                    if (blockType == AIR_BLOCK_TYPE) {
                        continue;
                    }

                    // Filter by transparency if provider is given
                    if (transparentProvider) {
                        bool isTransparent = (*transparentProvider)(blockType);
                        if (isTransparent != buildTransparent) {
                            continue;
                        }
                    }

                    // Check if this block has custom geometry
                    const BlockGeometry* customGeom = geometryFor(blockType);
                    if (!customGeom || customGeom->isEmpty()) {
                        continue;  // Standard cube, already handled by greedy mesh
                    }

                    // World position of this block
                    BlockCoord blockWorldPos(
                        subChunkWorldOrigin.x + x,
                        subChunkWorldOrigin.y + y,
                        subChunkWorldOrigin.z + z
                    );

                    // Local position within subchunk
                    glm::vec3 localPos(
                        static_cast<float>(x),
                        static_cast<float>(y),
                        static_cast<float>(z)
                    );

                    // Get base light level for the block
                    float baseSkyLight = 1.0f;
                    float baseBlockLight = 0.0f;
                    if ((smoothLighting_ || flatLighting_) && lightProvider_) {
                        uint8_t packed = lightProvider_(blockWorldPos);
                        baseSkyLight = static_cast<float>(packed >> 4) / 15.0f;
                        baseBlockLight = static_cast<float>(packed & 0x0F) / 15.0f;
                    }

                    const uint16_t joined = customGeom->hasConnections() ? connectionMask(blockWorldPos, blockType) : 0;

                    // Render each face in the custom geometry
                    for (const auto& faceGeom : customGeom->faces()) {
                        if (!faceGeom.isValid()) {
                            continue;
                        }
                        if (faceGeom.connection >= 0 &&
                            faceGeom.whenOpen == bool(joined & (1 << faceGeom.connection))) {
                            continue;
                        }

                        // For standard faces (0-5), check if neighbor occludes this face
                        if (faceGeom.isStandardFace() && !disableFaceCulling_) {
                            Face face = static_cast<Face>(faceGeom.faceIndex);
                            BlockCoord neighborPos = blockWorldPos.neighbor(face);
                            if (faceOccludesProvider_) {
                                Face oppFace = oppositeFace(face);
                                if (faceOccludesProvider_(neighborPos, oppFace)) {
                                    continue;
                                }
                            } else if (opaqueProvider(neighborPos)) {
                                continue;
                            }
                        }

                        // Get texture UVs
                        Face texFace = faceGeom.isStandardFace()
                            ? static_cast<Face>(faceGeom.faceIndex)
                            : Face::PosY;
                        glm::vec4 uvBounds = faceGeom.hasTextureUV ? faceGeom.textureUV : textureProvider(blockType, texFace);

                        // Calculate lighting for this face
                        float faceSkyLight = baseSkyLight;
                        float faceBlockLight = baseBlockLight;
                        customFaceLight(faceGeom, blockWorldPos, faceSkyLight, faceBlockLight);

                        addCustomFace(mesh, localPos, faceGeom, uvBounds, 1.0f, faceSkyLight, faceBlockLight,
                                      faceTint(blockType, faceGeom, blockWorldPos));
                    }
                }
            }
        }
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
    BlockCoord subChunkWorldOrigin(
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

                // Skip blocks with custom geometry (they can't be greedy-merged)
                if (geometryProvider_) {
                    const BlockGeometry* customGeom = geometryFor(blockType);
                    if (customGeom && !customGeom->isEmpty()) {
                        continue;  // Custom geometry handled in separate pass
                    }
                }

                // Filter by transparency if provider is given
                if (transparentProvider) {
                    bool isTransparent = (*transparentProvider)(blockType);
                    if (isTransparent != buildTransparent) {
                        continue;  // Skip blocks that don't match the pass type
                    }
                }

                // World position of this block
                BlockCoord blockWorldPos(
                    subChunkWorldOrigin.x + x,
                    subChunkWorldOrigin.y + y,
                    subChunkWorldOrigin.z + z
                );

                // Check if neighbor (in face direction) occludes this face
                BlockCoord neighborPos = blockWorldPos.neighbor(face);
                if (!disableFaceCulling_) {
                    if (faceOccludesProvider_) {
                        Face oppFace = oppositeFace(face);
                        if (faceOccludesProvider_(neighborPos, oppFace)) {
                            continue;  // Neighbor's face toward us is solid
                        }
                    } else if (opaqueProvider(neighborPos)) {
                        continue;  // Face is hidden
                    }
                }

                // This face is visible - add to mask
                int maskIdx = v * SIZE + u;
                mask[maskIdx].blockType = blockType;
                mask[maskIdx].uvBounds = textureProvider(blockType, face);
                mask[maskIdx].tint = cubeTint(blockType, face, blockWorldPos);
                if (!buildTransparent) {
                    mask[maskIdx].blend = faceBlend(blockType, blockWorldPos, face, opaqueProvider, textureProvider);
                }

                if (calculateAO_) {
                    mask[maskIdx].aoValues = getFaceAO(blockWorldPos, face, opaqueProvider);
                }

                if (smoothLighting_ && lightProvider_) {
                    // Smooth lighting: sample 9 points, average to 4 corners
                    auto lightResult = getFaceSkyBlockLight(blockWorldPos, face);
                    mask[maskIdx].skyLightValues = lightResult.sky;
                    mask[maskIdx].blockLightValues = lightResult.block;
                } else if (flatLighting_ && lightProvider_) {
                    // Flat lighting: sample 1 point, apply to all corners
                    BlockCoord faceAirPos = blockWorldPos.neighbor(face);
                    uint8_t packed = lightProvider_(faceAirPos);
                    float skyVal = static_cast<float>(packed >> 4) / 15.0f;
                    float blockVal = static_cast<float>(packed & 0x0F) / 15.0f;
                    mask[maskIdx].skyLightValues = {skyVal, skyVal, skyVal, skyVal};
                    mask[maskIdx].blockLightValues = {blockVal, blockVal, blockVal, blockVal};
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

namespace {
// Two triangles for a quad of vertices base..base+3, split along the diagonal through the darker
// pair of corners. Split the other way, one dark corner shades a single triangle and leaves a
// crease down the middle of the face; through it, the shade fans out evenly.
void pushQuadIndices(MeshData& mesh, uint32_t base, const std::array<float, 4>& ao,
                     const std::array<float, 4>& sky, const std::array<float, 4>& block) {
    auto bright = [&](int i) { return ao[i] * std::max(sky[i], block[i]) + ao[i]; };
    bool flip = bright(0) + bright(2) > bright(1) + bright(3) + 1e-4f;
    static constexpr uint32_t STRAIGHT[6] = {0, 1, 2, 0, 2, 3};
    static constexpr uint32_t FLIPPED[6] = {1, 2, 3, 1, 3, 0};
    for (uint32_t i : flip ? FLIPPED : STRAIGHT) mesh.indices.push_back(base + i);
}
}  // namespace

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

    // UV coordinates that tile across the merged region. The corners are in FACE_DATA's order,
    // so its offsets scaled up orient the tiles as a single block's face.
    glm::vec2 uvs[4];
    for (int i = 0; i < 4; ++i) {
        uvs[i] = glm::vec2(minU + faceData.uvOffsets[i].x * static_cast<float>(width) * tileWidth,
                           minV + faceData.uvOffsets[i].y * static_cast<float>(height) * tileHeight);
    }

    // For greedy quads, we use uniform AO and light across the quad
    // (averaging would be complex and greedy meshing typically assumes uniform lighting)
    // Use the entry's AO and light values directly
    const auto& aoValues = entry.aoValues;
    const auto& skyLightValues = entry.skyLightValues;
    const auto& blockLightValues = entry.blockLightValues;

    // Add vertices
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        vertex.position = corners[i];
        vertex.normal = normal;
        vertex.texCoord = uvs[i];
        vertex.tileBounds = tileBounds;  // Pass tile bounds for shader wrapping
        vertex.ao = aoValues[i];
        vertex.skyLight = skyLightValues[i];
        vertex.blockLight = blockLightValues[i];
        vertex.tint = entry.tint;
        vertex.blend = entry.blend;
        mesh.vertices.push_back(vertex);
    }

    pushQuadIndices(mesh, baseVertex, aoValues, skyLightValues, blockLightValues);
}

MeshData MeshBuilder::buildSubChunkMesh(
    const SubChunk& subChunk,
    ChunkPos chunkPos,
    const World& world,
    const BlockTextureProvider& textureProvider
) {
    BlockOpaqueProvider opaqueProvider = [&world](const BlockCoord& pos) { return fillsCell(world, pos); };

    return buildSubChunkMesh(subChunk, chunkPos, opaqueProvider, textureProvider);
}

void MeshBuilder::addFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    const glm::vec4& uvBounds,
    const std::array<float, 4>& aoValues,
    const std::array<float, 4>& skyLightValues,
    const std::array<float, 4>& blockLightValues,
    uint32_t tint,
    const glm::uvec4& blend
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
        vertex.skyLight = skyLightValues[i];
        vertex.blockLight = blockLightValues[i];
        vertex.tint = tint;
        vertex.blend = blend;
        mesh.vertices.push_back(vertex);
    }

    pushQuadIndices(mesh, baseVertex, aoValues, skyLightValues, blockLightValues);
}

namespace {

// The world steps a face's texture runs along: +u and +v, from where FACE_DATA puts its corners
struct FaceAxes {
    BlockCoord u, v;
};

const std::array<FaceAxes, 6>& faceAxes() {
    static const std::array<FaceAxes, 6> axes = [] {
        std::array<FaceAxes, 6> result{};
        for (int f = 0; f < 6; ++f) {
            const auto& data = MeshBuilder::FACE_DATA[f];
            glm::vec3 at[2][2];
            for (int i = 0; i < 4; ++i) {
                at[int(data.uvOffsets[i].x)][int(data.uvOffsets[i].y)] = data.positions[i];
            }
            auto step = [](glm::vec3 d) {
                return BlockCoord(int32_t(std::lround(d.x)), int32_t(std::lround(d.y)), int32_t(std::lround(d.z)));
            };
            result[f] = {step(at[1][0] - at[0][0]), step(at[0][1] - at[0][0])};
        }
        return result;
    }();
    return axes;
}

BlockCoord offset(const BlockCoord& pos, const FaceAxes& axes, int du, int dv) {
    return BlockCoord(pos.x + du * axes.u.x + dv * axes.v.x,
                      pos.y + du * axes.u.y + dv * axes.v.y,
                      pos.z + du * axes.u.z + dv * axes.v.z);
}

uint32_t packUV(float u, float v) {
    auto half = [](float x) { return static_cast<uint32_t>(std::clamp(x, 0.0f, 1.0f) * 65535.0f + 0.5f); };
    return half(u) | half(v) << 16;
}

uint32_t rgb565(uint32_t rgba) {
    return ((rgba & 0xFF) >> 3) | (((rgba >> 8) & 0xFF) >> 2) << 5 | (((rgba >> 16) & 0xFF) >> 3) << 11;
}

}  // namespace

bool MeshBuilder::faceHidden(const BlockCoord& pos, Face face, const BlockOpaqueProvider& opaqueProvider) const {
    BlockCoord front = pos.neighbor(face);
    if (faceOccludesProvider_) return faceOccludesProvider_(front, oppositeFace(face));
    return opaqueProvider(front);
}

// A neighbour spreads over this face if it blends at a higher priority, is a plain cube, and shows
// a face the same way in the same plane. Anything else, a step up or down included, leaves the
// edge as it is, so nothing is ever drawn over anything else. A diagonal reaching in only past a
// corner where both straight neighbours are covered over is left out: it would show grass in the
// inside corner of a wall that the grass is outside of.
glm::uvec4 MeshBuilder::faceBlend(BlockTypeId type, const BlockCoord& pos, Face face,
                                  const BlockOpaqueProvider& opaqueProvider,
                                  const BlockTextureProvider& textureProvider) const {
    if (!blendProvider_ || !blockAt_) return glm::uvec4(0u);
    const std::optional<BlockBlend> own = blendProvider_(type);
    if (!own) return glm::uvec4(0u);

    // Mask bit order, as BLEND_*: four edges, then four corners
    static constexpr int STEPS[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
    const FaceAxes& axes = faceAxes()[static_cast<int>(face)];

    struct Spill {
        BlockTypeId type;
        BlockBlend blend;
        uint32_t mask = 0;
    };
    std::array<Spill, 8> spills;
    int count = 0;
    for (int bit = 0; bit < 8; ++bit) {
        const int du = STEPS[bit][0], dv = STEPS[bit][1];
        const BlockCoord at = offset(pos, axes, du, dv);
        const BlockTypeId other = blockAt_(at);
        if (other == AIR_BLOCK_TYPE || other == type) continue;
        const std::optional<BlockBlend> blend = blendProvider_(other);
        if (!blend || blend->priority <= own->priority || blend->width <= 0.0f) continue;
        if (const BlockGeometry* geometry = geometryFor(other); geometry && !geometry->isEmpty()) continue;
        if (faceHidden(at, face, opaqueProvider)) continue;
        if (du != 0 && dv != 0 && faceHidden(offset(pos, axes, du, 0), face, opaqueProvider) &&
            faceHidden(offset(pos, axes, 0, dv), face, opaqueProvider)) {
            continue;
        }
        int i = 0;
        while (i < count && spills[i].type != other) ++i;
        if (i == count) spills[count++] = {other, *blend, 0};
        spills[i].mask |= 1u << bit;
    }
    if (count == 0) return glm::uvec4(0u);

    // A corner beside an edge of the same spread is nearer the edge everywhere, so adds nothing,
    // and leaving it out lets a row of faces along a border merge
    static constexpr uint32_t CORNER_EDGES[4] = {BLEND_LOW_U | BLEND_LOW_V, BLEND_HIGH_U | BLEND_LOW_V,
                                                 BLEND_LOW_U | BLEND_HIGH_V, BLEND_HIGH_U | BLEND_HIGH_V};
    for (int i = 0; i < count; ++i) {
        for (int c = 0; c < 4; ++c) {
            if (spills[i].mask & CORNER_EDGES[c]) spills[i].mask &= ~(1u << (4 + c));
        }
    }

    // The two of highest priority, the higher drawn last; the type breaks ties so a face always
    // comes out the same
    std::sort(spills.begin(), spills.begin() + count, [](const Spill& a, const Spill& b) {
        if (a.blend.priority != b.blend.priority) return a.blend.priority > b.blend.priority;
        return a.type.id < b.type.id;
    });
    glm::uvec4 packed(0u);
    for (int i = 0; i < std::min(count, 2); ++i) {
        const Spill& spill = spills[i];
        const int layer = 1 - i;
        const glm::vec4 uv = textureProvider(spill.type, face);
        const uint32_t width = static_cast<uint32_t>(std::clamp(spill.blend.width, 0.0f, 1.0f) * 255.0f + 0.5f);
        packed[layer] = packUV(uv.x, uv.y);
        packed.z |= (spill.mask << (8 * layer)) | (width << (16 + 8 * layer));
        packed.w |= rgb565(cubeTint(spill.type, face, pos)) << (16 * layer);
    }
    return packed;
}

// A diagonal only joins where neither straight neighbour beside it does: three blocks in an L
// make a corner, not a triangle.
const BlockGeometry* MeshBuilder::geometryFor(BlockTypeId type) const {
    if (!geometryProvider_) return nullptr;
    if (farGeometryProvider_) {
        if (const BlockGeometry* far = farGeometryProvider_(type)) {
            sawFarForms_ = true;
            if (!detailed_) return far;
        }
    }
    return geometryProvider_(type);
}

uint16_t MeshBuilder::connectionMask(const BlockCoord& pos, BlockTypeId type) const {
    if (!connectsProvider_) return 0;

    static constexpr int DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static constexpr int DZ[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    uint8_t found = 0;
    for (int i = 0; i < 8; ++i) {
        if (connectsProvider_(BlockCoord(pos.x + DX[i], pos.y, pos.z + DZ[i]), type)) {
            found |= static_cast<uint8_t>(1 << i);
        }
    }

    uint8_t joined = found & 0b01010101;
    for (int i = 1; i < 8; i += 2) {
        const int before = 1 << (i - 1);
        const int after = 1 << ((i + 1) % 8);
        if ((found & (1 << i)) && !(found & before) && !(found & after)) {
            joined |= static_cast<uint8_t>(1 << i);
        }
    }

    uint16_t mask = joined;
    if (connectsProvider_(BlockCoord(pos.x, pos.y + 1, pos.z), type)) mask |= 1 << 8;
    if (connectsProvider_(BlockCoord(pos.x, pos.y - 1, pos.z), type)) mask |= 1 << 9;
    return mask;
}

namespace {

/// The cube faces a custom face looks out through: one if it looks straight along an axis, and up
/// to three if it slopes, like a roof that looks both up and out
struct Facing {
    std::array<Face, 3> faces;
    size_t count = 0;
    void add(Face face) { faces[count++] = face; }
    const Face* begin() const { return faces.data(); }
    const Face* end() const { return faces.data() + count; }
};

Facing facingOf(const FaceGeometry& face) {
    Facing facing;
    const glm::vec3& v0 = face.vertices[0].position;
    glm::vec3 normal = glm::cross(face.vertices[2].position - v0, face.vertices[1].position - v0);
    float length = glm::length(normal);
    if (length < 1e-6f) return facing;
    normal /= length;

    // One at most for each axis, so three in all
    constexpr float LEANS = 0.3f;
    if (normal.x > LEANS) facing.add(Face::PosX);
    if (normal.x < -LEANS) facing.add(Face::NegX);
    if (normal.y > LEANS) facing.add(Face::PosY);
    if (normal.y < -LEANS) facing.add(Face::NegY);
    if (normal.z > LEANS) facing.add(Face::PosZ);
    if (normal.z < -LEANS) facing.add(Face::NegZ);
    return facing;
}

}  // namespace

void MeshBuilder::customFaceLight(const FaceGeometry& face, BlockCoord pos, float& sky, float& block) const {
    if (!lightProvider_) return;

    if (face.isStandardFace()) {
        uint8_t packed = lightProvider_(pos.neighbor(static_cast<Face>(face.faceIndex)));
        sky = static_cast<float>(packed >> 4) / 15.0f;
        block = static_cast<float>(packed & 0x0F) / 15.0f;
        return;
    }
    if (!smoothLighting_ && !flatLighting_) return;

    // A face inside the cell, like the top of a part-full stack, is lit by the cell it's in. But
    // that cell may read dark: the client's blocks lag the server's and its light doesn't, so a
    // block the server has made solid has already lost its light here while it still draws as
    // the partial one it was. What the face looks out at doesn't have that problem. It can't take
    // that alone, though, as a slab under a roof gets its light from the side. A sloping face
    // looks out at every cell it leans toward.
    for (Face facing : facingOf(face)) {
        uint8_t packed = lightProvider_(pos.neighbor(facing));
        sky = std::max(sky, static_cast<float>(packed >> 4) / 15.0f);
        block = std::max(block, static_cast<float>(packed & 0x0F) / 15.0f);
    }
}

void MeshBuilder::addCustomFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    const FaceGeometry& face,
    const glm::vec4& uvBounds,
    float ao,
    float sky,
    float block,
    uint32_t tint
) {
    if (face.vertices.size() < 3) {
        return;  // Invalid face
    }

    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

    // Compute face normal from first 3 vertices (cross product). Faces wind like the cube
    // in FACE_DATA, the winding that survives back-face culling: edge2 x edge1 points out.
    const glm::vec3& v0 = face.vertices[0].position;
    const glm::vec3& v1 = face.vertices[1].position;
    const glm::vec3& v2 = face.vertices[2].position;
    glm::vec3 edge1 = v1 - v0;
    glm::vec3 edge2 = v2 - v0;
    glm::vec3 normal = glm::normalize(glm::cross(edge2, edge1));

    // Extract UV bounds for atlas wrapping
    float minU = uvBounds.x;
    float minV = uvBounds.y;
    float maxU = uvBounds.z;
    float maxV = uvBounds.w;
    float tileWidth = maxU - minU;
    float tileHeight = maxV - minV;

    // Add vertices
    for (const auto& modelVert : face.vertices) {
        ChunkVertex vertex;
        vertex.position = blockPos + modelVert.position;
        vertex.normal = normal;

        // Map model UVs (0-1) to atlas tile coordinates
        vertex.texCoord = glm::vec2(
            minU + modelVert.uv.x * tileWidth,
            minV + modelVert.uv.y * tileHeight
        );
        vertex.tileBounds = uvBounds;
        vertex.ao = ao;
        vertex.skyLight = sky;
        vertex.blockLight = block;
        vertex.tint = tint;
        mesh.vertices.push_back(vertex);
    }

    // Triangulate the polygon (fan triangulation for convex polygons)
    // For N vertices: N-2 triangles
    size_t vertexCount = face.vertices.size();
    for (size_t i = 1; i + 1 < vertexCount; ++i) {
        mesh.indices.push_back(baseVertex + 0);
        mesh.indices.push_back(baseVertex + static_cast<uint32_t>(i));
        mesh.indices.push_back(baseVertex + static_cast<uint32_t>(i + 1));
    }
}

float MeshBuilder::calculateCornerAO(bool side1, bool side2, bool corner) const {
    // Voxel ambient occlusion
    // Count solid neighbors and map to AO value
    // Note: We don't use the "0fps" special case of returning 0.0 when both
    // sides are solid - that's too aggressive and makes 1x1 holes pitch black.
    // Instead, we let the solid count determine the AO (minimum 0.25).

    int solidCount = (side1 ? 1 : 0) + (side2 ? 1 : 0) + (corner ? 1 : 0);

    // Map solid count to AO value (0 solid = 1.0, 3 solid = 0.25)
    switch (solidCount) {
        case 0: return 1.0f;     // Fully lit
        case 1: return 0.75f;    // Light shadow
        case 2: return 0.5f;     // Medium shadow
        case 3: return 0.25f;    // Heavy shadow (minimum - never pitch black)
        default: return 1.0f;
    }
}

// The two directions across a face, and for each of its corners (in FACE_DATA order) which way
// that corner lies from the face's middle along each: -1 or +1. Taken from FACE_DATA itself, so
// AO and light always land on the vertex they were worked out for.
namespace {
struct FaceCorners {
    glm::ivec3 across;   // first tangent
    glm::ivec3 up;       // second tangent
    std::array<glm::ivec2, 4> side;
};

FaceCorners faceCorners(Face face, const std::array<MeshBuilder::FaceData, 6>& data) {
    FaceCorners c;
    switch (face) {
        case Face::PosX:
        case Face::NegX:
            c.across = {0, 0, 1};
            c.up = {0, 1, 0};
            break;
        case Face::PosY:
        case Face::NegY:
            c.across = {1, 0, 0};
            c.up = {0, 0, 1};
            break;
        default:
            c.across = {1, 0, 0};
            c.up = {0, 1, 0};
            break;
    }
    const auto& positions = data[static_cast<int>(face)].positions;
    for (int i = 0; i < 4; ++i) {
        glm::vec3 offset = positions[i] - glm::vec3(0.5f);
        float a = glm::dot(offset, glm::vec3(c.across));
        float b = glm::dot(offset, glm::vec3(c.up));
        c.side[i] = glm::ivec2(a > 0.0f ? 1 : -1, b > 0.0f ? 1 : -1);
    }
    return c;
}
}  // namespace

std::array<float, 4> MeshBuilder::getFaceAO(
    const BlockCoord& blockWorldPos,
    Face face,
    const BlockOpaqueProvider& opaqueProvider
) const {
    // Each corner is darkened by the blocks touching it in the layer the face looks into: the
    // two beside it along the face's edges, and the one diagonally out from the corner
    FaceCorners c = faceCorners(face, FACE_DATA);
    BlockCoord facePos = blockWorldPos.neighbor(face);

    auto isOpaqueAt = [&](int da, int db) -> bool {
        BlockCoord checkPos(
            facePos.x + c.across.x * da + c.up.x * db,
            facePos.y + c.across.y * da + c.up.y * db,
            facePos.z + c.across.z * da + c.up.z * db
        );
        return opaqueProvider(checkPos);
    };

    std::array<float, 4> aoValues;
    for (int i = 0; i < 4; ++i) {
        int a = c.side[i].x;
        int b = c.side[i].y;
        aoValues[i] = calculateCornerAO(isOpaqueAt(a, 0), isOpaqueAt(0, b), isOpaqueAt(a, b));
    }
    return aoValues;
}

MeshBuilder::FaceLightResult MeshBuilder::getFaceSkyBlockLight(
    const BlockCoord& blockWorldPos,
    Face face
) const {
    FaceLightResult result;

    if (!lightProvider_) {
        return result;
    }

    // Each corner averages the light of the four cells around it in the layer the face looks
    // into: the face's own cell, the two beside the corner, and the one diagonally out
    FaceCorners c = faceCorners(face, FACE_DATA);
    BlockCoord facePos = blockWorldPos.neighbor(face);

    auto lightAt = [&](int da, int db) -> uint8_t {
        BlockCoord checkPos(
            facePos.x + c.across.x * da + c.up.x * db,
            facePos.y + c.across.y * da + c.up.y * db,
            facePos.z + c.across.z * da + c.up.z * db
        );
        return lightProvider_(checkPos);
    };

    uint8_t middle = lightAt(0, 0);
    for (int i = 0; i < 4; ++i) {
        int a = c.side[i].x;
        int b = c.side[i].y;
        uint8_t samples[4] = {middle, lightAt(a, 0), lightAt(0, b), lightAt(a, b)};
        float sky = 0.0f;
        float block = 0.0f;
        for (uint8_t packed : samples) {
            sky += static_cast<float>(packed >> 4) / 15.0f;
            block += static_cast<float>(packed & 0x0F) / 15.0f;
        }
        result.sky[i] = sky * 0.25f;
        result.block[i] = block * 0.25f;
    }

    return result;
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
    const std::array<float, 4>& aoValues,
    float skyLight,
    float blockLightVal
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
        vertex.skyLight = skyLight;  // Uniform sky light across LOD face
        vertex.blockLight = blockLightVal;  // Uniform block light across LOD face
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

void MeshBuilder::addHeightLimitedFace(
    MeshData& mesh,
    const glm::vec3& blockPos,
    Face face,
    float blockScale,
    float height,
    const glm::vec4& uvBounds,
    const std::array<float, 4>& aoValues,
    float skyLight,
    float blockLightVal
) {
    // For height-limited blocks:
    // - Top face (PosY): at height instead of blockScale
    // - Bottom face (NegY): at 0 (same as full block)
    // - Side faces (X, Z): truncated to height instead of blockScale

    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());

    // Extract UV bounds
    float minU = uvBounds.x;
    float minV = uvBounds.y;
    float maxU = uvBounds.z;
    float maxV = uvBounds.w;
    float tileWidth = maxU - minU;
    float tileHeight = maxV - minV;

    // Determine scaling for each vertex based on face type
    auto scalePosition = [&](const glm::vec3& facePos) -> glm::vec3 {
        glm::vec3 scaled;
        // X and Z scale by blockScale
        scaled.x = facePos.x * blockScale;
        scaled.z = facePos.z * blockScale;
        // Y scales by height (facePos.y is 0 or 1)
        scaled.y = facePos.y * height;
        return scaled;
    };

    // For UV tiling, we need to adjust based on the actual dimensions
    auto getUVScale = [&](const glm::vec2& uvOffset) -> glm::vec2 {
        float uvScaleX = blockScale;
        float uvScaleY;

        // Determine which dimension uses height vs blockScale for UV
        switch (face) {
            case Face::PosY:
            case Face::NegY:
                // Top/bottom faces: both dimensions use blockScale
                uvScaleY = blockScale;
                break;
            default:
                // Side faces: V dimension (usually Y) uses height
                uvScaleY = height;
                break;
        }

        return glm::vec2(
            minU + uvOffset.x * uvScaleX * tileWidth,
            minV + uvOffset.y * uvScaleY * tileHeight
        );
    };

    // Add 4 vertices for the face
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        vertex.position = blockPos + scalePosition(faceData.positions[i]);
        vertex.normal = faceData.normal;
        vertex.texCoord = getUVScale(faceData.uvOffsets[i]);
        vertex.tileBounds = uvBounds;
        vertex.ao = aoValues[i];
        vertex.skyLight = skyLight;  // Uniform sky light across LOD face
        vertex.blockLight = blockLightVal;  // Uniform block light across LOD face
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
    BlockOpaqueProvider alwaysTransparent = [](const BlockCoord&) { return false; };
    return buildLODMesh(lodSubChunk, chunkPos, alwaysTransparent, textureProvider, LODMergeMode::FullHeight);
}

MeshData MeshBuilder::buildLODMesh(
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& neighborProvider,
    const BlockTextureProvider& textureProvider
) {
    // Delegate to the merge mode version with FullHeight
    return buildLODMesh(lodSubChunk, chunkPos, neighborProvider, textureProvider, LODMergeMode::FullHeight);
}

MeshData MeshBuilder::buildLODMesh(
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& neighborProvider,
    const BlockTextureProvider& textureProvider,
    LODMergeMode mergeMode
) {
    MeshData mesh;

    if (lodSubChunk.isEmpty()) {
        return mesh;
    }

    // Use greedy meshing if enabled (same setting as regular meshing)
    if (greedyMeshing_) {
        buildGreedyLODMesh(mesh, lodSubChunk, chunkPos, neighborProvider, textureProvider, mergeMode);
    } else {
        // Fallback to simple per-cell meshing
        int resolution = lodSubChunk.resolution();
        int grouping = lodSubChunk.grouping();
        float blockScale = static_cast<float>(grouping);

        // Reserve space based on expected geometry
        mesh.reserve(resolution * resolution * resolution * 6 * 4,
                     resolution * resolution * resolution * 6 * 6);

        // Default AO values (no AO calculation for LOD)
        std::array<float, 4> defaultAO = {1.0f, 1.0f, 1.0f, 1.0f};

        // Iterate over all LOD cells
        for (int ly = 0; ly < resolution; ++ly) {
            for (int lz = 0; lz < resolution; ++lz) {
                for (int lx = 0; lx < resolution; ++lx) {
                    LODBlockInfo blockInfo = lodSubChunk.getBlockInfo(lx, ly, lz);
                    if (blockInfo.type == AIR_BLOCK_TYPE) {
                        continue;
                    }

                    // Get actual height for this LOD block
                    float height = static_cast<float>(blockInfo.height);
                    if (height <= 0) {
                        height = blockScale;  // Fallback to full height
                    }

                    // Calculate world block position for the LOD cell corner
                    float worldX = static_cast<float>(chunkPos.x * 16 + lx * grouping);
                    float worldY = static_cast<float>(chunkPos.y * 16 + ly * grouping);
                    float worldZ = static_cast<float>(chunkPos.z * 16 + lz * grouping);

                    // Local position within subchunk
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

                        // For height-limited mode, top faces are never culled by internal neighbors
                        // (since we can't know if the neighbor's height matches ours)
                        bool skipInternalCull = (mergeMode == LODMergeMode::HeightLimited &&
                                                 face == Face::PosY);

                        if (!skipInternalCull) {
                            // Check if neighbor is within this LOD subchunk
                            if (nx >= 0 && nx < resolution &&
                                ny >= 0 && ny < resolution &&
                                nz >= 0 && nz < resolution) {
                                // Internal neighbor
                                // For side faces in height-limited mode, only cull if neighbor is fully solid
                                if (mergeMode == LODMergeMode::HeightLimited &&
                                    (face == Face::NegX || face == Face::PosX ||
                                     face == Face::NegZ || face == Face::PosZ)) {
                                    // Check if neighbor height would cover this face
                                    LODBlockInfo neighborInfo = lodSubChunk.getBlockInfo(nx, ny, nz);
                                    float neighborHeight = static_cast<float>(neighborInfo.height);
                                    // Only cull if neighbor is at least as tall as us
                                    neighborOpaque = (neighborInfo.type != AIR_BLOCK_TYPE &&
                                                      neighborHeight >= height);
                                } else {
                                    neighborOpaque = (lodSubChunk.getBlock(nx, ny, nz) != AIR_BLOCK_TYPE);
                                }
                            } else {
                                // External neighbor - use the provided neighbor provider
                                BlockCoord neighborWorldPos;
                                auto normal = faceNormal(face);
                                neighborWorldPos.x = static_cast<int32_t>(worldX) + normal[0] * grouping;
                                neighborWorldPos.y = static_cast<int32_t>(worldY) + normal[1] * grouping;
                                neighborWorldPos.z = static_cast<int32_t>(worldZ) + normal[2] * grouping;
                                neighborOpaque = neighborProvider(neighborWorldPos);
                            }
                        }

                        if (neighborOpaque && !disableFaceCulling_) {
                            continue;  // Face is hidden
                        }

                        // Get texture UVs for this face
                        glm::vec4 uvBounds = textureProvider(blockInfo.type, face);

                        // Sample light for this LOD cell if lighting is enabled
                        // Sample at 4 corners of the face and take max to avoid missing nearby lights
                        float faceSkyLight = 1.0f;
                        float faceBlockLight = 0.0f;
                        if ((smoothLighting_ || flatLighting_) && lightProvider_) {
                            auto normal = faceNormal(face);
                            uint8_t maxSky = 0;
                            uint8_t maxBlock = 0;

                            // Face position in the normal direction (just outside the LOD cell)
                            int32_t normalOffset = (normal[0] + normal[1] + normal[2]) > 0 ? grouping : -1;

                            // Sample at 4 corners of the face (iterate non-normal axes at 0 and grouping-1)
                            for (int corner = 0; corner < 4; ++corner) {
                                int32_t off1 = (corner & 1) ? grouping - 1 : 0;
                                int32_t off2 = (corner & 2) ? grouping - 1 : 0;

                                BlockCoord samplePos;
                                if (normal[0] != 0) {
                                    // X is the face axis
                                    samplePos = BlockCoord{
                                        static_cast<int32_t>(worldX) + normalOffset,
                                        static_cast<int32_t>(worldY) + off1,
                                        static_cast<int32_t>(worldZ) + off2
                                    };
                                } else if (normal[1] != 0) {
                                    // Y is the face axis
                                    samplePos = BlockCoord{
                                        static_cast<int32_t>(worldX) + off1,
                                        static_cast<int32_t>(worldY) + normalOffset,
                                        static_cast<int32_t>(worldZ) + off2
                                    };
                                } else {
                                    // Z is the face axis
                                    samplePos = BlockCoord{
                                        static_cast<int32_t>(worldX) + off1,
                                        static_cast<int32_t>(worldY) + off2,
                                        static_cast<int32_t>(worldZ) + normalOffset
                                    };
                                }

                                uint8_t packed = lightProvider_(samplePos);
                                maxSky = std::max(maxSky, static_cast<uint8_t>(packed >> 4));
                                maxBlock = std::max(maxBlock, static_cast<uint8_t>(packed & 0x0F));
                            }
                            faceSkyLight = static_cast<float>(maxSky) / 15.0f;
                            faceBlockLight = static_cast<float>(maxBlock) / 15.0f;
                        }

                        // Add the face with height limitation
                        if (mergeMode == LODMergeMode::HeightLimited && height < blockScale) {
                            addHeightLimitedFace(mesh, localPos, face, blockScale, height, uvBounds, defaultAO, faceSkyLight, faceBlockLight);
                        } else {
                            addScaledFace(mesh, localPos, face, blockScale, uvBounds, defaultAO, faceSkyLight, faceBlockLight);
                        }
                    }
                }
            }
        }
    }

    return mesh;
}

// ============================================================================
// LOD Greedy Meshing
// ============================================================================

void MeshBuilder::buildGreedyLODMesh(
    MeshData& mesh,
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& neighborProvider,
    const BlockTextureProvider& textureProvider,
    LODMergeMode mergeMode
) {
    int resolution = lodSubChunk.resolution();

    // Reserve space - with greedy meshing, we'll use much less
    mesh.reserve(resolution * resolution * 6 * 4, resolution * resolution * 6 * 6);

    // Process each face direction separately
    for (int faceIdx = 0; faceIdx < 6; ++faceIdx) {
        Face face = static_cast<Face>(faceIdx);
        greedyMeshLODFace(mesh, face, lodSubChunk, chunkPos, neighborProvider, textureProvider, mergeMode);
    }
}

void MeshBuilder::greedyMeshLODFace(
    MeshData& mesh,
    Face face,
    const LODSubChunk& lodSubChunk,
    ChunkPos chunkPos,
    const BlockOpaqueProvider& neighborProvider,
    const BlockTextureProvider& textureProvider,
    LODMergeMode mergeMode
) {
    int resolution = lodSubChunk.resolution();
    int grouping = lodSubChunk.grouping();
    float blockScale = static_cast<float>(grouping);

    // Determine which axis is normal to this face, and which are tangent
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

    // 2D mask array for this face direction
    std::vector<LODFaceMaskEntry> mask(resolution * resolution);

    // Process each slice perpendicular to the normal axis
    for (int slice = 0; slice < resolution; ++slice) {
        // Clear the mask
        for (auto& entry : mask) {
            entry = LODFaceMaskEntry{};
        }

        // Build the mask for this slice
        for (int v = 0; v < resolution; ++v) {
            for (int u = 0; u < resolution; ++u) {
                // Convert (slice, u, v) to (lx, ly, lz) based on face direction
                int coords[3];
                coords[normalAxis] = slice;
                coords[uAxis] = u;
                coords[vAxis] = v;

                int lx = coords[0];
                int ly = coords[1];
                int lz = coords[2];

                LODBlockInfo blockInfo = lodSubChunk.getBlockInfo(lx, ly, lz);
                if (blockInfo.type == AIR_BLOCK_TYPE) {
                    continue;
                }

                // Get actual height for this LOD block
                float height = static_cast<float>(blockInfo.height);
                if (height <= 0) {
                    height = blockScale;
                }

                // Calculate world position for neighbor checking
                float worldX = static_cast<float>(chunkPos.x * 16 + lx * grouping);
                float worldY = static_cast<float>(chunkPos.y * 16 + ly * grouping);
                float worldZ = static_cast<float>(chunkPos.z * 16 + lz * grouping);

                // Check neighbor occlusion
                int nCoords[3];
                nCoords[normalAxis] = slice + (face == Face::PosX || face == Face::PosY || face == Face::PosZ ? 1 : -1);
                nCoords[uAxis] = u;
                nCoords[vAxis] = v;

                int nx = nCoords[0];
                int ny = nCoords[1];
                int nz = nCoords[2];

                bool neighborOpaque = false;

                // For height-limited mode, top faces are never culled by internal neighbors
                bool skipInternalCull = (mergeMode == LODMergeMode::HeightLimited && face == Face::PosY);

                if (!skipInternalCull) {
                    if (nx >= 0 && nx < resolution &&
                        ny >= 0 && ny < resolution &&
                        nz >= 0 && nz < resolution) {
                        // Internal neighbor
                        if (mergeMode == LODMergeMode::HeightLimited &&
                            (face == Face::NegX || face == Face::PosX ||
                             face == Face::NegZ || face == Face::PosZ)) {
                            LODBlockInfo neighborInfo = lodSubChunk.getBlockInfo(nx, ny, nz);
                            float neighborHeight = static_cast<float>(neighborInfo.height);
                            neighborOpaque = (neighborInfo.type != AIR_BLOCK_TYPE && neighborHeight >= height);
                        } else {
                            neighborOpaque = (lodSubChunk.getBlock(nx, ny, nz) != AIR_BLOCK_TYPE);
                        }
                    } else {
                        // External neighbor
                        BlockCoord neighborWorldPos;
                        auto normal = faceNormal(face);
                        neighborWorldPos.x = static_cast<int32_t>(worldX) + normal[0] * grouping;
                        neighborWorldPos.y = static_cast<int32_t>(worldY) + normal[1] * grouping;
                        neighborWorldPos.z = static_cast<int32_t>(worldZ) + normal[2] * grouping;
                        neighborOpaque = neighborProvider(neighborWorldPos);
                    }
                }

                if (neighborOpaque && !disableFaceCulling_) {
                    continue;  // Face is hidden
                }

                // This face is visible - add to mask
                int maskIdx = v * resolution + u;
                mask[maskIdx].blockType = blockInfo.type;
                mask[maskIdx].uvBounds = textureProvider(blockInfo.type, face);

                // For height-limited mode, store the height (but only matters for top/bottom/side faces)
                if (mergeMode == LODMergeMode::HeightLimited) {
                    mask[maskIdx].height = height;
                } else {
                    mask[maskIdx].height = blockScale;  // FullHeight mode
                }

                // Sample light for this LOD cell if lighting is enabled
                // Sample at 4 corners of the face and take max to avoid missing nearby lights
                if ((smoothLighting_ || flatLighting_) && lightProvider_) {
                    auto normal = faceNormal(face);
                    uint8_t maxSky = 0;
                    uint8_t maxBlock = 0;

                    // Face position in the normal direction (just outside the LOD cell)
                    int32_t normalOffset = (normal[0] + normal[1] + normal[2]) > 0 ? grouping : -1;

                    // Sample at 4 corners of the face (iterate non-normal axes at 0 and grouping-1)
                    for (int corner = 0; corner < 4; ++corner) {
                        int32_t off1 = (corner & 1) ? grouping - 1 : 0;
                        int32_t off2 = (corner & 2) ? grouping - 1 : 0;

                        BlockCoord samplePos;
                        if (normal[0] != 0) {
                            samplePos = BlockCoord{
                                static_cast<int32_t>(worldX) + normalOffset,
                                static_cast<int32_t>(worldY) + off1,
                                static_cast<int32_t>(worldZ) + off2
                            };
                        } else if (normal[1] != 0) {
                            samplePos = BlockCoord{
                                static_cast<int32_t>(worldX) + off1,
                                static_cast<int32_t>(worldY) + normalOffset,
                                static_cast<int32_t>(worldZ) + off2
                            };
                        } else {
                            samplePos = BlockCoord{
                                static_cast<int32_t>(worldX) + off1,
                                static_cast<int32_t>(worldY) + off2,
                                static_cast<int32_t>(worldZ) + normalOffset
                            };
                        }

                        uint8_t packed = lightProvider_(samplePos);
                        maxSky = std::max(maxSky, static_cast<uint8_t>(packed >> 4));
                        maxBlock = std::max(maxBlock, static_cast<uint8_t>(packed & 0x0F));
                    }
                    mask[maskIdx].skyLight = static_cast<float>(maxSky) / 15.0f;
                    mask[maskIdx].blockLightVal = static_cast<float>(maxBlock) / 15.0f;
                }
            }
        }

        // Greedy merge the mask into quads
        std::vector<bool> used(resolution * resolution, false);

        for (int v = 0; v < resolution; ++v) {
            for (int u = 0; u < resolution; ++u) {
                int startIdx = v * resolution + u;

                // Skip if already used or empty
                if (used[startIdx] || mask[startIdx].isEmpty()) {
                    continue;
                }

                const LODFaceMaskEntry& entry = mask[startIdx];

                // Find width: extend in U direction while faces match
                int width = 1;
                while (u + width < resolution) {
                    int checkIdx = v * resolution + (u + width);
                    if (used[checkIdx] || mask[checkIdx] != entry) {
                        break;
                    }
                    ++width;
                }

                // Find height: extend in V direction while entire row matches
                int height = 1;
                bool canExtend = true;
                while (canExtend && v + height < resolution) {
                    for (int du = 0; du < width; ++du) {
                        int checkIdx = (v + height) * resolution + (u + du);
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
                        used[(v + dv) * resolution + (u + du)] = true;
                    }
                }

                // Add the merged quad
                addGreedyLODQuad(mesh, face, slice, u, v, width, height,
                                 blockScale, entry.height, entry, textureProvider);
            }
        }
    }
}

void MeshBuilder::addGreedyLODQuad(
    MeshData& mesh,
    Face face,
    int sliceCoord,
    int startU, int startV,
    int width, int height,
    float blockScale,
    float blockHeight,
    const LODFaceMaskEntry& entry,
    [[maybe_unused]] const BlockTextureProvider& textureProvider
) {
    // Determine axis mapping (same as in greedyMeshLODFace)
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
    // Each LOD cell is blockScale units in size
    // The face is at sliceCoord along the normal axis
    float normalCoord;
    if (positiveNormal) {
        normalCoord = static_cast<float>(sliceCoord + 1) * blockScale;
    } else {
        normalCoord = static_cast<float>(sliceCoord) * blockScale;
    }

    // For height-limited top faces, adjust the normal coordinate
    bool isHeightLimited = (blockHeight < blockScale);
    if (isHeightLimited && face == Face::PosY) {
        // Top face at blockHeight instead of blockScale
        normalCoord = static_cast<float>(sliceCoord) * blockScale + blockHeight;
    }

    // Build corner positions
    glm::vec3 corners[4];
    float coords[3];

    // Quad dimensions in world units
    float quadWidth = static_cast<float>(width) * blockScale;
    float quadHeight = static_cast<float>(height) * blockScale;

    // For side faces in height-limited mode, adjust the V dimension
    if (isHeightLimited && (face == Face::NegX || face == Face::PosX ||
                            face == Face::NegZ || face == Face::PosZ)) {
        // Side faces are truncated by blockHeight
        quadHeight = static_cast<float>(height) * blockHeight;
    }

    // Calculate start positions in world units
    float uStart = static_cast<float>(startU) * blockScale;
    float vStart = static_cast<float>(startV) * blockScale;

    // Adjust corner order based on face direction to maintain correct winding
    int uCoords[4], vCoords[4];

    switch (face) {
        case Face::NegX:
            uCoords[0] = 1; uCoords[1] = 0; uCoords[2] = 0; uCoords[3] = 1;  // width
            vCoords[0] = 0; vCoords[1] = 0; vCoords[2] = 1; vCoords[3] = 1;  // height
            break;
        case Face::PosX:
            uCoords[0] = 0; uCoords[1] = 1; uCoords[2] = 1; uCoords[3] = 0;
            vCoords[0] = 0; vCoords[1] = 0; vCoords[2] = 1; vCoords[3] = 1;
            break;
        case Face::NegY:
            uCoords[0] = 0; uCoords[1] = 1; uCoords[2] = 1; uCoords[3] = 0;
            vCoords[0] = 1; vCoords[1] = 1; vCoords[2] = 0; vCoords[3] = 0;
            break;
        case Face::PosY:
            uCoords[0] = 0; uCoords[1] = 1; uCoords[2] = 1; uCoords[3] = 0;
            vCoords[0] = 0; vCoords[1] = 0; vCoords[2] = 1; vCoords[3] = 1;
            break;
        case Face::NegZ:
            uCoords[0] = 0; uCoords[1] = 1; uCoords[2] = 1; uCoords[3] = 0;
            vCoords[0] = 0; vCoords[1] = 0; vCoords[2] = 1; vCoords[3] = 1;
            break;
        case Face::PosZ:
            uCoords[0] = 1; uCoords[1] = 0; uCoords[2] = 0; uCoords[3] = 1;
            vCoords[0] = 0; vCoords[1] = 0; vCoords[2] = 1; vCoords[3] = 1;
            break;
        default:
            return;
    }

    for (int i = 0; i < 4; ++i) {
        coords[normalAxis] = normalCoord;
        coords[uAxis] = uStart + static_cast<float>(uCoords[i]) * quadWidth;
        coords[vAxis] = vStart + static_cast<float>(vCoords[i]) * quadHeight;
        corners[i] = glm::vec3(coords[0], coords[1], coords[2]);
    }

    // Get face normal
    const FaceData& faceData = FACE_DATA[static_cast<int>(face)];
    glm::vec3 normal = faceData.normal;

    // Calculate UV coordinates for the merged quad with proper tiling
    glm::vec4 tileBounds = entry.uvBounds;
    float minU = tileBounds.x;
    float minV = tileBounds.y;
    float maxU = tileBounds.z;
    float maxV = tileBounds.w;
    float tileWidth = maxU - minU;
    float tileHeight = maxV - minV;

    // UV tiling: each LOD cell tiles the texture once (scaled by blockScale)
    // So a merged region of width*height LOD cells tiles width*height times
    float uvWidth = static_cast<float>(width) * blockScale;
    float uvHeight = static_cast<float>(height) * blockScale;

    // For height-limited side faces, UV height is reduced
    if (isHeightLimited && (face == Face::NegX || face == Face::PosX ||
                            face == Face::NegZ || face == Face::PosZ)) {
        uvHeight = static_cast<float>(height) * blockHeight;
    }

    glm::vec2 uvs[4];
    for (int i = 0; i < 4; ++i) {
        uvs[i] = glm::vec2(minU + faceData.uvOffsets[i].x * uvWidth * tileWidth,
                           minV + faceData.uvOffsets[i].y * uvHeight * tileHeight);
    }

    // Add vertices
    uint32_t baseVertex = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i) {
        ChunkVertex vertex;
        vertex.position = corners[i];
        vertex.normal = normal;
        vertex.texCoord = uvs[i];
        vertex.tileBounds = tileBounds;
        vertex.ao = 1.0f;      // No AO for LOD meshes
        vertex.skyLight = entry.skyLight;  // Use sampled sky light from mask entry
        vertex.blockLight = entry.blockLightVal;  // Use sampled block light from mask entry
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

}  // namespace finevox
