#pragma once

#include "finevox/position.hpp"
#include "finevox/string_interner.hpp"  // For BlockTypeId
#include "finevox/physics.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include <array>

namespace finevox {

// Forward declarations
class SubChunk;
class World;

// ============================================================================
// ChunkVertex - Vertex format for chunk meshes
// ============================================================================

struct ChunkVertex {
    glm::vec3 position;   // Local position within subchunk (0-16 on each axis)
    glm::vec3 normal;     // Face normal
    glm::vec2 texCoord;   // Texture coordinates
    float ao;             // Ambient occlusion (0-1, 1 = fully lit)

    ChunkVertex() = default;
    ChunkVertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& tex, float ambientOcclusion)
        : position(pos), normal(norm), texCoord(tex), ao(ambientOcclusion) {}

    bool operator==(const ChunkVertex& other) const {
        return position == other.position &&
               normal == other.normal &&
               texCoord == other.texCoord &&
               ao == other.ao;
    }
};

// ============================================================================
// MeshData - CPU-side mesh data ready for GPU upload
// ============================================================================

struct MeshData {
    std::vector<ChunkVertex> vertices;
    std::vector<uint32_t> indices;

    // Check if mesh has any geometry
    [[nodiscard]] bool isEmpty() const { return vertices.empty(); }

    // Clear all data
    void clear() {
        vertices.clear();
        indices.clear();
    }

    // Reserve space for expected geometry
    void reserve(size_t vertexCount, size_t indexCount) {
        vertices.reserve(vertexCount);
        indices.reserve(indexCount);
    }

    // Statistics
    [[nodiscard]] size_t vertexCount() const { return vertices.size(); }
    [[nodiscard]] size_t indexCount() const { return indices.size(); }
    [[nodiscard]] size_t triangleCount() const { return indices.size() / 3; }

    // Memory usage in bytes
    [[nodiscard]] size_t memoryUsage() const {
        return vertices.size() * sizeof(ChunkVertex) +
               indices.size() * sizeof(uint32_t);
    }
};

// ============================================================================
// BlockFaceInfo - Information needed to generate a face
// ============================================================================

struct BlockFaceInfo {
    BlockTypeId blockType;      // Type of block this face belongs to
    Face face;                  // Which face of the block
    glm::vec2 uvMin{0.0f};      // Texture UV minimum
    glm::vec2 uvMax{1.0f};      // Texture UV maximum
};

// ============================================================================
// BlockInfoProvider - Callback for getting block rendering info
// ============================================================================

// Callback to check if a block is solid/opaque (for face culling)
// Returns true if the block at the given position is opaque (hides faces behind it)
using BlockOpaqueProvider = std::function<bool(const BlockPos& pos)>;

// Callback to get texture UVs for a block face
// Returns UV coordinates (minU, minV, maxU, maxV) for the given block type and face
using BlockTextureProvider = std::function<glm::vec4(BlockTypeId type, Face face)>;

// ============================================================================
// MeshBuilder - Generates mesh data from subchunk blocks
// ============================================================================

class MeshBuilder {
public:
    MeshBuilder();

    // Build mesh for a subchunk using simple face culling
    // opaqueProvider: checks if neighboring blocks are opaque (for culling hidden faces)
    // textureProvider: gets UV coordinates for each block face
    // Returns mesh data for the opaque pass
    [[nodiscard]] MeshData buildSubChunkMesh(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider
    );

    // Build mesh using World for neighbor lookups
    // This is a convenience method that creates providers from World access
    [[nodiscard]] MeshData buildSubChunkMesh(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const World& world,
        const BlockTextureProvider& textureProvider
    );

    // Configuration
    void setCalculateAO(bool enabled) { calculateAO_ = enabled; }
    [[nodiscard]] bool calculateAO() const { return calculateAO_; }

    // Enable/disable greedy meshing (merges coplanar faces)
    void setGreedyMeshing(bool enabled) { greedyMeshing_ = enabled; }
    [[nodiscard]] bool greedyMeshing() const { return greedyMeshing_; }

    // DEBUG: Disable hidden face removal (renders all faces)
    void setDisableFaceCulling(bool disabled) { disableFaceCulling_ = disabled; }
    [[nodiscard]] bool disableFaceCulling() const { return disableFaceCulling_; }

private:
    bool calculateAO_ = true;
    bool greedyMeshing_ = true;  // Enabled by default
    bool disableFaceCulling_ = false;

