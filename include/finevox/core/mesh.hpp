#pragma once

/**
 * @file mesh.hpp
 * @brief Greedy meshing for SubChunk rendering
 *
 * Design: [06-rendering.md] §6.2 Mesh Generation
 */

#include "finevox/core/position.hpp"
#include "finevox/core/string_interner.hpp"  // For BlockTypeId
#include "finevox/core/physics.hpp"
#include "finevox/core/lod.hpp"
#include "finevox/core/block_model.hpp"
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
    glm::vec2 texCoord;   // Texture coordinates (may extend beyond 0-1 for tiling)
    glm::vec4 tileBounds; // Texture tile bounds (minU, minV, maxU, maxV) for atlas tiling
    float ao;             // Ambient occlusion (0-1, 1 = fully lit)
    float skyLight;       // Sky light (0-1, from LightEngine sky channel)
    float blockLight;     // Block light (0-1, from LightEngine block channel)

    ChunkVertex() = default;
    ChunkVertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& tex,
                const glm::vec4& tile, float ambientOcclusion,
                float sky = 1.0f, float block = 0.0f)
        : position(pos), normal(norm), texCoord(tex), tileBounds(tile),
          ao(ambientOcclusion), skyLight(sky), blockLight(block) {}

    bool operator==(const ChunkVertex& other) const {
        return position == other.position &&
               normal == other.normal &&
               texCoord == other.texCoord &&
               tileBounds == other.tileBounds &&
               ao == other.ao &&
               skyLight == other.skyLight &&
               blockLight == other.blockLight;
    }

    /// Get combined brightness (AO * max light) for final rendering
    [[nodiscard]] float combinedBrightness() const { return ao * std::max(skyLight, blockLight); }
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
// SubChunkMeshData - Combined opaque and transparent mesh data
// ============================================================================

struct SubChunkMeshData {
    MeshData opaque;       // Opaque blocks (rendered first, no sorting)
    MeshData transparent;  // Transparent blocks (rendered second, may need sorting)

    [[nodiscard]] bool isEmpty() const {
        return opaque.isEmpty() && transparent.isEmpty();
    }

    void clear() {
        opaque.clear();
        transparent.clear();
    }

    [[nodiscard]] size_t totalVertexCount() const {
        return opaque.vertexCount() + transparent.vertexCount();
    }

    [[nodiscard]] size_t totalIndexCount() const {
        return opaque.indexCount() + transparent.indexCount();
    }

