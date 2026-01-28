#pragma once

/**
 * @file lod.hpp
 * @brief Level of detail system for distant chunks
 *
 * Design: [07-lod.md], [22-lod-extended.md]
 */

#include "finevox/position.hpp"
#include "finevox/subchunk.hpp"
#include <array>
#include <cstdint>
#include <cmath>
#include <glm/glm.hpp>

namespace finevox {

// ============================================================================
// LODMergeMode - How LOD blocks are merged/sized
// ============================================================================

/// Mode for how LOD blocks are sized when merging multiple source blocks
enum class LODMergeMode : uint8_t {
    /// Full height: LOD blocks are always full cubes (current behavior)
    /// Pros: Maximum hidden face removal, simplest
    /// Cons: Visual stepping at LOD boundaries
    FullHeight,

    /// Height-limited: LOD block height matches highest source block in group
    /// Pros: Smoother transitions at LOD boundaries
    /// Cons: More faces for top-layer blocks (no side culling)
    HeightLimited,

    /// No merge (debug): Each source block rendered individually at LOD resolution
    /// Pros: Maximum detail preservation
    /// Cons: No vertex reduction, defeats purpose of LOD
    NoMerge
};

// ============================================================================
// LODLevel - Level of detail enumeration
// ============================================================================

/// Level of detail for rendering
/// Higher LOD numbers = lower detail (more downsampling)
enum class LODLevel : uint8_t {
    LOD0 = 0,  // Full detail: 16x16x16 (1:1 blocks)
    LOD1 = 1,  // Half detail: 8x8x8 (2x2x2 block groups)
    LOD2 = 2,  // Quarter detail: 4x4x4 (4x4x4 block groups)
    LOD3 = 3,  // Eighth detail: 2x2x2 (8x8x8 block groups)
    LOD4 = 4,  // Minimum detail: 1x1x1 (entire subchunk = 1 block)
};

constexpr size_t LOD_LEVEL_COUNT = 5;

// ============================================================================
// LODRequest - Fractional LOD request with hysteresis encoding
// ============================================================================

/// LOD request using 2x encoding for hysteresis
/// - Even values (0, 2, 4, 6, 8) = exact LOD match required (LOD0, LOD1, LOD2, LOD3, LOD4)
/// - Odd values (1, 3, 5, 7) = flexible, matches either neighboring LOD
///
/// Example:
///   Request 2 (LOD1 exact) - only LOD1 mesh is acceptable
///   Request 3 (LOD1-LOD2 flexible) - either LOD1 or LOD2 mesh is acceptable
///   Request 4 (LOD2 exact) - only LOD2 mesh is acceptable
///
/// This encoding allows stateless hysteresis: when in a transition zone,
/// the request is flexible, accepting meshes built at either neighboring level.
/// This prevents thrashing when camera moves back and forth near boundaries.
struct LODRequest {
    uint8_t value;  // 2x LOD level, odd = flexible

    /// Create exact LOD request (only this level is acceptable)
    static constexpr LODRequest exact(LODLevel level) {
        return LODRequest{static_cast<uint8_t>(static_cast<int>(level) * 2)};
    }

    /// Create flexible LOD request (accepts level or level+1)
    /// @param level The lower LOD level (higher detail)
    static constexpr LODRequest flexible(LODLevel level) {
        return LODRequest{static_cast<uint8_t>(static_cast<int>(level) * 2 + 1)};
    }

    /// Check if this request is flexible (odd value)
    [[nodiscard]] constexpr bool isFlexible() const { return (value & 1) != 0; }

    /// Check if this request is exact (even value)
    [[nodiscard]] constexpr bool isExact() const { return (value & 1) == 0; }

    /// Get the base LOD level (for exact: the level, for flexible: the lower/finer level)
    [[nodiscard]] constexpr LODLevel baseLevel() const {
        return static_cast<LODLevel>(value / 2);
    }

    /// Get the LOD level to build (always builds at the base level)
    [[nodiscard]] constexpr LODLevel buildLevel() const {
        return baseLevel();
    }

    /// Check if a mesh built at the given LOD level satisfies this request
    [[nodiscard]] constexpr bool accepts(LODLevel meshLevel) const {
        int meshValue = static_cast<int>(meshLevel) * 2;
        int diff = meshValue - static_cast<int>(value);
        // Exact: diff must be 0
        // Flexible: diff can be -1 (mesh is finer) or +1 (mesh is coarser)
        return diff >= -1 && diff <= 1;
    }