    // Add a single face to the mesh data
    void addFace(
        MeshData& mesh,
        const glm::vec3& blockPos,   // Local block position within subchunk
        Face face,
        const glm::vec4& uvBounds,   // (minU, minV, maxU, maxV)
        const std::array<float, 4>& aoValues  // AO for each corner (CCW from bottom-left)
    );

    // Calculate ambient occlusion for a face corner
    // side1, side2: whether adjacent blocks in each direction are solid
    // corner: whether the diagonal corner block is solid
    [[nodiscard]] float calculateCornerAO(bool side1, bool side2, bool corner) const;

    // Get the 4 AO values for a face (CCW from bottom-left when looking at face)
    [[nodiscard]] std::array<float, 4> getFaceAO(
        const BlockPos& blockWorldPos,
        Face face,
        const BlockOpaqueProvider& opaqueProvider
    ) const;

    // Face vertex data (positions relative to block corner, normals, and UV corners)
    struct FaceData {
        std::array<glm::vec3, 4> positions;  // CCW winding
        glm::vec3 normal;
        std::array<glm::vec2, 4> uvOffsets;  // Relative UV offsets (0 or 1)
    };
    static const std::array<FaceData, 6> FACE_DATA;

    // ========================================================================
    // Greedy Meshing
    // ========================================================================

    // Data for a visible face in the greedy mesh mask
    struct FaceMaskEntry {
        BlockTypeId blockType = AIR_BLOCK_TYPE;  // Block type (AIR means no visible face)
        glm::vec4 uvBounds{0.0f};                // Texture UVs
        std::array<float, 4> aoValues{1.0f, 1.0f, 1.0f, 1.0f};  // AO per corner

        bool operator==(const FaceMaskEntry& other) const {
            // For greedy meshing, faces can merge if they have same block type and AO
            // UVs will be tiled, so we don't check them
            return blockType == other.blockType &&
                   aoValues == other.aoValues;
        }

        bool isEmpty() const { return blockType == AIR_BLOCK_TYPE; }
    };

    // Build mesh using greedy meshing algorithm
    void buildGreedyMesh(
        MeshData& mesh,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider
    );

    // Build mesh using simple per-face algorithm (non-greedy)
    void buildSimpleMesh(
        MeshData& mesh,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider
    );

    // Process one face direction for greedy meshing
    void greedyMeshFace(
        MeshData& mesh,
        Face face,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider
    );

    // Add a greedy-merged quad (larger than 1x1)
    void addGreedyQuad(
        MeshData& mesh,
        Face face,
        int sliceCoord,           // Position along face normal axis
        int startU, int startV,   // Start position in face tangent space
        int width, int height,    // Size of merged region
        const FaceMaskEntry& entry,
        const BlockTextureProvider& textureProvider
    );
};

// ============================================================================
// Utility functions
// ============================================================================

// Get unit normal vector for a face (as glm::vec3)
[[nodiscard]] inline glm::vec3 faceNormalVec3(Face face) {
    switch (face) {
        case Face::PosX: return glm::vec3( 1.0f,  0.0f,  0.0f);
        case Face::NegX: return glm::vec3(-1.0f,  0.0f,  0.0f);
        case Face::PosY: return glm::vec3( 0.0f,  1.0f,  0.0f);
        case Face::NegY: return glm::vec3( 0.0f, -1.0f,  0.0f);
        case Face::PosZ: return glm::vec3( 0.0f,  0.0f,  1.0f);
        case Face::NegZ: return glm::vec3( 0.0f,  0.0f, -1.0f);
        default: return glm::vec3(0.0f);
    }
}

// Get offset to neighbor block in the direction of a face
[[nodiscard]] inline BlockPos faceOffset(Face face) {
    switch (face) {
        case Face::PosX: return BlockPos( 1,  0,  0);
        case Face::NegX: return BlockPos(-1,  0,  0);
        case Face::PosY: return BlockPos( 0,  1,  0);
        case Face::NegY: return BlockPos( 0, -1,  0);
        case Face::PosZ: return BlockPos( 0,  0,  1);
        case Face::NegZ: return BlockPos( 0,  0, -1);
        default: return BlockPos(0, 0, 0);
    }
}

}  // namespace finevox