    [[nodiscard]] size_t totalMemoryUsage() const {
        return opaque.memoryUsage() + transparent.memoryUsage();
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

// Callback to check if a block is transparent
// Returns true if the block at the given position is transparent (needs separate render pass)
using BlockTransparentProvider = std::function<bool(BlockTypeId type)>;

// Callback to get texture UVs for a block face
// Returns UV coordinates (minU, minV, maxU, maxV) for the given block type and face
using BlockTextureProvider = std::function<glm::vec4(BlockTypeId type, Face face)>;

// Callback to get packed light at a position for smooth lighting
// Returns packed byte: sky light in high nibble (bits 4-7), block light in low nibble (bits 0-3)
// This matches LightData internal format: (sky << 4) | block
// If null, mesh builder will use default lighting (full sky brightness, no block light)
using BlockLightProvider = std::function<uint8_t(const BlockPos& pos)>;

// Callback to check if a block type has custom geometry (non-cube)
// Returns true if the block should use custom mesh instead of standard cube faces
using BlockCustomMeshCheck = std::function<bool(BlockTypeId type)>;

// Callback to get custom geometry for a block type
// Returns pointer to BlockGeometry if the block has custom mesh, nullptr otherwise
// The returned pointer must remain valid for the duration of mesh building
using BlockGeometryProvider = std::function<const BlockGeometry*(BlockTypeId type)>;

// Callback to check if a specific face of a block at a position occludes neighbors
// Returns true if the face at (pos, face) is solid and would hide the opposite face of adjacent block
// For standard opaque blocks, all faces occlude
// For custom geometry blocks, only faces in solidFacesMask occlude
// For air/transparent blocks, no faces occlude
using BlockFaceOccludesProvider = std::function<bool(const BlockPos& pos, Face face)>;

// ============================================================================
// MeshBuilder - Generates mesh data from subchunk blocks
// ============================================================================

class MeshBuilder {
public:
    MeshBuilder();

    // Build mesh for a subchunk using simple face culling
    // opaqueProvider: checks if neighboring blocks are opaque (for culling hidden faces)
    // textureProvider: gets UV coordinates for each block face
    // Returns mesh data for the opaque pass only (legacy interface)
    [[nodiscard]] MeshData buildSubChunkMesh(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider
    );

    // Build mesh using World for neighbor lookups
    // This is a convenience method that creates providers from World access
    // Returns mesh data for the opaque pass only (legacy interface)
    [[nodiscard]] MeshData buildSubChunkMesh(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const World& world,
        const BlockTextureProvider& textureProvider
    );

    // Build mesh with separate opaque and transparent passes
    // transparentProvider: checks if a block type is transparent
    // Returns both opaque mesh (for early pass) and transparent mesh (for sorted pass)
    [[nodiscard]] SubChunkMeshData buildSubChunkMeshSplit(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTransparentProvider& transparentProvider,
        const BlockTextureProvider& textureProvider
    );

    // Build mesh with separate opaque and transparent passes using World
    [[nodiscard]] SubChunkMeshData buildSubChunkMeshSplit(
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const World& world,
        const BlockTransparentProvider& transparentProvider,
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

    // Enable/disable smooth lighting (interpolates light at vertices)
    void setSmoothLighting(bool enabled) { smoothLighting_ = enabled; }
    [[nodiscard]] bool smoothLighting() const { return smoothLighting_; }

    // Enable/disable flat lighting (single light sample per face, no interpolation)
    // When enabled, shows the raw L1 ball without smoothing. Requires light provider.
    // Note: smoothLighting takes precedence if both are enabled.
    void setFlatLighting(bool enabled) { flatLighting_ = enabled; }
    [[nodiscard]] bool flatLighting() const { return flatLighting_; }

    // Set light provider for smooth/flat lighting calculations
    void setLightProvider(BlockLightProvider provider) { lightProvider_ = std::move(provider); }
    void clearLightProvider() { lightProvider_ = nullptr; }

    // Set custom geometry provider for non-cube blocks
    void setGeometryProvider(BlockGeometryProvider provider) { geometryProvider_ = std::move(provider); }
    void clearGeometryProvider() { geometryProvider_ = nullptr; }

    // Set face occludes provider for directional face culling (used with custom geometry blocks)
    // When set, this replaces the simple opaqueProvider check with per-face occlusion testing
    void setFaceOccludesProvider(BlockFaceOccludesProvider provider) { faceOccludesProvider_ = std::move(provider); }
    void clearFaceOccludesProvider() { faceOccludesProvider_ = nullptr; }

    // ========================================================================
    // LOD Mesh Generation
    // ========================================================================

    /// Build mesh for an LOD subchunk (downsampled block data)
    /// Blocks are scaled up by the LOD grouping factor
    /// @param lodSubChunk The downsampled block data
    /// @param chunkPos The chunk position in world coordinates
    /// @param textureProvider Callback to get texture UVs for block faces
    /// @return Mesh data with scaled block geometry
    [[nodiscard]] MeshData buildLODMesh(
        const LODSubChunk& lodSubChunk,
        ChunkPos chunkPos,
        const BlockTextureProvider& textureProvider
    );

    /// Build mesh for an LOD subchunk with neighbor culling
    /// @param lodSubChunk The downsampled block data
    /// @param chunkPos The chunk position in world coordinates
    /// @param neighborProvider Callback to check if LOD neighbor positions are opaque
    /// @param textureProvider Callback to get texture UVs for block faces
    /// @return Mesh data with scaled block geometry and hidden face removal
    [[nodiscard]] MeshData buildLODMesh(
        const LODSubChunk& lodSubChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& neighborProvider,
        const BlockTextureProvider& textureProvider
    );

    /// Build mesh for an LOD subchunk with merge mode control
    /// @param lodSubChunk The downsampled block data (with height info if HeightLimited)
    /// @param chunkPos The chunk position in world coordinates
    /// @param neighborProvider Callback to check if LOD neighbor positions are opaque
    /// @param textureProvider Callback to get texture UVs for block faces
    /// @param mergeMode How to handle block heights (FullHeight, HeightLimited, NoMerge)
    /// @return Mesh data with scaled block geometry
    [[nodiscard]] MeshData buildLODMesh(
        const LODSubChunk& lodSubChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& neighborProvider,
        const BlockTextureProvider& textureProvider,
        LODMergeMode mergeMode
    );

private:
    bool calculateAO_ = true;
    bool greedyMeshing_ = true;  // Enabled by default
    bool disableFaceCulling_ = false;
    bool smoothLighting_ = false;  // Disabled by default (use when LightEngine is available)
    bool flatLighting_ = false;   // Single light sample per face (shows raw L1 ball)
    BlockLightProvider lightProvider_;  // Optional provider for smooth/flat lighting
    BlockGeometryProvider geometryProvider_;  // Optional provider for custom block geometry
    BlockFaceOccludesProvider faceOccludesProvider_;  // Optional provider for per-face occlusion

    // Add a single face to the mesh data
    void addFace(
        MeshData& mesh,
        const glm::vec3& blockPos,   // Local block position within subchunk
        Face face,
        const glm::vec4& uvBounds,   // (minU, minV, maxU, maxV)
        const std::array<float, 4>& aoValues,  // AO for each corner (CCW from bottom-left)
        const std::array<float, 4>& skyLightValues = {1.0f, 1.0f, 1.0f, 1.0f},
        const std::array<float, 4>& blockLightValues = {0.0f, 0.0f, 0.0f, 0.0f}
    );

    // Add a custom face from BlockGeometry (for non-cube blocks)
    // Handles arbitrary vertex counts (3-6 vertices per face)
    void addCustomFace(
        MeshData& mesh,
        const glm::vec3& blockPos,   // Local block position within subchunk
        const FaceGeometry& face,    // Face geometry with vertices and UVs
        const glm::vec4& uvBounds,   // Texture tile bounds for atlas wrapping
        float ao = 1.0f,             // Ambient occlusion (uniform for custom faces)
        float sky = 1.0f,            // Sky light level (uniform for custom faces)
        float block = 0.0f           // Block light level (uniform for custom faces)
    );

    // Add a scaled face to the mesh data (for LOD blocks)
    // blockScale: size of the LOD block (2, 4, 8, or 16)
    void addScaledFace(
        MeshData& mesh,
        const glm::vec3& blockPos,   // Local block position within subchunk (in world blocks)
        Face face,
        float blockScale,            // Scale factor for the block
        const glm::vec4& uvBounds,   // (minU, minV, maxU, maxV)
        const std::array<float, 4>& aoValues,  // AO for each corner
        float skyLight = 1.0f,       // Sky light value (uniform across face for LOD)
        float blockLightVal = 0.0f   // Block light value (uniform across face for LOD)
    );

    // Add a height-limited scaled face (for HeightLimited LOD mode)
    // The block only extends from 0 to height (instead of 0 to blockScale)
    void addHeightLimitedFace(
        MeshData& mesh,
        const glm::vec3& blockPos,   // Local block position within subchunk
        Face face,
        float blockScale,            // Full scale factor
        float height,                // Actual height (1 to blockScale)
        const glm::vec4& uvBounds,
        const std::array<float, 4>& aoValues,
        float skyLight = 1.0f,       // Sky light value (uniform across face for LOD)
        float blockLightVal = 0.0f   // Block light value (uniform across face for LOD)
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

    // Get separate sky and block light values for a face (CCW from bottom-left)
    // Averages light from the 4 blocks adjacent to each vertex
    // Returns {skyLightValues, blockLightValues}
    struct FaceLightResult {
        std::array<float, 4> sky{1.0f, 1.0f, 1.0f, 1.0f};
        std::array<float, 4> block{0.0f, 0.0f, 0.0f, 0.0f};
    };
    [[nodiscard]] FaceLightResult getFaceSkyBlockLight(
        const BlockPos& blockWorldPos,
        Face face
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
        std::array<float, 4> aoValues{1.0f, 1.0f, 1.0f, 1.0f};        // AO per corner
        std::array<float, 4> skyLightValues{1.0f, 1.0f, 1.0f, 1.0f};  // Sky light per corner
        std::array<float, 4> blockLightValues{0.0f, 0.0f, 0.0f, 0.0f};// Block light per corner

        bool operator==(const FaceMaskEntry& other) const {
            // For greedy meshing, faces can merge if they have same block type, AO, and light
            // UVs will be tiled, so we don't check them
            return blockType == other.blockType &&
                   aoValues == other.aoValues &&
                   skyLightValues == other.skyLightValues &&
                   blockLightValues == other.blockLightValues;
        }

        bool isEmpty() const { return blockType == AIR_BLOCK_TYPE; }
    };

    // Build mesh using greedy meshing algorithm
    void buildGreedyMesh(
        MeshData& mesh,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider,
        const BlockTransparentProvider* transparentProvider = nullptr,
        bool buildTransparent = false  // false = opaque only, true = transparent only
    );

    // Build mesh using simple per-face algorithm (non-greedy)
    void buildSimpleMesh(
        MeshData& mesh,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider,
        const BlockTransparentProvider* transparentProvider = nullptr,
        bool buildTransparent = false  // false = opaque only, true = transparent only
    );

    // Process one face direction for greedy meshing
    void greedyMeshFace(
        MeshData& mesh,
        Face face,
        const SubChunk& subChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& opaqueProvider,
        const BlockTextureProvider& textureProvider,
        const BlockTransparentProvider* transparentProvider,
        bool buildTransparent
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

    // ========================================================================
    // LOD Greedy Meshing
    // ========================================================================

    // LOD face mask entry - includes scale information
    struct LODFaceMaskEntry {
        BlockTypeId blockType = AIR_BLOCK_TYPE;
        glm::vec4 uvBounds{0.0f};
        float height = 0.0f;      // For height-limited mode
        float skyLight = 1.0f;    // Sky light value
        float blockLightVal = 0.0f;// Block light value

        bool operator==(const LODFaceMaskEntry& other) const {
            // For greedy meshing, faces can merge if same block type, height, and light
            return blockType == other.blockType &&
                   height == other.height &&
                   skyLight == other.skyLight &&
                   blockLightVal == other.blockLightVal;
        }

        bool operator!=(const LODFaceMaskEntry& other) const {
            return !(*this == other);
        }

        bool isEmpty() const { return blockType == AIR_BLOCK_TYPE; }
    };

    // Build LOD mesh using greedy meshing algorithm
    void buildGreedyLODMesh(
        MeshData& mesh,
        const LODSubChunk& lodSubChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& neighborProvider,
        const BlockTextureProvider& textureProvider,
        LODMergeMode mergeMode
    );

    // Process one face direction for LOD greedy meshing
    void greedyMeshLODFace(
        MeshData& mesh,
        Face face,
        const LODSubChunk& lodSubChunk,
        ChunkPos chunkPos,
        const BlockOpaqueProvider& neighborProvider,
        const BlockTextureProvider& textureProvider,
        LODMergeMode mergeMode
    );

    // Add a greedy-merged LOD quad (scaled up)
    void addGreedyLODQuad(
        MeshData& mesh,
        Face face,
        int sliceCoord,           // Position along face normal axis (in LOD cells)
        int startU, int startV,   // Start position in face tangent space (in LOD cells)
        int width, int height,    // Size of merged region (in LOD cells)
        float blockScale,         // Scale of each LOD cell (2, 4, 8, etc.)
        float blockHeight,        // Actual height for HeightLimited mode (0 = use blockScale)
        const LODFaceMaskEntry& entry,
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