    /// Equality comparison
    [[nodiscard]] constexpr bool operator==(LODRequest other) const { return value == other.value; }
    [[nodiscard]] constexpr bool operator!=(LODRequest other) const { return value != other.value; }
};

/// Check if a mesh at the given LOD level satisfies a request
[[nodiscard]] constexpr bool lodMatches(LODRequest request, LODLevel meshLevel) {
    return request.accepts(meshLevel);
}

/// Get the block grouping factor for an LOD level (1, 2, 4, 8, or 16)
[[nodiscard]] constexpr int lodBlockGrouping(LODLevel level) {
    return 1 << static_cast<int>(level);
}

/// Get the effective resolution for an LOD level (16, 8, 4, 2, or 1)
[[nodiscard]] constexpr int lodResolution(LODLevel level) {
    return 16 >> static_cast<int>(level);
}

// ============================================================================
// LODConfig - Configuration for LOD distance thresholds
// ============================================================================

/// Configuration for LOD distance thresholds and debug settings
class LODConfig {
public:
    /// Distance thresholds for each LOD level (in blocks)
    /// LOD0: 0 to distances[0]
    /// LOD1: distances[0] to distances[1]
    /// etc.
    std::array<float, LOD_LEVEL_COUNT> distances = {
        32.0f,   // LOD0 threshold (full detail up to 32 blocks)
        64.0f,   // LOD1 threshold (2x downsampling up to 64 blocks)
        128.0f,  // LOD2 threshold (4x downsampling up to 128 blocks)
        256.0f,  // LOD3 threshold (8x downsampling up to 256 blocks)
        512.0f   // LOD4 threshold (16x downsampling beyond 256 blocks)
    };

    /// Hysteresis for LOD transitions (prevents rapid switching at boundary)
    /// When transitioning to lower detail: use distance + hysteresis
    /// When transitioning to higher detail: use distance - hysteresis
    float hysteresis = 4.0f;

    /// Debug: shift all LOD distances by this factor
    /// lodBias = 0: normal behavior
    /// lodBias = 1: everything renders at LOD1 (as if 2x farther)
    /// lodBias = 2: everything renders at LOD2 (as if 4x farther)
    /// lodBias = -1: everything renders at LOD0 (full detail always)
    int lodBias = 0;

    /// Debug: force specific LOD level (-1 = use distance-based)
    int forceLOD = -1;

    /// Get the LOD request for a given distance from the camera
    /// Returns a request that encodes hysteresis: exact when clearly in one zone,
    /// flexible when in the transition zone between two levels.
    /// @param distance Distance from camera in blocks
    /// @return LODRequest that may accept one or two LOD levels
    [[nodiscard]] LODRequest getRequestForDistance(float distance) const {
        // Handle force LOD mode
        if (forceLOD >= 0 && forceLOD < static_cast<int>(LOD_LEVEL_COUNT)) {
            return LODRequest::exact(static_cast<LODLevel>(forceLOD));
        }

        // Apply bias by shifting effective distance
        float effectiveDistance = distance;
        if (lodBias > 0) {
            effectiveDistance *= static_cast<float>(1 << lodBias);
        } else if (lodBias < 0) {
            effectiveDistance /= static_cast<float>(1 << (-lodBias));
        }

        // Find which zone we're in
        // For each threshold, check if we're in the hysteresis band
        for (int i = 0; i < static_cast<int>(LOD_LEVEL_COUNT) - 1; ++i) {
            float threshold = distances[i];
            float lowerBound = threshold - hysteresis;
            float upperBound = threshold + hysteresis;

            if (effectiveDistance < lowerBound) {
                // Clearly in LOD level i (exact)
                return LODRequest::exact(static_cast<LODLevel>(i));
            }
            if (effectiveDistance < upperBound) {
                // In hysteresis zone between i and i+1 (flexible)
                return LODRequest::flexible(static_cast<LODLevel>(i));
            }
        }

        // Beyond all thresholds, use LOD4 (exact)
        return LODRequest::exact(LODLevel::LOD4);
    }

    /// Get the LOD level for a given distance (legacy interface, no hysteresis encoding)
    /// @param distance Distance from camera in blocks
    /// @param currentLevel Current LOD level (for hysteresis - decides which side to favor)
    /// @return Appropriate LOD level for this distance
    [[nodiscard]] LODLevel getLevelForDistance(float distance, LODLevel currentLevel = LODLevel::LOD0) const {
        LODRequest request = getRequestForDistance(distance);
        if (request.isExact()) {
            return request.baseLevel();
        }
        // Flexible: prefer current level if it matches, otherwise use base level
        if (request.accepts(currentLevel)) {
            return currentLevel;
        }
        return request.baseLevel();
    }

    /// Get LOD level without hysteresis (for initial assignment)
    [[nodiscard]] LODLevel getLevelForDistanceSimple(float distance) const {
        return getRequestForDistance(distance).baseLevel();
    }

    /// Calculate distance from camera position to chunk center
    [[nodiscard]] static float distanceToChunk(const glm::dvec3& cameraPos, ChunkPos chunkPos) {
        // Chunk center in world coordinates
        double cx = static_cast<double>(chunkPos.x) * 16.0 + 8.0;
        double cy = static_cast<double>(chunkPos.y) * 16.0 + 8.0;
        double cz = static_cast<double>(chunkPos.z) * 16.0 + 8.0;

        double dx = cameraPos.x - cx;
        double dy = cameraPos.y - cy;
        double dz = cameraPos.z - cz;

        return static_cast<float>(std::sqrt(dx*dx + dy*dy + dz*dz));
    }

    /// Calculate distance from camera position to chunk center (single precision)
    [[nodiscard]] static float distanceToChunk(const glm::vec3& cameraPos, ChunkPos chunkPos) {
        return distanceToChunk(glm::dvec3(cameraPos), chunkPos);
    }
};

// ============================================================================
// LODSubChunk - Downsampled block storage for LOD levels
// ============================================================================

/// Result of selecting a representative block for an LOD cell
struct LODBlockInfo {
    BlockTypeId type = AIR_BLOCK_TYPE;
    uint8_t height = 0;  // Height in source blocks (0 = air, 1-grouping = solid height)
};

/// Downsampled block data for a subchunk at a specific LOD level
/// Stores representative blocks for grouped regions
class LODSubChunk {
public:
    /// Create an LOD subchunk for the given level
    /// @param level LOD level (LOD1-LOD4, LOD0 uses regular SubChunk)
    explicit LODSubChunk(LODLevel level = LODLevel::LOD1);

    /// Get the LOD level
    [[nodiscard]] LODLevel level() const { return level_; }

    /// Get the effective resolution (8, 4, 2, or 1)
    [[nodiscard]] int resolution() const { return lodResolution(level_); }

    /// Get the block grouping factor (2, 4, 8, or 16)
    [[nodiscard]] int grouping() const { return lodBlockGrouping(level_); }

    /// Get block type at LOD coordinates
    /// Coordinates range from 0 to resolution()-1
    [[nodiscard]] BlockTypeId getBlock(int x, int y, int z) const;

    /// Get block info (type + height) at LOD coordinates
    [[nodiscard]] LODBlockInfo getBlockInfo(int x, int y, int z) const;

    /// Set block type at LOD coordinates
    void setBlock(int x, int y, int z, BlockTypeId type);

    /// Set block info (type + height) at LOD coordinates
    void setBlockInfo(int x, int y, int z, const LODBlockInfo& info);

    /// Get total number of cells at this LOD level
    [[nodiscard]] int volume() const {
        int r = resolution();
        return r * r * r;
    }

    /// Check if all blocks are air
    [[nodiscard]] bool isEmpty() const { return nonAirCount_ == 0; }

    /// Count of non-air blocks
    [[nodiscard]] int nonAirCount() const { return nonAirCount_; }

    /// Clear all blocks to air
    void clear();

    /// Generate LOD data from a full-resolution SubChunk
    /// Uses mode-based block selection (most common solid block in group)
    /// @param source The full-resolution subchunk to downsample
    /// @param mergeMode How to merge blocks (affects height calculation)
    void downsampleFrom(const SubChunk& source, LODMergeMode mergeMode = LODMergeMode::FullHeight);

    /// Get version for cache invalidation
    [[nodiscard]] uint64_t version() const { return version_; }

    /// Increment version (called when data changes)
    void incrementVersion() { ++version_; }

private:
    LODLevel level_;
    std::vector<BlockTypeId> blocks_;  // Size = volume()
    std::vector<uint8_t> heights_;     // Size = volume(), height in source blocks
    int nonAirCount_ = 0;
    uint64_t version_ = 0;

    /// Convert LOD coordinates to array index
    [[nodiscard]] int toIndex(int x, int y, int z) const {
        int r = resolution();
        return y * r * r + z * r + x;
    }

    /// Select representative block from a group using mode (most common)
    /// Returns block type and height (topmost occupied Y within the group)
    [[nodiscard]] LODBlockInfo selectRepresentativeBlock(
        const SubChunk& source,
        int groupX, int groupY, int groupZ) const;
};

// ============================================================================
// LOD Debug Mode
// ============================================================================

/// Debug visualization modes for LOD system
enum class LODDebugMode : uint8_t {
    None,           // Normal rendering
    ColorByLOD,     // Tint chunks by LOD level
    WireframeByLOD, // Wireframe for non-LOD0 chunks
    ShowBoundaries, // Highlight LOD transition boundaries
};

/// Get debug color for an LOD level (for ColorByLOD mode)
[[nodiscard]] inline glm::vec3 lodDebugColor(LODLevel level) {
    switch (level) {
        case LODLevel::LOD0: return glm::vec3(1.0f, 0.2f, 0.2f);  // Red
        case LODLevel::LOD1: return glm::vec3(1.0f, 0.6f, 0.2f);  // Orange
        case LODLevel::LOD2: return glm::vec3(1.0f, 1.0f, 0.2f);  // Yellow
        case LODLevel::LOD3: return glm::vec3(0.2f, 1.0f, 0.2f);  // Green
        case LODLevel::LOD4: return glm::vec3(0.2f, 0.6f, 1.0f);  // Blue
        default: return glm::vec3(1.0f);  // White
    }
}

/// Get name string for LOD level
[[nodiscard]] inline const char* lodLevelName(LODLevel level) {
    switch (level) {
        case LODLevel::LOD0: return "LOD0 (16x16x16)";
        case LODLevel::LOD1: return "LOD1 (8x8x8)";
        case LODLevel::LOD2: return "LOD2 (4x4x4)";
        case LODLevel::LOD3: return "LOD3 (2x2x2)";
        case LODLevel::LOD4: return "LOD4 (1x1x1)";
        default: return "Unknown";
    }
}

}  // namespace finevox
